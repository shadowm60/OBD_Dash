/*
 * Dash pages (roadmap B): a CAN broadcast (rusEFI) decoded by dash_svc with
 * the DBC on the SD card.
 *
 *   dash          3 x 3 tiles: dash_t<n>_name / _value / _unit (n = 1..9),
 *                 panels objects.dash_tile<n>; dash_status. Values 10x/s
 *                 (view_dash_tick, ui_bridge's 100 ms timer), "--" when not
 *                 received for STALE_MS.
 *   dash_setup    DBC file + base ID (settings dash_dbc / dash_base, Apply),
 *                 tile assignment: dash_tile_sel + dash_sig_list / dash_sig_sel,
 *                 saved at once; dash_setup_info.
 *   dash_signals  every signal as a table (objects.dash_signal_table).
 *
 * Layout: one file per DBC, /sdcard/db/dash/<dbc file name>.txt, lines
 * "<tile> = <signal name>" ('#' comments), written when a tile is changed.
 * Without a file the tiles get common signals picked by name (DEFAULTS),
 * then the first ones left. Decoding runs while any of the pages is shown.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "bridge.h"
#include "dash_svc.h"
#include "settings.h"
#include "sd_card.h"

static const char *TAG = "view_dash";

#define DASH_TILES  9           /* tools/obd_ui.py DASH_TILES */
#define ROWS        160
#define STALE_MS    3000
#define LAYOUT_DIR  MOUNT_POINT "/db/dash"

static uivar_t v_status = UIVAR("dash_status");
static uivar_t v_signals = UIVAR("dash_signals");
static uivar_t v_files = UIVAR("dash_files");
static uivar_t v_file_sel = UIVAR("dash_file_sel");
static uivar_t v_base = UIVAR("dash_base_text");
static uivar_t v_info = UIVAR("dash_setup_info");
static uivar_t v_tile_sel = UIVAR("dash_tile_sel");
static uivar_t v_sig_list = UIVAR("dash_sig_list");
static uivar_t v_sig_sel = UIVAR("dash_sig_sel");

static uivar_t v_tname[DASH_TILES] = {
    UIVAR("dash_t1_name"), UIVAR("dash_t2_name"), UIVAR("dash_t3_name"), UIVAR("dash_t4_name"), UIVAR("dash_t5_name"),
    UIVAR("dash_t6_name"), UIVAR("dash_t7_name"), UIVAR("dash_t8_name"), UIVAR("dash_t9_name"),
};
static uivar_t v_tvalue[DASH_TILES] = {
    UIVAR("dash_t1_value"), UIVAR("dash_t2_value"), UIVAR("dash_t3_value"), UIVAR("dash_t4_value"), UIVAR("dash_t5_value"),
    UIVAR("dash_t6_value"), UIVAR("dash_t7_value"), UIVAR("dash_t8_value"), UIVAR("dash_t9_value"),
};
static uivar_t v_tunit[DASH_TILES] = {
    UIVAR("dash_t1_unit"), UIVAR("dash_t2_unit"), UIVAR("dash_t3_unit"), UIVAR("dash_t4_unit"), UIVAR("dash_t5_unit"),
    UIVAR("dash_t6_unit"), UIVAR("dash_t7_unit"), UIVAR("dash_t8_unit"), UIVAR("dash_t9_unit"),
};

/* Default tiles: first signal whose name contains one of the words. */
static const char *const DEFAULTS[DASH_TILES][3] = {
    { "RPM" }, { "CLT", "COOLANT" }, { "TPS", "THROTTLE" }, { "MAP" }, { "LAMBDA", "AFR" },
    { "IAT", "INTAKE" }, { "VBATT", "BATT", "VOLT" }, { "SPEED", "VSS" }, { "OIL" },
};

