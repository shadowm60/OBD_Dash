/*
 * Diag pages (roadmap C1 / C2), with a diagnostic database from the SD card
 * (/db/diag/<make>.txt, docs/diag_db.md).
 *
 *   diag       database dropdown (diag_files / diag_file_sel, setting diag_db),
 *              Scan ECUs (diag_scan_text), diag_status, ECU rows built here in
 *              objects.diag_ecu_list (tap = ECU page).
 *   diag_ecu   one ECU: Identification (every database DID F180..F1FF of this
 *              ECU), Read DTCs (19 02 FF, texts from the database, status
 *              decoded), Clear DTCs (14 FF FF FF after a confirmation, then read
 *              again), Save to SD (dtc_<ECU>_<date>.txt).
 *              diag_ecu_title, diag_ecu_text.
 *   diag_did   read one DID (C3): database DIDs of the ECU (diag_did_list /
 *              diag_did_sel, entry 0 = the hex field diag_did_hex), Read, Live
 *              (every second); diag_did_result (decoded), diag_did_raw (bytes).
 *
 * Requests go one after the other through comsm (UDS over the chosen car
 * interface), only while OBD is not connected. Scan = Tester Present (3E 00);
 * a negative answer still means the ECU is there.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "bridge.h"
#include "screens.h"
#include "settings.h"
#include "sd_card.h"
#include "comsm.h"
#include "diagdb.h"

static const char *TAG = "view_diag";

#define DIAG_DIR    MOUNT_POINT "/db/diag"
#define MAX_ECU     64
#define MAX_DID     1024
#define MAX_DTC     4096
#define MAX_FILES   16
#define MAX_INFO    32              /* identification DIDs per ECU */
#define INFO_LEN    96

static uivar_t v_files = UIVAR("diag_files");
static uivar_t v_file_sel = UIVAR("diag_file_sel");
static uivar_t v_scan = UIVAR("diag_scan_text");
static uivar_t v_status = UIVAR("diag_status");
static uivar_t v_title = UIVAR("diag_ecu_title");
static uivar_t v_text = UIVAR("diag_ecu_text");
static uivar_t v_did_title = UIVAR("diag_did_title");
static uivar_t v_did_list = UIVAR("diag_did_list");
static uivar_t v_did_sel = UIVAR("diag_did_sel");
static uivar_t v_did_hex = UIVAR("diag_did_hex");
static uivar_t v_did_live = UIVAR("diag_did_live_text");
static uivar_t v_did_result = UIVAR("diag_did_result");
static uivar_t v_did_raw = UIVAR("diag_did_raw");

typedef enum { ECU_UNKNOWN, ECU_ASKING, ECU_YES, ECU_NO, ECU_ERROR } ecu_state_t;

static diagdb_t     s_db;
static ecu_state_t *s_ecu_st;
static uint8_t     *s_ecu_nrc;
static lv_obj_t   **s_row_lbl;              /* label of each ECU row */
static char         s_file[48];
static bool         s_entered, s_ecu_entered;
static int          s_sel = -1;
EXT_RAM_BSS_ATTR static char s_files[MAX_FILES * 48];
static int          s_nfiles;
static char         s_msg[128];

/* ---- one job at a time: scan, identification, DTCs, clear */
typedef enum { JOB_NONE, JOB_SCAN, JOB_INFO, JOB_DTC, JOB_CLEAR, JOB_DID } job_t;
static job_t        s_job;
static int          s_step;                 /* scan: ECU index; info: DID index */
static bool         s_sent;
static uint32_t     s_seq;
static int          s_ecu = -1;             /* ECU of the ECU page */

/* ECU page results */
static uint16_t     s_info_did[MAX_INFO];
static int          s_ninfo;
EXT_RAM_BSS_ATTR static char s_info[MAX_INFO][INFO_LEN];
EXT_RAM_BSS_ATTR static char s_dtc_text[6144];
static char         s_info_when[32], s_dtc_when[32];
static bool         s_have_info, s_have_dtc;
static char         s_job_msg[96];
EXT_RAM_BSS_ATTR static char s_page[MAX_INFO * INFO_LEN + 6144 + 512];

