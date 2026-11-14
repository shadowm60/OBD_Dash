/*
 * Diag page (roadmap C1): ECU scan with a diagnostic database from the SD
 * card (/db/diag/<make>.txt, docs/diag_db.md).
 *   diag_files / diag_file_sel   database dropdown (setting diag_db)
 *   diag_scan_text, diag_status, diag_ecus
 * Scan = Tester Present (3E 00) to every ECU of the database, one after the
 * other through comsm (UDS, the chosen car interface), only while OBD is not
 * connected. A negative answer still means the ECU is there.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "bridge.h"
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

static uivar_t v_files = UIVAR("diag_files");
static uivar_t v_file_sel = UIVAR("diag_file_sel");
static uivar_t v_scan = UIVAR("diag_scan_text");
static uivar_t v_status = UIVAR("diag_status");
static uivar_t v_ecus = UIVAR("diag_ecus");

typedef enum { ECU_UNKNOWN, ECU_ASKING, ECU_YES, ECU_NO, ECU_ERROR } ecu_state_t;

static diagdb_t     s_db;
static ecu_state_t *s_ecu_st;
static uint8_t     *s_ecu_nrc;
static char         s_file[48];             /* loaded database file */
static bool         s_entered;
static int          s_sel = -1;
EXT_RAM_BSS_ATTR static char s_files[MAX_FILES * 48];
static int          s_nfiles;
static int          s_scan = -1;            /* ECU being asked, -1 = no scan */
static uint32_t     s_uds_seq;
static char         s_msg[128];
EXT_RAM_BSS_ATTR static char s_text[MAX_ECU * 64 + 64];

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
    s_db.max_ecu = MAX_ECU;
    s_db.max_did = MAX_DID;
    s_db.max_dtc = MAX_DTC;
    return s_db.ecus && s_db.dids && s_db.dtcs && s_ecu_st && s_ecu_nrc;
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

static void load(const char *name)
{
    s_scan = -1;
    s_db.necu = s_db.ndid = s_db.ndtc = 0;
    strlcpy(s_file, name, sizeof(s_file));
    char path[96];
    snprintf(path, sizeof(path), DIAG_DIR "/%s", name);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        snprintf(s_msg, sizeof(s_msg), "cannot open %s", name);
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
    if (s_nfiles && file_at(sel, name, sizeof(name)) && strcmp(name, s_file) != 0) {
        load(name);
    }
}

void view_diag_scan(void)
{
    if (s_scan >= 0) {
        s_scan = -1;                            /* Stop */
        return;
    }
    comsm_status_t cs;
    comsm_get_status(&cs);
    if (cs.state != COMSM_IDLE) {
        view_show_message("Diag", "Disconnect OBD first (OBD page): diagnostics use the same link.");
        return;
    }
    if (s_db.necu == 0) {
        view_show_message("Diag", "No database loaded: upload one (e.g. generic.txt) on the web page into db/diag/.");
        return;
    }
    memset(s_ecu_st, 0, MAX_ECU * sizeof(ecu_state_t));
    s_scan = 0;
    s_uds_seq = UINT32_MAX;                     /* first request not sent yet */
}

/* One ECU after the other: send, wait for the result, next. */
static void scan_step(void)
{
    if (s_scan < 0) {
        return;
    }
    if (s_scan >= s_db.necu) {
        s_scan = -1;
        return;
    }
    comsm_uds_result_t r;
    comsm_uds_get(&r);
    if (s_ecu_st[s_scan] != ECU_ASKING) {
        static const uint8_t TP[2] = { 0x3E, 0x00 };
        if (comsm_uds_request(s_db.ecus[s_scan].tx, s_db.ecus[s_scan].rx, TP, sizeof(TP)) == ESP_OK) {
            s_ecu_st[s_scan] = ECU_ASKING;
            s_uds_seq = r.seq;
        } else {
            comsm_status_t cs;
            comsm_get_status(&cs);
            if (cs.state != COMSM_IDLE) {
                s_scan = -1;                    /* OBD connected meanwhile */
            }
        }
        return;
    }
    if (r.seq == s_uds_seq || r.state == COMSM_UDS_BUSY) {
        return;
    }
    s_ecu_st[s_scan] = r.state == COMSM_UDS_DONE || r.state == COMSM_UDS_NEGATIVE ? ECU_YES
                     : r.state == COMSM_UDS_TIMEOUT ? ECU_NO : ECU_ERROR;
    s_ecu_nrc[s_scan] = r.state == COMSM_UDS_NEGATIVE ? r.nrc : 0;
    if (r.state == COMSM_UDS_ERROR) {
        snprintf(s_msg, sizeof(s_msg), "link: %s", r.error[0] ? r.error : "error");
    }
    s_scan++;
}

void view_diag_update(bool visible)
{
    if (!visible) {
        s_entered = false;
        s_scan = -1;
        return;
    }
    if (!s_entered) {
        s_entered = true;
        enter();
    }
    int sel = uv_get_int(&v_file_sel, s_sel);
    if (sel != s_sel && sel >= 0 && sel < s_nfiles) {
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
    scan_step();

    uv_str(&v_scan, s_scan >= 0 ? "Stop" : "Scan ECUs");
    int yes = 0, asked = 0;
    for (int i = 0; i < s_db.necu; i++) {
        yes += s_ecu_st[i] == ECU_YES;
        asked += s_ecu_st[i] == ECU_YES || s_ecu_st[i] == ECU_NO || s_ecu_st[i] == ECU_ERROR;
    }
    if (s_db.necu == 0) {
        uv_strf(&v_status, "%s", s_msg[0] ? s_msg : "no database loaded");
    } else {
        uv_strf(&v_status, "%s: %u ECUs, %u DIDs, %u DTC texts. %s%d of %d answered.%s%s",
                s_file, s_db.necu, s_db.ndid, s_db.ndtc, s_scan >= 0 ? "Scanning... " : "", yes, asked,
                s_msg[0] ? "  " : "", s_msg);
    }
    size_t o = 0;
    s_text[0] = '\0';
    for (int i = 0; i < s_db.necu && o + 80 < sizeof(s_text); i++) {
        const diagdb_ecu_t *e = &s_db.ecus[i];
        const char *st = s_ecu_st[i] == ECU_ASKING ? "asking..." : s_ecu_st[i] == ECU_YES ? "answers"
                       : s_ecu_st[i] == ECU_NO ? "no answer" : s_ecu_st[i] == ECU_ERROR ? "link error" : "-";
        o += snprintf(s_text + o, sizeof(s_text) - o, "%s%-20s  %03X / %03X   %s", i ? "\n" : "", e->name, e->tx, e->rx, st);
        if (s_ecu_st[i] == ECU_YES && s_ecu_nrc[i]) {
            o += snprintf(s_text + o, sizeof(s_text) - o, " (negative answer %02X)", s_ecu_nrc[i]);
        }
    }
    uv_str(&v_ecus, s_db.necu ? s_text : "Upload a database file (docs/diag_db.md) on the web page; "
                                         "sd/db/diag/generic.txt in the project is a starting point.");
}
