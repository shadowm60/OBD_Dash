#include "diagdb.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>

#define LINE_LEN  256
#define MAX_COLS  9

enum { SEC_NONE, SEC_ECU, SEC_DID, SEC_DTC };

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static void copy(char *dst, size_t len, const char *src)
{
    size_t n = strlen(src);
    if (n >= len) n = len - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool hexnum(const char *s, unsigned long maxv, unsigned long *out)
{
    if (*s == '\0') return false;
    char *e;
    unsigned long v = strtoul(s, &e, 16);
    if (*e != '\0' || v > maxv) return false;
    *out = v;
    return true;
}

static bool number(const char *s, double dflt, double *out)
{
    if (*s == '\0') {
        *out = dflt;
        return true;
    }
    char *e;
    *out = strtod(s, &e);
    return *e == '\0';
}

bool diagdb_parse_dtc(const char *in, uint32_t *code)
{
    char s[16];
    copy(s, sizeof(s), in);
    for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p);
    size_t n = strlen(s);
    unsigned long v;
    if (n == 6 && hexnum(s, 0xFFFFFF, &v)) {
        *code = (uint32_t)v;
        return true;
    }
    unsigned long ftb = 0;
    char *dash = strchr(s, '-');
    if (dash) {
        *dash = '\0';
        if (!hexnum(dash + 1, 0xFF, &ftb)) return false;
    }
    const char *letters = "PCBU";
    const char *l = strlen(s) == 5 ? strchr(letters, s[0]) : NULL;
    if (l == NULL || s[0] == '\0' || !hexnum(s + 1, 0x3FFF, &v)) return false;
    *code = (uint32_t)(((unsigned long)(l - letters) << 14 | v) << 8 | ftb);
    return true;
}

void diagdb_format_dtc(uint32_t code, char *buf)
{
    unsigned hi = (code >> 8) & 0xFFFF;
    if (code & 0xFF) {
        sprintf(buf, "%c%04X-%02X", "PCBU"[hi >> 14], hi & 0x3FFF, (unsigned)(code & 0xFF));
    } else {
        sprintf(buf, "%c%04X", "PCBU"[hi >> 14], hi & 0x3FFF);
    }
}

static bool type_of(const char *s, diagdb_type_t *t)
{
    static const char *const NAMES[] = { "ascii", "hex", "u8", "u16", "u32", "s8", "s16", "s32" };
    for (int i = 0; i < 8; i++) {
        if (strcasecmp(s, NAMES[i]) == 0) {
            *t = (diagdb_type_t)i;
            return true;
        }
    }
    return false;
}

int diagdb_find_ecu(const diagdb_t *db, const char *name)
{
    for (int i = 0; i < db->necu; i++) {
        if (strcasecmp(db->ecus[i].name, name) == 0) return i;
    }
    return -1;
}

/* "*" -> DIAGDB_ALL, name -> index, -2 = unknown */
static int ecu_ref(const diagdb_t *db, const char *s)
{
    if (*s == '\0' || strcmp(s, "*") == 0) return DIAGDB_ALL;
    int i = diagdb_find_ecu(db, s);
    return i >= 0 ? i : -2;
}

static void bad(diagdb_t *db, int lineno, const char *what)
{
    if (db->skipped++ == 0) {
        snprintf(db->error, sizeof(db->error), "line %d: %.60s", lineno, what);
    }
}