/* read-data page */
#define MAX_DID_LIST 128
static uint16_t     s_did_ids[MAX_DID_LIST];
static int          s_ndid_list;
EXT_RAM_BSS_ATTR static char s_did_opts[MAX_DID_LIST * 72 + 32];
static bool         s_did_entered;
static uint16_t     s_did;                  /* DID being read */
static bool         s_live;
static int64_t      s_did_next_us;
static uint32_t     s_did_count;
EXT_RAM_BSS_ATTR static char s_did_res[160];
EXT_RAM_BSS_ATTR static char s_did_raw[COMSM_UDS_MAX * 3 + 160];

/* ---------------------------------------------------------------- database */

static bool alloc_tables(void)
{
    if (s_db.ecus) {
        return true;
    }
    s_db.ecus = heap_caps_calloc(MAX_ECU, sizeof(diagdb_ecu_t), MALLOC_CAP_SPIRAM);
    s_db.dids = heap_caps_calloc(MAX_DID, sizeof(diagdb_did_t), MALLOC_CAP_SPIRAM);
    s_db.dtcs = heap_caps_calloc(MAX_DTC, sizeof(diagdb_dtc_t), MALLOC_CAP_SPIRAM);
    s_ecu_st = heap_caps_calloc(MAX_ECU, sizeof(ecu_state_t), MALLOC_CAP_SPIRAM);
    s_ecu_nrc = heap_caps_calloc(MAX_ECU, 1, MALLOC_CAP_SPIRAM);
    s_row_lbl = heap_caps_calloc(MAX_ECU, sizeof(lv_obj_t *), MALLOC_CAP_SPIRAM);
    s_db.max_ecu = MAX_ECU;
    s_db.max_did = MAX_DID;
    s_db.max_dtc = MAX_DTC;
    return s_db.ecus && s_db.dids && s_db.dtcs && s_ecu_st && s_ecu_nrc && s_row_lbl;
}

static int list_files(void)
{
    char names[MAX_FILES][48];
    int n = 0;
    DIR *d = opendir(DIAG_DIR);
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL && n < MAX_FILES) {
        size_t l = strlen(e->d_name);
        if (e->d_type == DT_DIR || l < 5 || l >= sizeof(names[0]) || strcasecmp(e->d_name + l - 4, ".txt") != 0) {
            continue;
        }
        int i = n++;
        while (i > 0 && strcasecmp(names[i - 1], e->d_name) > 0) {
            strcpy(names[i], names[i - 1]);
            i--;
        }
        strcpy(names[i], e->d_name);
    }
    if (d) {
        closedir(d);
    }
    size_t o = 0;
    s_files[0] = '\0';
    for (int i = 0; i < n; i++) {
        o += snprintf(s_files + o, sizeof(s_files) - o, "%s%s", i ? "\n" : "", names[i]);
    }
    return n;
}

static bool file_at(int idx, char *out, size_t len)
{
    const char *p = s_files;
    for (int i = 0; i < idx && p; i++) {
        p = strchr(p, '\n');
        p = p ? p + 1 : NULL;
    }
    if (p == NULL || *p == '\0') {
        return false;
    }
    const char *e = strchr(p, '\n');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n >= len) n = len - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

/* ---------------------------------------------------------------- ECU rows */

static void row_clicked_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_job == JOB_SCAN) {
        return;                         /* finish (or stop) the scan first */
    }
    s_ecu = i;
    s_ecu_entered = false;
    nav_go("diag_ecu");
}

static void build_rows(void)
{
    lv_obj_t *list = objects.diag_ecu_list;
    if (list == NULL) {
        return;
    }
    lv_obj_clean(list);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, LV_PART_MAIN);
    for (int i = 0; i < s_db.necu; i++) {
        lv_obj_t *b = lv_btn_create(list);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_height(b, 44);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x22303f), LV_PART_MAIN);
        lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(b, row_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, 0);
        lv_label_set_text(l, "");
        s_row_lbl[i] = l;
    }
    if (s_db.necu == 0) {
        lv_obj_t *l = lv_label_create(list);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_label_set_text(l, "Upload a database file (docs/diag_db.md) on the web page into db/diag/; "
                             "sd/db/diag/generic.txt in the project is a starting point.");
    }
}

