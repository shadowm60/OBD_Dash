/*
 * CAN page: bus monitor (canmon) + bus settings. Logging checkboxes on the
 * same page are handled by view_obd.c.
 *   can_bitrate_sel, can_listen_only (two-way), can_run_text, can_stats,
 *   can_load, can_load_text, can_ids (monospace: objects.can_id_table)
 *
 * Bitrate and listen-only restart the CAN controller, so they are only
 * changed while OBD is disconnected. Listen-only is not stored: the dash
 * always boots able to talk to the ECU.
 */
#include <stdio.h>
#include <inttypes.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bridge.h"
#include "settings.h"
#include "comsm.h"
#include "canmon.h"

/* index = can_bitrate_sel, same order as the dropdown (tools/obd_ui.py) */
static const uint32_t BITRATES[] = { 125000, 250000, 500000, 1000000 };
#define N_BITRATES (sizeof(BITRATES) / sizeof(BITRATES[0]))
#define ID_ROWS    80
#define IDS_LEN    (64 * (ID_ROWS + 2))

static uivar_t v_bitrate = UIVAR("can_bitrate_sel");
static uivar_t v_listen = UIVAR("can_listen_only");
static uivar_t v_run = UIVAR("can_run_text");
static uivar_t v_stats = UIVAR("can_stats");
static uivar_t v_load = UIVAR("can_load");
static uivar_t v_load_text = UIVAR("can_load_text");
static uivar_t v_ids = UIVAR("can_ids");

static const char *TAG = "view_can";

static int  s_sel;
static int64_t s_pending_us;      /* start / stop sent, waiting for canmon */
static bool s_pending_run;
static bool s_listen;
EXT_RAM_BSS_ATTR static char s_ids[IDS_LEN];     /* PSRAM */

uint32_t view_can_bitrate(uint8_t idx)
{
    return BITRATES[idx < N_BITRATES ? idx : 2];
}

static bool obd_idle(void)
{
    comsm_status_t st;
    comsm_get_status(&st);
    return st.state == COMSM_IDLE;
}

void view_can_init(void)
{
    app_settings_t cfg;
    settings_get(&cfg);
    s_sel = cfg.can_bitrate_idx < N_BITRATES ? cfg.can_bitrate_idx : 2;
    s_listen = false;
    uv_int(&v_bitrate, s_sel);
    uv_bool(&v_listen, false);
}

/* Bus settings from the page: only while OBD is disconnected. */
static void read_inputs(void)
{
    int sel = uv_get_int(&v_bitrate, s_sel);
    bool listen = uv_get_bool(&v_listen, s_listen);
    if (sel == s_sel && listen == s_listen) {
        return;
    }
    if (sel < 0 || sel >= (int)N_BITRATES || !obd_idle()) {
        if (sel >= 0 && sel < (int)N_BITRATES) {
            view_show_message("CAN bus", "Disconnect OBD first: changing the bus settings restarts the CAN controller.");
        }
        uv_int(&v_bitrate, s_sel);      /* put the inputs back */
        uv_bool(&v_listen, s_listen);
        return;
    }
    s_listen = listen;
    if (sel != s_sel) {
        s_sel = sel;
        app_settings_t cfg;
        settings_get(&cfg);
        cfg.can_bitrate_idx = (uint8_t)sel;
        settings_put(&cfg);
    }
    canmon_set_bus(BITRATES[s_sel], s_listen);
}

void view_can_update(bool visible)
{
    read_inputs();

    canmon_stats_t st;
    canmon_get_stats(&st);
    if (s_pending_us && (st.running == s_pending_run || esp_timer_get_time() - s_pending_us > 2000000)) {
        if (st.running != s_pending_run) {
            ESP_LOGW(TAG, "monitor did not %s within 2 s", s_pending_run ? "start" : "stop");
        }
        s_pending_us = 0;
    }
    uv_str(&v_run, s_pending_us ? (s_pending_run ? "Starting..." : "Stopping...")
                                : st.running ? "Stop monitor" : "Start monitor");
    uv_int(&v_load, st.running ? st.load_pct : 0);
    uv_strf(&v_load_text, "%u %%", st.running ? st.load_pct : 0);
    if (!visible) {
        return;
    }

    char up[24];
    const char *what = !st.running ? "monitor stopped - "
                     : (st.frames == 0 && st.uptime_s >= 2) ? "no frames received (quiet bus?) - "
                     : "";
    uv_strf(&v_stats,
            "%" PRIu32 " kbit/s%s, bus %s\n"
            "%s%" PRIu32 " frames/s (peak %" PRIu32 "), %" PRIu32 " IDs%s\n"
            "%" PRIu32 " frames (%" PRIu32 " std, %" PRIu32 " ext) in %s\n"
            "errors: bus %" PRIu32 ", TEC %" PRIu32 ", REC %" PRIu32 "%s",
            st.bitrate / 1000, st.listen_only ? ", listen only" : "", st.state,
            what, st.fps, st.fps_peak, st.ids,
            st.id_table_full ? " (table full)" : "",
            st.frames, st.std_frames, st.ext_frames, fmt_duration(st.uptime_s, up, sizeof(up)),
            st.bus_errors, st.tec, st.rec, st.dropped ? ", some frames dropped" : "");
    canmon_format_ids(s_ids, sizeof(s_ids), ID_ROWS);
    uv_str(&v_ids, s_ids);
}

void view_can_start_stop(void)
{
    canmon_stats_t st;
    canmon_get_stats(&st);
    esp_err_t err = st.running ? canmon_stop() : canmon_start();
    ESP_LOGI(TAG, "%s monitor: %s", st.running ? "stop" : "start", esp_err_to_name(err));
    if (err == ESP_OK) {
        s_pending_run = !st.running;
        s_pending_us = esp_timer_get_time();
    } else {
        view_show_message("CAN monitor", "The monitor task did not take the command.");
    }
}

void view_can_reset(void)
{
    canmon_reset();
}

/* Before OBD connects: the bus must be able to send (no listen-only). */
void view_can_prepare_obd(void)
{
    if (s_listen) {
        s_listen = false;
        uv_bool(&v_listen, false);
        canmon_set_bus(BITRATES[s_sel], false);
    }
}
