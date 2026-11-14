#include "dbc.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>

#define LINE_LEN 512

static char *skip_ws(char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

static void copy_name(char *dst, size_t len, const char *src, size_t n)
{
    if (n >= len) {
        n = len - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* BO_ 513 BASE1: 8 Vector__XXX */
static bool parse_bo(dbc_t *db, char *p)
{
    if (db->nmsg >= db->max_msg) {
        return false;
    }
    char *end;
    unsigned long raw = strtoul(p, &end, 10);
    if (end == p) {
        return false;
    }
    p = skip_ws(end);
    char *colon = strchr(p, ':');
    if (colon == NULL) {
        return false;
    }
    dbc_message_t *m = &db->msgs[db->nmsg];
    memset(m, 0, sizeof(*m));
    char *ne = colon;
    while (ne > p && (ne[-1] == ' ' || ne[-1] == '\t')) {
        ne--;
    }
    copy_name(m->name, sizeof(m->name), p, (size_t)(ne - p));
    m->ext = (raw & 0x80000000ul) != 0;
    m->id = (uint32_t)(raw & 0x1FFFFFFFul);
    m->dlc = (uint8_t)strtoul(colon + 1, NULL, 10);
    m->first_sig = db->nsig;
    m->mux_sig = -1;
    db->nmsg++;
    return true;
}

/*  SG_ RPM : 16|16@1+ (1,0) [0|8000] "rpm" Vector__XXX
 *  SG_ Mux M : 0|8@1+ (1,0) [0|255] "" X
 *  SG_ Val m3 : 8|16@1- (0.1,-40) [-40|200] "C" X */
static bool parse_sg(dbc_t *db, char *p)
{
    if (db->nmsg == 0 || db->nsig >= db->max_sig) {
        return false;
    }
    dbc_message_t *m = &db->msgs[db->nmsg - 1];
    if (m->first_sig + m->nsig != db->nsig) {
        return false;               /* signals must follow their message */
    }
    char *colon = strchr(p, ':');
    if (colon == NULL) {
        return false;
    }
    dbc_signal_t *s = &db->sigs[db->nsig];
    memset(s, 0, sizeof(*s));
    s->mux = -1;

    /* name and optional multiplex indicator before the colon */
    char *ne = p;
    while (ne < colon && *ne != ' ' && *ne != '\t') {
        ne++;
    }
    copy_name(s->name, sizeof(s->name), p, (size_t)(ne - p));
    char *mx = skip_ws(ne);
    if (mx < colon) {
        if (*mx == 'M') {
            s->mux = -2;
        } else if (*mx == 'm' && isdigit((unsigned char)mx[1])) {
            s->mux = (int16_t)atoi(mx + 1);     /* "m3", also "m3M" (extended mux) */
        }
    }

    unsigned start, len, order;
    char sign;
    double scale, offset, mn = 0, mxv = 0;
    int n = sscanf(colon + 1, " %u|%u@%u%c (%lf,%lf) [%lf|%lf]", &start, &len, &order, &sign,
                   &scale, &offset, &mn, &mxv);
    if (n < 6 || len == 0 || len > 64 || start > 63 || (sign != '+' && sign != '-')) {
        return false;
    }
    s->start = (uint8_t)start;
    s->len = (uint8_t)len;
    s->intel = order == 1;
    s->is_signed = sign == '-';
    s->scale = scale;
    s->offset = offset;
    s->min = mn;
    s->max = mxv;
    char *q1 = strchr(colon, '"');
    char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
    if (q1 && q2) {
        copy_name(s->unit, sizeof(s->unit), q1 + 1, (size_t)(q2 - q1 - 1));
    }
    s->msg = (uint16_t)(db->nmsg - 1);
    if (s->mux == -2) {
        m->mux_sig = (int16_t)(db->nsig - m->first_sig);    /* relative, fixed after sorting */
    }
    m->nsig++;
    db->nsig++;
    return true;
}

static int msg_cmp(const void *a, const void *b)
{
    const dbc_message_t *x = a, *y = b;
    uint64_t kx = (uint64_t)x->ext << 32 | x->id;
    uint64_t ky = (uint64_t)y->ext << 32 | y->id;
    return kx < ky ? -1 : kx > ky;
}

int dbc_parse(dbc_t *db, FILE *f)
{
    db->nmsg = db->nsig = 0;
    db->skipped = 0;
    db->id_offset = 0;
    char *line = malloc(LINE_LEN);
    if (line == NULL) {
        return -1;
    }
    while (fgets(line, LINE_LEN, f)) {
        size_t l = strlen(line);
        if (l == LINE_LEN - 1 && line[l - 1] != '\n') {
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {
            }
            continue;                           /* too long: skip */
        }
        char *p = skip_ws(line);
        if (strncmp(p, "BO_ ", 4) == 0) {
            if (!parse_bo(db, p + 4)) {
                db->skipped++;
            }
        } else if (strncmp(p, "SG_ ", 4) == 0) {
            if (!parse_sg(db, skip_ws(p + 4))) {
                db->skipped++;
            }
        }
    }
    free(line);
    if (ferror(f)) {
        return -1;
    }
    /* Sort messages for the lookup; signals stay in file order, so remap
     * their message index after sorting. */
    for (uint16_t i = 0; i < db->nmsg; i++) {
        db->msgs[i].count = i;                  /* original index, temporarily */
    }
    qsort(db->msgs, db->nmsg, sizeof(dbc_message_t), msg_cmp);
    for (uint16_t i = 0; i < db->nmsg; i++) {
        dbc_message_t *m = &db->msgs[i];
        for (uint16_t k = 0; k < m->nsig; k++) {
            db->sigs[m->first_sig + k].msg = i;
        }
        if (m->mux_sig >= 0) {
            m->mux_sig = (int16_t)(m->first_sig + m->mux_sig);
        }
        m->count = 0;
    }
    return db->nsig;
}

uint64_t dbc_extract(const uint8_t *d, uint8_t start, uint8_t len, bool intel)
{
    uint64_t mask = len >= 64 ? ~0ull : ((1ull << len) - 1);
    if (intel) {
        uint64_t v = 0;
        for (int i = 7; i >= 0; i--) {
            v = v << 8 | d[i];
        }
        return (v >> start) & mask;
    }
    /* Motorola: start = MSB in the DBC's "sawtooth" bit numbering */
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v = v << 8 | d[i];
    }
    unsigned msb = (start / 8u) * 8u + (7u - start % 8u);     /* 0 = MSB of byte 0 */
    if (msb + len > 64) {
        return 0;
    }
    return (v >> (64 - (msb + len))) & mask;
}

static double physical(const dbc_signal_t *s, const uint8_t *d)
{
    uint64_t raw = dbc_extract(d, s->start, s->len, s->intel);
    if (s->is_signed && s->len < 64 && (raw & (1ull << (s->len - 1)))) {
        return (double)((int64_t)raw - (int64_t)(1ull << s->len)) * s->scale + s->offset;
    }
    if (s->is_signed && s->len == 64) {
        return (double)(int64_t)raw * s->scale + s->offset;
    }
    return (double)raw * s->scale + s->offset;
}

int dbc_base_id(const dbc_t *db)
{
    for (uint16_t i = 0; i < db->nmsg; i++) {
        if (!db->msgs[i].ext) {
            return (int)db->msgs[i].id;         /* sorted: the first standard one is the lowest */
        }
    }
    return -1;
}

int dbc_decode(dbc_t *db, uint32_t id, bool ext, const uint8_t *data, uint8_t dlc, uint32_t ts_ms)
{
    if (!ext) {
        int64_t dbc_id = (int64_t)id - db->id_offset;
        if (dbc_id < 0 || dbc_id > 0x7FF) {
            return -1;
        }
        id = (uint32_t)dbc_id;
    }
    int lo = 0, hi = db->nmsg - 1;
    uint64_t key = (uint64_t)ext << 32 | id;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        dbc_message_t *m = &db->msgs[mid];
        uint64_t k = (uint64_t)m->ext << 32 | m->id;
        if (k < key) {
            lo = mid + 1;
        } else if (k > key) {
            hi = mid - 1;
        } else {
            uint8_t d[8] = { 0 };
            memcpy(d, data, dlc > 8 ? 8 : dlc);
            long muxv = -1;
            if (m->mux_sig >= 0) {
                muxv = (long)physical(&db->sigs[m->mux_sig], d);
            }
            for (uint16_t i = 0; i < m->nsig; i++) {
                dbc_signal_t *s = &db->sigs[m->first_sig + i];
                if (s->mux >= 0 && s->mux != muxv) {
                    continue;
                }
                /* signal beyond the bytes received: leave the old value */
                unsigned last_bit = s->intel ? s->start + s->len - 1u
                                             : (s->start / 8u) * 8u + (7u - s->start % 8u) + s->len - 1u;
                if (last_bit / 8u >= dlc) {
                    continue;
                }
                s->value = physical(s, d);
                s->ts_ms = ts_ms ? ts_ms : 1;
            }
            m->count++;
            return mid;
        }
    }
    return -1;
}

int dbc_find(const dbc_t *db, const char *name)
{
    for (uint16_t i = 0; i < db->nsig; i++) {
        if (strcasecmp(db->sigs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}