static void update_rows(void)
{
    for (int i = 0; i < s_db.necu; i++) {
        if (s_row_lbl[i] == NULL) {
            continue;
        }
        const diagdb_ecu_t *e = &s_db.ecus[i];
        const char *st = s_ecu_st[i] == ECU_ASKING ? "asking..." : s_ecu_st[i] == ECU_YES ? "answers"
                       : s_ecu_st[i] == ECU_NO ? "no answer" : s_ecu_st[i] == ECU_ERROR ? "link error" : "";
        char t[112];
        if (s_ecu_st[i] == ECU_YES && s_ecu_nrc[i]) {
            snprintf(t, sizeof(t), "%-20s  %03X / %03X   %s (negative answer %02X)", e->name, e->tx, e->rx, st, s_ecu_nrc[i]);
        } else {
            snprintf(t, sizeof(t), "%-20s  %03X / %03X   %s", e->name, e->tx, e->rx, st);
        }
        if (strcmp(lv_label_get_text(s_row_lbl[i]), t) != 0) {
            lv_label_set_text(s_row_lbl[i], t);
        }
    }
}

static void load(const char *name)
{
    s_job = JOB_NONE;
    s_db.necu = s_db.ndid = s_db.ndtc = 0;
    memset(s_row_lbl, 0, MAX_ECU * sizeof(lv_obj_t *));
    strlcpy(s_file, name, sizeof(s_file));
    char path[96];
    snprintf(path, sizeof(path), DIAG_DIR "/%s", name);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        snprintf(s_msg, sizeof(s_msg), "cannot open %.40s", name);
        build_rows();
        return;
    }
    bool ok = diagdb_parse(&s_db, f);
    fclose(f);
    memset(s_ecu_st, 0, MAX_ECU * sizeof(ecu_state_t));
    if (!ok) {
        snprintf(s_msg, sizeof(s_msg), "%.24s: no ECUs%s%.60s", name, s_db.error[0] ? " - " : "", s_db.error);
    } else if (s_db.skipped) {
        snprintf(s_msg, sizeof(s_msg), "%lu lines skipped, first: %.60s", (unsigned long)s_db.skipped, s_db.error);
    } else {
        s_msg[0] = '\0';
    }
    build_rows();
    ESP_LOGI(TAG, "%s: %u ECUs, %u DIDs, %u DTC texts, %lu lines skipped", name, s_db.necu, s_db.ndid,
             s_db.ndtc, (unsigned long)s_db.skipped);
}

static void enter(void)
{
    if (!alloc_tables()) {
        snprintf(s_msg, sizeof(s_msg), "out of memory");
        return;
    }
    s_nfiles = waveshare_sd_card_mounted() ? list_files() : 0;
    uv_str(&v_files, s_nfiles ? s_files : "(no .txt files in db/diag/)");
    app_settings_t c;
    settings_get(&c);
    int sel = 0;
    char name[48];
    for (int i = 0; i < s_nfiles && file_at(i, name, sizeof(name)); i++) {
        if (strcmp(name, c.diag_db) == 0) {
            sel = i;
        }
    }
    s_sel = sel;
    uv_int(&v_file_sel, sel);
    if (s_nfiles && file_at(sel, name, sizeof(name))) {
        if (strcmp(name, s_file) != 0) {
            load(name);
        }
    } else if (s_file[0] == '\0') {
        build_rows();
    }
}

/* ---------------------------------------------------------------- jobs */

static bool obd_idle(const char *what)
{
    comsm_status_t cs;
    comsm_get_status(&cs);
    if (cs.state != COMSM_IDLE) {
        char t[112];
        snprintf(t, sizeof(t), "Disconnect OBD first (OBD page): %s uses the same link.", what);
        view_show_message("Diag", t);
        return false;
    }
    return true;
}

static void start_job(job_t j)
{
    s_job = j;
    s_step = 0;
    s_sent = false;
}

void view_diag_scan(void)
{
    if (s_job == JOB_SCAN) {
        s_job = JOB_NONE;                       /* Stop */
        return;
    }
    if (s_job != JOB_NONE || !obd_idle("the scan")) {
        return;
    }
    if (s_db.necu == 0) {
        view_show_message("Diag", "No database loaded: upload one (e.g. generic.txt) on the web page into db/diag/.");
        return;
    }
    memset(s_ecu_st, 0, MAX_ECU * sizeof(ecu_state_t));
    start_job(JOB_SCAN);
}

static void stamp(char *out, size_t len)
{
    time_t t = time(NULL);
    if (t > 1700000000) {
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(out, len, "%Y-%m-%d %H:%M:%S", &tm);
    } else {
        snprintf(out, len, "clock not set");
    }
}