bool diagdb_parse(diagdb_t *db, FILE *f)
{
    db->necu = db->ndid = db->ndtc = 0;
    db->skipped = 0;
    db->error[0] = '\0';
    char *line = malloc(LINE_LEN);
    if (line == NULL) return false;
    int sec = SEC_NONE, lineno = 0;
    while (fgets(line, LINE_LEN, f)) {
        lineno++;
        size_t l = strlen(line);
        if (l == LINE_LEN - 1 && line[l - 1] != '\n') {
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            bad(db, lineno, "line too long");
            continue;
        }
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *p = trim(line);
        if (*p == '\0') continue;
        if (*p == '[') {
            if (strcasecmp(p, "[ecu]") == 0) sec = SEC_ECU;
            else if (strcasecmp(p, "[did]") == 0) sec = SEC_DID;
            else if (strcasecmp(p, "[dtc]") == 0) sec = SEC_DTC;
            else { sec = SEC_NONE; bad(db, lineno, "unknown section"); }
            continue;
        }
        char *col[MAX_COLS];
        int n = 0;
        for (char *c = p; n < MAX_COLS;) {
            char *bar = strchr(c, '|');
            if (bar) *bar = '\0';
            col[n++] = trim(c);
            if (!bar) break;
            c = bar + 1;
        }
        for (int i = n; i < MAX_COLS; i++) col[i] = (char *)"";

        if (sec == SEC_ECU) {
            unsigned long tx, rx;
            if (col[0][0] == '\0' || !hexnum(col[1], 0x7FF, &tx) || !hexnum(col[2], 0x7FF, &rx)) {
                bad(db, lineno, "ECU needs name | tx | rx (hex, 11 bit)");
                continue;
            }
            if (col[3][0] && strcasecmp(col[3], "uds") != 0) {
                bad(db, lineno, "only protocol uds is supported");
                continue;
            }
            if (diagdb_find_ecu(db, col[0]) >= 0) {
                bad(db, lineno, "ECU name used twice");
                continue;
            }
            if (db->necu >= db->max_ecu) { bad(db, lineno, "too many ECUs"); continue; }
            diagdb_ecu_t *e = &db->ecus[db->necu++];
            copy(e->name, sizeof(e->name), col[0]);
            e->tx = (uint16_t)tx;
            e->rx = (uint16_t)rx;
        } else if (sec == SEC_DID) {
            unsigned long did;
            diagdb_type_t t;
            double scale, offset;
            int ecu = ecu_ref(db, col[0]);
            if (ecu == -2) { bad(db, lineno, "DID for an unknown ECU"); continue; }
            if (!hexnum(col[1], 0xFFFF, &did) || !type_of(col[3], &t) ||
                !number(col[4], 1.0, &scale) || !number(col[5], 0.0, &offset)) {
                bad(db, lineno, "DID needs ecu | did (hex) | name | type [| scale | offset | unit]");
                continue;
            }
            if (db->ndid >= db->max_did) { bad(db, lineno, "too many DIDs"); continue; }
            diagdb_did_t *d = &db->dids[db->ndid++];
            d->ecu = (int16_t)ecu;
            d->did = (uint16_t)did;
            copy(d->name, sizeof(d->name), col[2]);
            d->type = t;
            d->scale = scale;
            d->offset = offset;
            copy(d->unit, sizeof(d->unit), col[6]);
        } else if (sec == SEC_DTC) {
            uint32_t code;
            int ecu = ecu_ref(db, col[0]);
            if (ecu == -2) { bad(db, lineno, "DTC for an unknown ECU"); continue; }
            if (!diagdb_parse_dtc(col[1], &code)) { bad(db, lineno, "bad DTC code"); continue; }
            if (db->ndtc >= db->max_dtc) { bad(db, lineno, "too many DTC texts"); continue; }
            diagdb_dtc_t *d = &db->dtcs[db->ndtc++];
            d->ecu = (int16_t)ecu;
            d->code = code;
            copy(d->text, sizeof(d->text), col[2]);
        } else {
            bad(db, lineno, "entry before the first [section]");
        }
    }
    free(line);
    return !ferror(f) && db->necu > 0;
}

const diagdb_did_t *diagdb_find_did(const diagdb_t *db, int ecu, uint16_t did)
{
    const diagdb_did_t *any = NULL;
    for (int i = 0; i < db->ndid; i++) {
        const diagdb_did_t *d = &db->dids[i];
        if (d->did != did) continue;
        if (d->ecu == ecu) return d;
        if (d->ecu == DIAGDB_ALL && any == NULL) any = d;
    }
    return any;
}

const char *diagdb_dtc_text(const diagdb_t *db, int ecu, uint32_t code)
{
    const char *any = NULL, *generic = NULL;
    for (int i = 0; i < db->ndtc; i++) {
        const diagdb_dtc_t *d = &db->dtcs[i];
        bool mine = d->ecu == ecu, all = d->ecu == DIAGDB_ALL;
        if (!mine && !all) continue;
        if (d->code == code) {
            if (mine) return d->text;
            if (any == NULL) any = d->text;
        } else if (generic == NULL && (d->code >> 8) == (code >> 8) && (d->code & 0xFF) == 0) {
            generic = d->text;          /* same SAE code, generic failure type */
        }
    }
    return any ? any : generic;
}

void diagdb_decode(const diagdb_did_t *d, const uint8_t *data, int len, char *out, int out_len)
{
    int o = 0;
    out[0] = '\0';
    if (d->type == DIAGDB_ASCII) {
        while (len > 0 && (data[len - 1] == ' ' || data[len - 1] == 0 || data[len - 1] == 0xFF)) len--;
        for (int i = 0; i < len && o < out_len - 1; i++) {
            out[o++] = isprint(data[i]) ? (char)data[i] : '.';
        }
        out[o] = '\0';
        return;
    }
    if (d->type == DIAGDB_HEX) {
        for (int i = 0; i < len && o + 4 < out_len; i++) {
            o += snprintf(out + o, out_len - o, "%s%02X", i ? " " : "", data[i]);
        }
        return;
    }
    static const int SIZE[] = { 0, 0, 1, 2, 4, 1, 2, 4 };
    int n = SIZE[d->type];
    if (len < n) {
        snprintf(out, out_len, "(too short: %d bytes)", len);
        return;
    }
    uint32_t raw = 0;
    for (int i = 0; i < n; i++) raw = raw << 8 | data[i];
    double v;
    if (d->type >= DIAGDB_S8) {
        int32_t sv = (int32_t)(raw << (32 - 8 * n)) >> (32 - 8 * n);       /* sign extend */
        v = sv;
    } else {
        v = raw;
    }
    v = v * d->scale + d->offset;
    snprintf(out, out_len, d->unit[0] ? "%g %s" : "%g", v, d->unit);
}