static bool     s_tried;
static bool     s_setup_shown;
static uint32_t s_gen = UINT32_MAX;         /* dash_svc generation the tiles are resolved for */
static int      s_tile[DASH_TILES];         /* signal index per tile, -1 = empty */
static char     s_dbc_file[48];
static int      s_tile_shown = -1, s_sig_shown = -1;    /* tile picker state */
EXT_RAM_BSS_ATTR static char s_table[ROWS * 64 + 128];
EXT_RAM_BSS_ATTR static char s_files[16 * 48];
EXT_RAM_BSS_ATTR static char s_names[8192];
static int      s_nfiles;

/* ---------------------------------------------------------------- loading */

static void load_from_settings(void)
{
    app_settings_t cfg;
    settings_get(&cfg);
    dash_svc_set_base(cfg.dash_base);
    dash_svc_load(cfg.dash_dbc);
    s_tried = true;
}

void view_dash_reload(void)
{
    load_from_settings();
}

/* ---------------------------------------------------------------- layout */

static void layout_path(char *out, size_t len)
{
    snprintf(out, len, LAYOUT_DIR "/%.40s.txt", s_dbc_file);
}

static bool contains_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        if (strncasecmp(hay, needle, n) == 0) {
            return true;
        }
    }
    return false;
}

static bool used(int sig, int upto)
{
    for (int i = 0; i < upto; i++) {
        if (s_tile[i] == sig) {
            return true;
        }
    }
    return false;
}

static void layout_defaults(void)
{
    int n = dash_svc_signal_count();
    dash_svc_value_t v;
    for (int t = 0; t < DASH_TILES; t++) {
        s_tile[t] = -1;
        for (int i = 0; i < n && s_tile[t] < 0; i++) {
            if (used(i, t) || !dash_svc_value(i, &v)) {
                continue;
            }
            for (int w = 0; w < 3 && DEFAULTS[t][w]; w++) {
                if (contains_ci(v.name, DEFAULTS[t][w])) {
                    s_tile[t] = i;
                    break;
                }
            }
        }
    }
    /* fill the rest with the first unused signals */
    for (int t = 0, i = 0; t < DASH_TILES; t++) {
        while (s_tile[t] < 0 && i < n) {
            if (!used(i, DASH_TILES)) {
                s_tile[t] = i;
            }
            i++;
        }
    }
}

/* Reads the layout file; false if there is none. */
static bool layout_read(void)
{
    char path[96];
    layout_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    for (int t = 0; t < DASH_TILES; t++) {
        s_tile[t] = -1;
    }
    char line[96];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') continue;
        int t = atoi(p);
        char *eq = strchr(p, '=');
        if (t < 1 || t > DASH_TILES || eq == NULL) continue;
        char *name = eq + 1;
        while (isspace((unsigned char)*name)) name++;
        char *e = name + strlen(name);
        while (e > name && isspace((unsigned char)e[-1])) *--e = '\0';
        s_tile[t - 1] = name[0] ? dash_svc_find(name) : -1;
        if (name[0] && s_tile[t - 1] < 0) {
            ESP_LOGW(TAG, "layout: tile %d: no signal '%s' in the DBC", t, name);
        }
    }
    fclose(f);
    return true;
}

static void layout_write(void)
{
    char path[96];
    mkdir(MOUNT_POINT "/db", 0775);
    mkdir(LAYOUT_DIR, 0775);
    layout_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        view_show_message("Dash", "Could not write the layout to the SD card.");
        return;
    }
    fprintf(f, "# OBD_Dash dash layout for %s: <tile 1..%d, row by row> = <signal name>\n",
            s_dbc_file, DASH_TILES);
    for (int t = 0; t < DASH_TILES; t++) {
        dash_svc_value_t v;
        fprintf(f, "%d = %s\n", t + 1, s_tile[t] >= 0 && dash_svc_value(s_tile[t], &v) ? v.name : "");
    }
    fclose(f);
    ESP_LOGI(TAG, "layout saved: %s", path);
}

