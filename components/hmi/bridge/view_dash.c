/*
 * Dash pages (roadmap B): a CAN broadcast (rusEFI) decoded by dash_svc with
 * the DBC on the SD card.
 *
 *   dash          3 x 3 tiles: dash_t<n>_name / _value / _unit (n = 1..9),
 *                 panels objects.dash_tile<n>; dash_status. Values 10x/s
 *                 (view_dash_tick, ui_bridge's 100 ms timer), "--" when not
 *                 received for STALE_MS.
 *   dash_setup    DBC file + base ID (settings dash_dbc / dash_base, Apply);
 *                 dash_setup_info.
 *   dash_tile     a tapped tile: signal (dash_sig_list / dash_sig_sel) and
 *                 limits dash_wlo / dash_whi (warning below / above),
 *                 dash_alo / dash_ahi (alarm); Save writes the layout.
 *   dash_signals  every signal as a table (objects.dash_signal_table).
 *
 * Layout: one file per DBC, /sdcard/db/dash/<dbc file name>.txt, lines
 * "<tile> = <signal name> [| wlo=<v> whi=<v> alo=<v> ahi=<v>]" ('#'
 * comments), written when a tile is saved. Tile colour: amber = warning,
 * red blinking = alarm; leaving a level needs 1 % of the DBC range
 * (hysteresis), so a value on the limit doesn't flicker.
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
#include "screens.h"
#include "comsm.h"
#include "obd_client.h"

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
static uivar_t v_edit_title = UIVAR("dash_edit_title");
static uivar_t v_edit_info = UIVAR("dash_edit_info");
static uivar_t v_lim[4] = { UIVAR("dash_wlo"), UIVAR("dash_whi"), UIVAR("dash_alo"), UIVAR("dash_ahi") };
static const char *const LIM_KEY[4] = { "wlo", "whi", "alo", "ahi" };
enum { WLO, WHI, ALO, AHI };
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
typedef struct {
    bool   has[4];
    double v[4];
} limits_t;
static limits_t s_lim[DASH_TILES];
static int      s_level[DASH_TILES];        /* 0 normal, 1 warning, 2 alarm (colour shown) */
static int      s_edit = -1;                /* tile on the dash_tile page */
static bool     s_edit_shown;
static uint32_t s_ticks;
static lv_obj_t *s_tile_obj[DASH_TILES];
static char     s_dbc_file[48];
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
    memset(s_lim, 0, sizeof(s_lim));
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') continue;
        int t = atoi(p);
        char *eq = strchr(p, '=');
        if (t < 1 || t > DASH_TILES || eq == NULL) continue;
        char *name = eq + 1;
        char *bar = strchr(name, '|');
        if (bar) {
            *bar = '\0';
            for (char *tok = strtok(bar + 1, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
                for (int k = 0; k < 4; k++) {
                    size_t kl = strlen(LIM_KEY[k]);
                    if (strncasecmp(tok, LIM_KEY[k], kl) == 0 && tok[kl] == '=') {
                        s_lim[t - 1].has[k] = true;
                        s_lim[t - 1].v[k] = strtod(tok + kl + 1, NULL);
                    }
                }
            }
        }
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
    fprintf(f, "# OBD_Dash dash layout for %s: <tile 1..%d, row by row> = <signal name>"
            " [| wlo=<warning below> whi=<warning above> alo=<alarm below> ahi=<alarm above>]\n",
            s_dbc_file, DASH_TILES);
    for (int t = 0; t < DASH_TILES; t++) {
        dash_svc_value_t v;
        fprintf(f, "%d = %s", t + 1, s_tile[t] >= 0 && dash_svc_value(s_tile[t], &v) ? v.name : "");
        const char *sep = " |";
        for (int k = 0; k < 4; k++) {
            if (s_lim[t].has[k]) {
                fprintf(f, "%s %s=%g", sep, LIM_KEY[k], s_lim[t].v[k]);
                sep = "";
            }
        }
        fputc('\n', f);
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
        memset(s_lim, 0, sizeof(s_lim));
    } else if (!layout_read()) {
        memset(s_lim, 0, sizeof(s_lim));
        layout_defaults();
    }
}