void view_diag_read_info(void)
{
    if (s_job != JOB_NONE || s_ecu < 0 || !obd_idle("diagnostics")) {
        return;
    }
    /* identification = the database's DIDs F180..F1FF for this ECU (own and '*') */
    s_ninfo = 0;
    for (int i = 0; i < s_db.ndid && s_ninfo < MAX_INFO; i++) {
        const diagdb_did_t *d = &s_db.dids[i];
        if (d->did < 0xF180 || d->did > 0xF1FF || (d->ecu != s_ecu && d->ecu != DIAGDB_ALL)) {
            continue;
        }
        bool dup = false;
        for (int k = 0; k < s_ninfo; k++) {
            dup |= s_info_did[k] == d->did;
        }
        if (!dup) {
            s_info_did[s_ninfo] = d->did;
            snprintf(s_info[s_ninfo], INFO_LEN, "%.60s: ...", diagdb_find_did(&s_db, s_ecu, d->did)->name);
            s_ninfo++;
        }
    }
    if (s_ninfo == 0) {
        view_show_message("Identification", "The database has no identification DIDs (F180..F1FF) for this ECU.");
        return;
    }
    s_have_info = true;
    stamp(s_info_when, sizeof(s_info_when));
    s_job_msg[0] = '\0';
    start_job(JOB_INFO);
}

void view_diag_read_dtc(void)
{
    if (s_job != JOB_NONE || s_ecu < 0 || !obd_idle("diagnostics")) {
        return;
    }
    strcpy(s_dtc_text, "reading...");
    s_have_dtc = true;
    s_job_msg[0] = '\0';
    start_job(JOB_DTC);
}

static void clear_confirm_cb(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_current_target(e);
    const char *btn = lv_msgbox_get_active_btn_text(mbox);
    if (btn && strcmp(btn, "Clear") == 0 && s_job == JOB_NONE && s_ecu >= 0 && obd_idle("diagnostics")) {
        ESP_LOGI(TAG, "user confirmed DTC clear on %s", s_db.ecus[s_ecu].name);
        strcpy(s_dtc_text, "clearing...");
        s_have_dtc = true;
        start_job(JOB_CLEAR);
    }
    lv_msgbox_close_async(mbox);
}