/* After a (re)load: resolve the layout for this DBC. */
static void resolve_layout(void)
{
    dash_svc_status_t st;
    dash_svc_get_status(&st);
    if (st.generation == s_gen) {
        return;
    }
    s_gen = st.generation;
    strlcpy(s_dbc_file, st.file, sizeof(s_dbc_file));
    if (!st.loaded) {
        for (int t = 0; t < DASH_TILES; t++) {
            s_tile[t] = -1;
        }
    } else if (!layout_read()) {
        layout_defaults();
    }
    s_tile_shown = s_sig_shown = -1;            /* tile picker shows the new state */
}

/* ---------------------------------------------------------------- tiles */

void view_dash_tick(void)
{
    if (!nav_is("dash")) {
        return;
    }
    resolve_layout();
    for (int t = 0; t < DASH_TILES; t++) {
        dash_svc_value_t v;
        if (s_tile[t] < 0 || !dash_svc_value(s_tile[t], &v)) {
            uv_str(&v_tname[t], "");
            uv_str(&v_tvalue[t], "");
            uv_str(&v_tunit[t], "");
            continue;
        }
        uv_str(&v_tname[t], v.name);
        uv_str(&v_tunit[t], v.unit);
        if (!v.received || v.age_ms > STALE_MS) {
            uv_str(&v_tvalue[t], "--");
        } else {
            uv_strf(&v_tvalue[t], "%.*f", v.decimals, v.value);
        }
    }
}

/* ---------------------------------------------------------------- settings page */