/* ---------------------------------------------------------------- tiles */

/* 0 normal, 1 warning, 2 alarm; `cur` = level shown, leaving it needs `h` */
static int level_of(const limits_t *l, double x, int cur, double h)
{
    double ha = cur >= 2 ? h : 0, hw = cur >= 1 ? h : 0;
    if ((l->has[AHI] && x >= l->v[AHI] - ha) || (l->has[ALO] && x <= l->v[ALO] + ha)) {
        return 2;
    }
    if ((l->has[WHI] && x >= l->v[WHI] - hw) || (l->has[WLO] && x <= l->v[WLO] + hw)) {
        return 1;
    }
    return 0;
}

static void set_tile_colour(int t, int level, bool blink_off)
{
    static const uint32_t COL[] = { 0x161e28, 0x7a5a00, 0xa01c1c };     /* panel, amber, red */
    if (s_tile_obj[t] == NULL) {
        return;
    }
    uint32_t c = level == 2 && blink_off ? 0x4a1010 : COL[level];
    lv_obj_set_style_bg_color(s_tile_obj[t], lv_color_hex(c), LV_PART_MAIN);
}

void view_dash_tick(void)
{
    if (!nav_is("dash")) {
        return;
    }
    s_ticks++;
    bool blink_off = (s_ticks / 4) & 1;         /* 0.8 s period */
    resolve_layout();
    for (int t = 0; t < DASH_TILES; t++) {
        dash_svc_value_t v;
        int level = 0;
        if (s_tile[t] < 0 || !dash_svc_value(s_tile[t], &v)) {
            uv_str(&v_tname[t], "");
            uv_str(&v_tvalue[t], "");
            uv_str(&v_tunit[t], "");
        } else {
            uv_str(&v_tname[t], v.name);
            uv_str(&v_tunit[t], v.unit);
            if (!v.received || v.age_ms > STALE_MS) {
                uv_str(&v_tvalue[t], "--");
            } else {
                uv_strf(&v_tvalue[t], "%.*f", v.decimals, v.value);
                double range = v.max - v.min;
                level = level_of(&s_lim[t], v.value, s_level[t], range > 0 ? range * 0.01 : 0);
            }
        }
        if (level != s_level[t] || level == 2) {
            set_tile_colour(t, level, blink_off);
            s_level[t] = level;
        }
    }
}

static void tile_clicked_cb(lv_event_t *e)
{
    s_edit = (int)(intptr_t)lv_event_get_user_data(e);
    s_edit_shown = false;
    nav_go("dash_tile");
}