void view_diag_clear_dtc(void)
{
    if (s_job != JOB_NONE || s_ecu < 0) {
        return;
    }
    static const char *btns[] = { "Clear", "Cancel", "" };
    char t[160];
    snprintf(t, sizeof(t), "Erase the trouble codes and their freeze frames in %s?\n\n"
             "Use with ignition on and engine off.", s_db.ecus[s_ecu].name);
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Clear trouble codes?", t, btns, false);
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_width(mbox, 520);
    lv_obj_add_event_cb(mbox, clear_confirm_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

static const char *nrc_text(uint8_t nrc)
{
    switch (nrc) {
    case 0x10: return "general reject";
    case 0x11: return "service not supported";
    case 0x12: return "sub-function not supported";
    case 0x13: return "wrong length";
    case 0x22: return "conditions not correct";
    case 0x31: return "not supported";
    case 0x33: return "security access denied";
    case 0x7E: return "not in this session";
    case 0x7F: return "service not in this session";
    default:   return "negative answer";
    }
}

/* the status bits that matter to a reader */
static void status_text(uint8_t st, char *out, size_t len)
{
    size_t o = 0;
    out[0] = '\0';
    if (st & 0x01) o += snprintf(out + o, len - o, "%sfailed now", o ? ", " : "");
    if (st & 0x08) o += snprintf(out + o, len - o, "%sconfirmed", o ? ", " : "");
    if (st & 0x04) o += snprintf(out + o, len - o, "%spending", o ? ", " : "");
    if (st & 0x80) o += snprintf(out + o, len - o, "%sMIL on", o ? ", " : "");
    if (o == 0) {
        snprintf(out, len, "stored");
    }
}

static void dtc_result(const comsm_uds_result_t *r)
{
    size_t o = 0;
    stamp(s_dtc_when, sizeof(s_dtc_when));
    if (r->state == COMSM_UDS_NEGATIVE) {
        snprintf(s_dtc_text, sizeof(s_dtc_text), "%s (NRC %02X)", nrc_text(r->nrc), r->nrc);
        return;
    }
    if (r->state != COMSM_UDS_DONE || r->len < 3 || r->data[0] != 0x59) {
        if (r->state == COMSM_UDS_TIMEOUT) {
            snprintf(s_dtc_text, sizeof(s_dtc_text), "no answer");
        } else {
            snprintf(s_dtc_text, sizeof(s_dtc_text), "error %s", r->error);
        }
        return;
    }
    int n = (r->len - 3) / 4;
    if (n == 0) {
        snprintf(s_dtc_text, sizeof(s_dtc_text), "no trouble codes");
        return;
    }
    o += snprintf(s_dtc_text + o, sizeof(s_dtc_text) - o, "%d trouble code%s:", n, n == 1 ? "" : "s");
    for (int i = 0; i < n && o + 180 < sizeof(s_dtc_text); i++) {
        const uint8_t *p = r->data + 3 + 4 * i;
        uint32_t code = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        char c[12], st[64];
        diagdb_format_dtc(code, c);
        status_text(p[3], st, sizeof(st));
        const char *t = diagdb_dtc_text(&s_db, s_ecu, code);
        o += snprintf(s_dtc_text + o, sizeof(s_dtc_text) - o, "\n  %s  %.64s\n      status %02X: %s",
                      c, t ? t : "(no text in the database)", p[3], st);
    }
}

static void did_result(const comsm_uds_result_t *r)
{
    char when[32];
    stamp(when, sizeof(when));
    s_did_count++;
    const diagdb_did_t *d = diagdb_find_did(&s_db, s_ecu, s_did);
    if (r->state == COMSM_UDS_DONE && r->len >= 3 && r->data[0] == 0x62 && (r->data[1] << 8 | r->data[2]) == s_did) {
        const uint8_t *p = r->data + 3;
        int n = r->len - 3;
        char v[96];
        if (d) {
            diagdb_decode(d, p, n, v, sizeof(v));
            snprintf(s_did_res, sizeof(s_did_res), "%04X  %.48s:  %s", s_did, d->name, v);
        } else {
            snprintf(s_did_res, sizeof(s_did_res), "%04X  (not in the database): %d bytes", s_did, n);
        }
        size_t o = snprintf(s_did_raw, sizeof(s_did_raw), "raw (%d bytes):", n);
        for (int i = 0; i < n && o + 4 < sizeof(s_did_raw); i++) {
            o += snprintf(s_did_raw + o, sizeof(s_did_raw) - o, " %02X", p[i]);
        }
        bool text = n > 0;
        for (int i = 0; i < n; i++) {
            text &= (p[i] >= 0x20 && p[i] < 0x7F) || p[i] == 0 || p[i] == 0xFF;
        }
        if (text && o + n + 16 < sizeof(s_did_raw)) {
            o += snprintf(s_did_raw + o, sizeof(s_did_raw) - o, "\ntext: \"");
            for (int i = 0; i < n; i++) {
                s_did_raw[o++] = (p[i] >= 0x20 && p[i] < 0x7F) ? (char)p[i] : '.';
            }
            o += snprintf(s_did_raw + o, sizeof(s_did_raw) - o, "\"");
        }
        snprintf(s_did_raw + o, sizeof(s_did_raw) - o, "\n%s, read %lu time%s", when,
                 (unsigned long)s_did_count, s_did_count == 1 ? "" : "s");
    } else {
        if (r->state == COMSM_UDS_NEGATIVE) {
            snprintf(s_did_res, sizeof(s_did_res), "%04X: %s (NRC %02X)", s_did, nrc_text(r->nrc), r->nrc);
        } else if (r->state == COMSM_UDS_TIMEOUT) {
            snprintf(s_did_res, sizeof(s_did_res), "%04X: no answer", s_did);
        } else {
            snprintf(s_did_res, sizeof(s_did_res), "%04X: error %s", s_did, r->error);
        }
        snprintf(s_did_raw, sizeof(s_did_raw), "%s, read %lu time%s", when, (unsigned long)s_did_count,
                 s_did_count == 1 ? "" : "s");
    }
}

/* Runs the current job one request at a time. */
static void job_step(void)
{
    if (s_job == JOB_NONE) {
        return;
    }
    if ((s_job == JOB_SCAN && s_step >= s_db.necu) || (s_job == JOB_INFO && s_step >= s_ninfo)) {
        s_job = JOB_NONE;
        return;
    }
    comsm_uds_result_t r;
    comsm_uds_get(&r);
    int ecu = s_job == JOB_SCAN ? s_step : s_ecu;
    if (!s_sent) {
        uint8_t req[7];
        uint8_t len = 0;
        switch (s_job) {
        case JOB_SCAN:  req[0] = 0x3E; req[1] = 0x00; len = 2; break;
        case JOB_INFO:  req[0] = 0x22; req[1] = s_info_did[s_step] >> 8; req[2] = s_info_did[s_step] & 0xFF; len = 3; break;
        case JOB_DTC:   req[0] = 0x19; req[1] = 0x02; req[2] = 0xFF; len = 3; break;
        case JOB_CLEAR: req[0] = 0x14; req[1] = 0xFF; req[2] = 0xFF; req[3] = 0xFF; len = 4; break;
        case JOB_DID:   req[0] = 0x22; req[1] = s_did >> 8; req[2] = s_did & 0xFF; len = 3; break;
        default: return;
        }
        if (comsm_uds_request(s_db.ecus[ecu].tx, s_db.ecus[ecu].rx, req, len) == ESP_OK) {
            s_sent = true;
            s_seq = r.seq;
            if (s_job == JOB_SCAN) {
                s_ecu_st[ecu] = ECU_ASKING;
            }
        } else {
            comsm_status_t cs;
            comsm_get_status(&cs);
            if (cs.state != COMSM_IDLE) {
                s_job = JOB_NONE;               /* OBD connected meanwhile */
                snprintf(s_job_msg, sizeof(s_job_msg), "stopped: OBD connected");
            }
        }
        return;
    }
    if (r.seq == s_seq || r.state == COMSM_UDS_BUSY) {
        return;                                 /* still waiting */
    }
    s_sent = false;
    if (r.state == COMSM_UDS_ERROR) {
        snprintf(s_job_msg, sizeof(s_job_msg), "link: %s", r.error[0] ? r.error : "error");
    }
    switch (s_job) {
    case JOB_SCAN:
        s_ecu_st[ecu] = r.state == COMSM_UDS_DONE || r.state == COMSM_UDS_NEGATIVE ? ECU_YES
                      : r.state == COMSM_UDS_TIMEOUT ? ECU_NO : ECU_ERROR;
        s_ecu_nrc[ecu] = r.state == COMSM_UDS_NEGATIVE ? r.nrc : 0;
        s_step++;
        break;
    case JOB_INFO: {
        const diagdb_did_t *d = diagdb_find_did(&s_db, s_ecu, s_info_did[s_step]);
        char v[72];
        if (r.state == COMSM_UDS_DONE && r.len >= 3 && r.data[0] == 0x62 &&
            (r.data[1] << 8 | r.data[2]) == s_info_did[s_step] && d) {
            diagdb_decode(d, r.data + 3, r.len - 3, v, sizeof(v));
        } else if (r.state == COMSM_UDS_NEGATIVE) {
            snprintf(v, sizeof(v), "(%s)", nrc_text(r.nrc));
        } else {
            snprintf(v, sizeof(v), r.state == COMSM_UDS_TIMEOUT ? "(no answer)" : "(error)");
        }
        snprintf(s_info[s_step], INFO_LEN, "%.40s: %.50s", d ? d->name : "?", v);
        s_step++;
        break;
    }
    case JOB_DTC:
        dtc_result(&r);
        s_job = JOB_NONE;
        break;
    case JOB_DID:
        did_result(&r);
        s_job = JOB_NONE;
        break;
    case JOB_CLEAR:
        if (r.state == COMSM_UDS_DONE) {
            ESP_LOGI(TAG, "DTCs cleared in %s", s_db.ecus[s_ecu].name);
            strcpy(s_dtc_text, "cleared, reading again...");
            start_job(JOB_DTC);
        } else {
            snprintf(s_dtc_text, sizeof(s_dtc_text), "clear failed: %s",
                     r.state == COMSM_UDS_NEGATIVE ? nrc_text(r.nrc) : r.state == COMSM_UDS_TIMEOUT ? "no answer" : "error");
            s_job = JOB_NONE;
        }
        break;
    default:
        s_job = JOB_NONE;
        break;
    }
}

/* ---------------------------------------------------------------- ECU page */

static void build_page(void)
{
    size_t o = 0;
    o += snprintf(s_page + o, sizeof(s_page) - o, "IDENTIFICATION");
    if (s_have_info) {
        o += snprintf(s_page + o, sizeof(s_page) - o, "  (%s)", s_info_when);
        for (int i = 0; i < s_ninfo; i++) {
            o += snprintf(s_page + o, sizeof(s_page) - o, "\n  %s", s_info[i]);
        }
    } else {
        o += snprintf(s_page + o, sizeof(s_page) - o, "\n  press Identification");
    }
    o += snprintf(s_page + o, sizeof(s_page) - o, "\n\nTROUBLE CODES");
    if (s_have_dtc) {
        o += snprintf(s_page + o, sizeof(s_page) - o, "  (%s)\n  %s", s_dtc_when, s_dtc_text);
    } else {
        o += snprintf(s_page + o, sizeof(s_page) - o, "\n  press Read DTCs");
    }
    if (s_job_msg[0]) {
        snprintf(s_page + o, sizeof(s_page) - o, "\n\n%s", s_job_msg);
    }
}

void view_diag_save(void)
{
    if (s_ecu < 0 || (!s_have_info && !s_have_dtc)) {
        view_show_message("Save", "Read the identification or the trouble codes first.");
        return;
    }
    char name[24], st[20] = "", path[96];
    strlcpy(name, s_db.ecus[s_ecu].name, sizeof(name));
    for (char *p = name; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) *p = '-';
    }
    time_t t = time(NULL);
    if (t > 1700000000) {
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(st, sizeof(st), "%Y%m%d_%H%M%S", &tm);
    } else {
        snprintf(st, sizeof(st), "%lu", (unsigned long)(esp_log_timestamp() / 1000));
    }
    snprintf(path, sizeof(path), MOUNT_POINT "/dtc_%s_%s.txt", name, st);
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        view_show_message("Save failed", "Could not write to the SD card.");
        return;
    }
    build_page();
    fprintf(f, "OBD_Dash diagnostics report\nECU: %s (request %03X, answer %03X)\nDatabase: %s\n\n%s\n",
            s_db.ecus[s_ecu].name, s_db.ecus[s_ecu].tx, s_db.ecus[s_ecu].rx, s_file, s_page);
    fclose(f);
    char m[112];
    snprintf(m, sizeof(m), "Saved to %.90s", path);
    view_show_message("Saved", m);
    ESP_LOGI(TAG, "%s", m);
}