/* name of entry `idx` of a '\n' separated list */
static bool list_at(const char *list, int idx, char *out, size_t len)
{
    const char *p = list;
    for (int i = 0; i < idx && p; i++) {
        p = strchr(p, '\n');
        p = p ? p + 1 : NULL;
    }
    if (p == NULL || *p == '\0') {
        return false;
    }
    const char *e = strchr(p, '\n');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n >= len) {
        n = len - 1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static void build_sig_list(void)
{
    /* entry 0 = empty tile, then the signals in DBC order (index + 1) */
    strcpy(s_names, "(empty)");
    size_t o = strlen(s_names);
    if (dash_svc_signal_count() > 0) {
        s_names[o++] = '\n';
        dash_svc_signal_names(s_names + o, sizeof(s_names) - o);
    }
    uv_str(&v_sig_list, s_names);
}

static void setup_enter(void)
{
    app_settings_t cfg;
    settings_get(&cfg);
    s_nfiles = dash_svc_list_files(s_files, sizeof(s_files));
    uv_str(&v_files, s_nfiles ? s_files : "(no .dbc files in db/dbc/)");
    dash_svc_status_t st;
    dash_svc_get_status(&st);
    int sel = 0;
    char name[48];
    for (int i = 0; i < s_nfiles && list_at(s_files, i, name, sizeof(name)); i++) {
        if (strcmp(name, st.file) == 0) {
            sel = i;
        }
    }
    uv_int(&v_file_sel, sel);
    if (cfg.dash_base) {
        uv_strf(&v_base, "%u", cfg.dash_base);
    } else {
        uv_str(&v_base, "");
    }
    s_tile_shown = s_sig_shown = -1;
}

void view_dash_apply(void)
{
    app_settings_t cfg;
    settings_get(&cfg);
    char name[48] = "";
    list_at(s_files, uv_get_int(&v_file_sel, 0), name, sizeof(name));
    char t[12];
    uv_get_str(&v_base, t, sizeof(t));
    char *end;
    unsigned long base = t[0] ? strtoul(t, &end, 0) : 0;      /* decimal or 0x... */
    if (t[0] && (*end != '\0' || base > 0x7FF)) {
        view_show_message("Base ID", "Use a CAN ID 0..2047 (0x000..0x7FF), decimal or 0x hex.");
        return;
    }
    strlcpy(cfg.dash_dbc, name, sizeof(cfg.dash_dbc));
    cfg.dash_base = (uint16_t)base;
    settings_put(&cfg);
    load_from_settings();
    ESP_LOGI(TAG, "dash: %s, base %lu", name[0] ? name : "(first DBC)", base);
}

/* Tile picker: choosing a tile shows its signal; choosing a signal assigns it. */
static void update_tile_picker(void)
{
    if (s_tile_shown < 0) {                     /* first time / after a load */
        build_sig_list();
        s_tile_shown = uv_get_int(&v_tile_sel, 0);
        if (s_tile_shown < 0 || s_tile_shown >= DASH_TILES) {
            s_tile_shown = 0;
        }
        s_sig_shown = s_tile[s_tile_shown] + 1;
        uv_int(&v_sig_sel, s_sig_shown);
        return;
    }
    int tile = uv_get_int(&v_tile_sel, s_tile_shown);
    if (tile != s_tile_shown && tile >= 0 && tile < DASH_TILES) {
        s_tile_shown = tile;
        s_sig_shown = s_tile[tile] + 1;
        uv_int(&v_sig_sel, s_sig_shown);
        return;
    }
    int sig = uv_get_int(&v_sig_sel, s_sig_shown);
    if (sig != s_sig_shown && sig >= 0 && sig <= dash_svc_signal_count()) {
        s_sig_shown = sig;
        s_tile[s_tile_shown] = sig - 1;
        layout_write();
    }
}

static void update_setup(void)
{
    dash_svc_status_t st;
    dash_svc_get_status(&st);
    if (!st.loaded) {
        uv_strf(&v_info, "No DBC loaded%s%s", st.error[0] ? ": " : "", st.error);
    } else if (st.dbc_base < 0) {
        uv_strf(&v_info, "%s: only 29-bit IDs, the base does not apply.", st.file);
    } else {
        int off = st.bus_base - st.dbc_base;
        uv_strf(&v_info, "Loaded: %s, %u messages, %u signals.\n"
                "11-bit IDs in the file: %d..%d (0x%03X..0x%03X)\n"
                "expected on the bus:    %d..%d (0x%03X..0x%03X)%s",
                st.file, st.nmsg, st.nsig, st.dbc_base, st.dbc_last, st.dbc_base, st.dbc_last,
                st.dbc_base + off, st.dbc_last + off, st.dbc_base + off, st.dbc_last + off,
                st.dbc_last + off > 0x7FF ? "\nWarning: beyond 0x7FF, those messages can't match." : "");
    }
    update_tile_picker();
}

/* ---------------------------------------------------------------- 250 ms */

void view_dash_update(bool visible)
{
    bool setup = nav_is("dash_setup");
    bool signals = nav_is("dash_signals");
    dash_svc_set_active(visible || setup || signals);
    if ((visible || setup || signals) && !s_tried && waveshare_sd_card_mounted()) {
        load_from_settings();
    }
    if (visible || setup) {
        resolve_layout();
    }
    if (setup) {
        if (!s_setup_shown) {
            setup_enter();
        }
        update_setup();
    }
    s_setup_shown = setup;

    if (signals) {
        dash_svc_status_t st;
        dash_svc_get_status(&st);
        if (st.loaded) {
            dash_svc_format_signals(s_table, sizeof(s_table), ROWS);
            uv_str(&v_signals, s_table);
        } else {
            uv_str(&v_signals, "No DBC loaded.");
        }
    }
    if (!visible) {
        return;
    }

    dash_svc_status_t st;
    dash_svc_get_status(&st);
    if (!waveshare_sd_card_mounted()) {
        uv_str(&v_status, "No SD card: the DBC goes in /db/dbc/ on the card.");
    } else if (!st.loaded) {
        uv_strf(&v_status, "%s - upload a .dbc on the web page, then Settings > Apply.",
                st.error[0] ? st.error : "no DBC loaded");
    } else if (st.frames == 0) {
        uv_strf(&v_status, "%s, base 0x%03X: nothing received yet. ECU broadcasting? Base ID? CAN bitrate?",
                st.file, st.bus_base >= 0 ? st.bus_base : 0);
    } else {
        uv_strf(&v_status, "%s, base 0x%03X - %u of %u messages seen",
                st.file, st.bus_base >= 0 ? st.bus_base : 0, st.msgs_seen, st.nmsg);
    }
}