void view_dash_init(void)
{
    lv_obj_t *const tiles[DASH_TILES] = {
        objects.dash_tile1, objects.dash_tile2, objects.dash_tile3, objects.dash_tile4, objects.dash_tile5,
        objects.dash_tile6, objects.dash_tile7, objects.dash_tile8, objects.dash_tile9,
    };
    for (int t = 0; t < DASH_TILES; t++) {
        s_tile_obj[t] = tiles[t];
        if (tiles[t]) {
            lv_obj_add_flag(tiles[t], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(tiles[t], tile_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)t);
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

/* ---------------------------------------------------------------- tile page */

static void edit_enter(void)
{
    build_sig_list();
    uv_strf(&v_edit_title, "Tile %d", s_edit + 1);
    uv_int(&v_sig_sel, s_tile[s_edit] + 1);
    for (int k = 0; k < 4; k++) {
        if (s_lim[s_edit].has[k]) {
            uv_strf(&v_lim[k], "%g", s_lim[s_edit].v[k]);
        } else {
            uv_str(&v_lim[k], "");
        }
    }
}

static void update_edit(void)
{
    int sig = uv_get_int(&v_sig_sel, 0) - 1;
    dash_svc_value_t v;
    if (sig < 0 || !dash_svc_value(sig, &v)) {
        uv_str(&v_edit_info, "empty tile");
    } else if (!v.received) {
        uv_strf(&v_edit_info, "%s: not received yet, range %g..%g %s", v.name, v.min, v.max, v.unit);
    } else {
        uv_strf(&v_edit_info, "%s: now %.*f %s, range %g..%g", v.name, v.decimals, v.value, v.unit, v.min, v.max);
    }
}

void view_dash_tile_clear(void)
{
    for (int k = 0; k < 4; k++) {
        uv_str(&v_lim[k], "");
    }
}

void view_dash_tile_save(void)
{
    if (s_edit < 0 || s_edit >= DASH_TILES) {
        return;
    }
    limits_t l = { 0 };
    for (int k = 0; k < 4; k++) {
        char t[16];
        uv_get_str(&v_lim[k], t, sizeof(t));
        if (t[0] == '\0') {
            continue;
        }
        char *end;
        double x = strtod(t, &end);
        if (*end != '\0') {
            view_show_message("Limits", "Use plain numbers, e.g. 105 or -0.5; empty = no limit.");
            return;
        }
        l.has[k] = true;
        l.v[k] = x;
    }
    int sig = uv_get_int(&v_sig_sel, 0) - 1;
    s_tile[s_edit] = sig >= 0 && sig < dash_svc_signal_count() ? sig : -1;
    s_lim[s_edit] = l;
    s_level[s_edit] = -1;               /* recolour */
    layout_write();
    nav_go("dash");
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
}

/* ---------------------------------------------------------------- 250 ms */

void view_dash_update(bool visible)
{
    bool setup = nav_is("dash_setup");
    bool signals = nav_is("dash_signals");
    bool tile = nav_is("dash_tile");
    bool on = visible || setup || signals || tile;
    app_settings_t cfg;
    settings_get(&cfg);
    bool adapter = cfg.obd_link == OBD_LINK_ELM;        /* CAN page: Car link */
    dash_svc_set_active(on, adapter);
    if (on && !s_tried && waveshare_sd_card_mounted()) {
        load_from_settings();
    }
    /* adapter link: the comsm task runs the adapter's monitor while OBD is idle */
    uint32_t filter, mask;
    if (on && adapter && dash_svc_id_filter(&filter, &mask)) {
        comsm_set_link_monitor(true, filter, mask, dash_svc_push_frame, NULL);
    } else {
        comsm_set_link_monitor(false, 0, 0, NULL, NULL);
    }
    if (tile && s_edit >= 0) {
        if (!s_edit_shown) {
            edit_enter();
            s_edit_shown = true;
        }
        update_edit();
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
    comsm_status_t cs;
    comsm_get_status(&cs);
    if (!waveshare_sd_card_mounted()) {
        uv_str(&v_status, "No SD card: the DBC goes in /db/dbc/ on the card.");
    } else if (st.loaded && adapter && cs.state != COMSM_IDLE) {
        uv_strf(&v_status, "%s via BLE adapter: paused while OBD uses the adapter (Disconnect on the OBD page).", st.file);
    } else if (st.loaded && adapter && !cs.monitoring) {
        uv_strf(&v_status, "%s via BLE adapter: waiting for the adapter (connect it on the BLE page)...", st.file);
    } else if (!st.loaded) {
        uv_strf(&v_status, "%s - upload a .dbc on the web page, then Settings > Apply.",
                st.error[0] ? st.error : "no DBC loaded");
    } else if (st.frames == 0) {
        uv_strf(&v_status, "%s, base 0x%03X: nothing received yet. ECU broadcasting? Base ID? CAN bitrate?",
                st.file, st.bus_base >= 0 ? st.bus_base : 0);
    } else {
        uv_strf(&v_status, "%s, base 0x%03X%s - %u of %u messages seen%s",
                st.file, st.bus_base >= 0 ? st.bus_base : 0, adapter ? " via BLE adapter" : "",
                st.msgs_seen, st.nmsg, st.dropped ? ", frames dropped" : "");
    }
}