static void update_ecu_page(void)
{
    if (s_ecu < 0 || s_ecu >= s_db.necu) {
        uv_str(&v_title, "no ECU");
        uv_str(&v_text, "");
        return;
    }
    if (!s_ecu_entered) {
        s_ecu_entered = true;
        s_have_info = s_have_dtc = false;
        s_job_msg[0] = '\0';
    }
    const diagdb_ecu_t *e = &s_db.ecus[s_ecu];
    uv_strf(&v_title, "%s  (%03X / %03X)%s", e->name, e->tx, e->rx, s_job != JOB_NONE ? "  - working..." : "");
    build_page();
    uv_str(&v_text, s_page);
}

/* ---------------------------------------------------------------- read-data page */

static void did_enter(void)
{
    s_ndid_list = 0;
    size_t o = snprintf(s_did_opts, sizeof(s_did_opts), "(DID typed in the field ->)");
    for (int i = 0; i < s_db.ndid && s_ndid_list < MAX_DID_LIST; i++) {
        const diagdb_did_t *d = &s_db.dids[i];
        if (d->ecu != s_ecu && d->ecu != DIAGDB_ALL) {
            continue;
        }
        bool dup = false;
        for (int k = 0; k < s_ndid_list; k++) {
            dup |= s_did_ids[k] == d->did;
        }
        if (dup) {
            continue;
        }
        const diagdb_did_t *best = diagdb_find_did(&s_db, s_ecu, d->did);
        s_did_ids[s_ndid_list++] = d->did;
        o += snprintf(s_did_opts + o, sizeof(s_did_opts) - o, "\n%04X  %.60s", d->did, best->name);
    }
    uv_str(&v_did_list, s_did_opts);
    uv_int(&v_did_sel, s_ndid_list ? 1 : 0);
    s_live = false;
    s_did_count = 0;
    s_did_res[0] = s_did_raw[0] = '\0';
}

