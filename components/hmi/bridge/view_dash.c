/*
 * Dash page (B1 of the roadmap): what dash_svc decodes from the CAN
 * broadcast with the DBC on the SD card, as a live signal table.
 *   dash_status, dash_signals (monospace: objects.dash_signal_table)
 * Dash settings (sub-page dash_setup): DBC file + base ID, stored in the
 * settings (dash_dbc, dash_base), applied with Apply.
 *   dash_files / dash_file_sel (dropdown), dash_base_text, dash_setup_info
 * Decoding runs only while the Dash page is shown. The DBC is loaded the
 * first time the page opens.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "bridge.h"
#include "dash_svc.h"
#include "settings.h"
#include "sd_card.h"

static const char *TAG = "view_dash";

#define ROWS    160

static uivar_t v_status = UIVAR("dash_status");
static uivar_t v_signals = UIVAR("dash_signals");
static uivar_t v_files = UIVAR("dash_files");
static uivar_t v_file_sel = UIVAR("dash_file_sel");
static uivar_t v_base = UIVAR("dash_base_text");
static uivar_t v_info = UIVAR("dash_setup_info");

static bool s_tried;
static bool s_setup_shown;
EXT_RAM_BSS_ATTR static char s_table[ROWS * 64 + 128];
EXT_RAM_BSS_ATTR static char s_files[16 * 48];
static int  s_nfiles;

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

/* name of entry `idx` of the '\n' separated list */
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
    if (n >= len) {
        n = len - 1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
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
    for (int i = 0; i < s_nfiles && file_at(i, name, sizeof(name)); i++) {
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
    file_at(uv_get_int(&v_file_sel, 0), name, sizeof(name));
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

void view_dash_update(bool visible)
{
    bool setup = nav_is("dash_setup");
    dash_svc_set_active(visible || setup);
    if (setup) {
        if (!s_setup_shown) {
            setup_enter();
        }
        update_setup();
    }
    s_setup_shown = setup;
    if (!visible) {
        return;
    }
    if (!s_tried && waveshare_sd_card_mounted()) {
        load_from_settings();
    }

    dash_svc_status_t st;
    dash_svc_get_status(&st);
    if (!waveshare_sd_card_mounted()) {
        uv_str(&v_status, "No SD card: put the DBC (e.g. rusEFI's) in /db/dbc/ on the card.");
    } else if (!st.loaded) {
        uv_strf(&v_status, "%s\nUpload a .dbc on the web page (or put it in /db/dbc/), then Settings > Apply.",
                st.error[0] ? st.error : "no DBC loaded");
    } else {
        uv_strf(&v_status, "%s, base 0x%03X: %u messages, %u signals%s\n"
                "%lu frames decoded, %u of %u messages seen, %lu frames with other IDs%s",
                st.file, st.bus_base >= 0 ? st.bus_base : 0, st.nmsg, st.nsig,
                st.skipped ? " (some lines skipped)" : "",
                (unsigned long)st.frames, st.msgs_seen, st.nmsg, (unsigned long)st.unknown,
                st.frames == 0 ? "\nNothing yet: ECU broadcasting? Base ID right (Settings)? CAN page bitrate?" : "");
    }
    if (st.loaded) {
        dash_svc_format_signals(s_table, sizeof(s_table), ROWS);
        uv_str(&v_signals, s_table);
    } else {
        uv_str(&v_signals, "");
    }
}