/* DID chosen on the page; false (and a message) if none */
static bool chosen_did(uint16_t *did)
{
    int sel = uv_get_int(&v_did_sel, 0);
    if (sel >= 1 && sel <= s_ndid_list) {
        *did = s_did_ids[sel - 1];
        return true;
    }
    char t[8];
    uv_get_str(&v_did_hex, t, sizeof(t));
    char *end;
    unsigned long v = strtoul(t, &end, 16);
    if (t[0] == '\0' || *end != '\0' || v > 0xFFFF) {
        view_show_message("Read data", "Choose a DID from the list, or type 4 hex digits (e.g. F190) in the field.");
        return false;
    }
    *did = (uint16_t)v;
    return true;
}

static void did_start(void)
{
    uint16_t did;
    if (s_ecu < 0 || !chosen_did(&did)) {
        s_live = false;
        return;
    }
    if (did != s_did) {
        s_did_count = 0;
    }
    s_did = did;
    start_job(JOB_DID);
}

void view_diag_did_read(void)
{
    if (s_job != JOB_NONE || !obd_idle("diagnostics")) {
        return;
    }
    s_live = false;
    did_start();
}

void view_diag_did_live(void)
{
    if (s_live) {
        s_live = false;
        return;
    }
    if (!obd_idle("diagnostics")) {
        return;
    }
    s_live = true;
    s_did_next_us = 0;
}

static void update_did_page(void)
{
    if (s_ecu < 0 || s_ecu >= s_db.necu) {
        uv_str(&v_did_title, "no ECU");
        return;
    }
    if (!s_did_entered) {
        s_did_entered = true;
        did_enter();
    }
    int64_t now = esp_timer_get_time();
    if (s_live && s_job == JOB_NONE && now >= s_did_next_us) {
        s_did_next_us = now + 1000000;
        did_start();
    }
    const diagdb_ecu_t *e = &s_db.ecus[s_ecu];
    uv_strf(&v_did_title, "%s  (%03X / %03X) - read data by identifier (22)%s", e->name, e->tx, e->rx,
            s_job == JOB_DID ? "  - reading..." : "");
    uv_str(&v_did_live, s_live ? "Stop live" : "Live");
    uv_str(&v_did_result, s_did_res[0] ? s_did_res : "Choose a DID and press Read (or Live: every second).");
    uv_str(&v_did_raw, s_did_raw);
}

/* ---------------------------------------------------------------- 250 ms */

void view_diag_update(bool visible)
{
    bool ecu_page = nav_is("diag_ecu");
    bool did_page = nav_is("diag_did");
    if (!did_page) {
        s_did_entered = false;
        s_live = false;
    }
    if (!visible && !ecu_page && !did_page) {
        s_entered = false;
        s_job = JOB_NONE;
        return;
    }
    job_step();
    if (did_page) {
        update_did_page();
        return;
    }
    if (ecu_page) {
        update_ecu_page();
        return;
    }
    if (!s_entered) {
        s_entered = true;
        enter();
    }
    int sel = uv_get_int(&v_file_sel, s_sel);
    if (sel != s_sel && sel >= 0 && sel < s_nfiles && s_job == JOB_NONE) {
        s_sel = sel;
        char name[48];
        if (file_at(sel, name, sizeof(name))) {
            load(name);
            app_settings_t c;
            settings_get(&c);
            strlcpy(c.diag_db, name, sizeof(c.diag_db));
            settings_put(&c);
        }
    }
    uv_str(&v_scan, s_job == JOB_SCAN ? "Stop" : "Scan ECUs");
    int yes = 0, asked = 0;
    for (int i = 0; i < s_db.necu; i++) {
        yes += s_ecu_st[i] == ECU_YES;
        asked += s_ecu_st[i] == ECU_YES || s_ecu_st[i] == ECU_NO || s_ecu_st[i] == ECU_ERROR;
    }
    if (s_db.necu == 0) {
        uv_strf(&v_status, "%s", s_msg[0] ? s_msg : "no database loaded");
    } else {
        uv_strf(&v_status, "%s: %u ECUs, %u DIDs, %u DTC texts. %s%d of %d answered. Tap an ECU for details.%s%s",
                s_file, s_db.necu, s_db.ndid, s_db.ndtc, s_job == JOB_SCAN ? "Scanning... " : "", yes, asked,
                s_msg[0] ? "  " : "", s_msg);
    }
    update_rows();
}
