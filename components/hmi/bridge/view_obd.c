/*
 * OBD page options and CAN page logging (replaces the old Settings / System
 * screens). Runs every 100 ms from ui_bridge. Inputs are two-way bound EEZ
 * variables: read back, stored in settings, applied to comsm / datalog.
 *
 *   obd_log_params, obd_read_speed (inputs)
 *   obd_speed_text, obd_detail                                  (texts)
 *   obd_mil_text + objects.obd_mil_bt: MIL telltale (PID 01), only while connected
 *   can_log_raw, can_log_all (inputs), can_log_info             (CAN page)
 *
 * conn_status / conn_button_text (connection line and button) come from
 * ui_bridge.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "bridge.h"
#include "settings.h"
#include "comsm.h"
#include "obd_pids.h"
#include "datalog.h"
#include "vehicle_data.h"
#include "screens.h"

static uivar_t v_log_params = UIVAR("obd_log_params");
static uivar_t v_speed = UIVAR("obd_read_speed");
static uivar_t v_speed_text = UIVAR("obd_speed_text");
static uivar_t v_detail = UIVAR("obd_detail");
static uivar_t v_log_raw = UIVAR("can_log_raw");
static uivar_t v_log_all = UIVAR("can_log_all");
static uivar_t v_log_info = UIVAR("can_log_info");

static uivar_t v_mil = UIVAR("obd_mil_text");

static uint32_t s_log_errors;
static int      s_mil_level = -1;      /* 0 hidden (unknown), 1 ok, 2 DTCs, 3 MIL on */

#define MIL_STALE_MS 6000              /* PID 01 is polled at ~0.5 Hz */

/* MIL telltale: red = MIL on, amber = stored DTCs without MIL, green = none;
 * hidden while not known (not connected / ECU doesn't support PID 01). */
static void update_mil(void)
{
    vd_sample_t mil, cnt;
    bool have_mil = vd_get(VD_MIL, MIL_STALE_MS, &mil);
    bool have_cnt = vd_get(VD_DTC_COUNT, MIL_STALE_MS, &cnt);
    int n = have_cnt ? (int)cnt.value : 0;
    bool on = have_mil && mil.value >= 0.5f;
    int level = !have_mil ? 0 : on ? 3 : n > 0 ? 2 : 1;

    uv_str(&v_mil, LV_SYMBOL_WARNING);
    if (level != s_mil_level && objects.obd_mil_bt != NULL) {
        static const uint32_t colours[] = { 0x22303f, 0x1f7a3d, 0xb7791f, 0xc0392b };
        s_mil_level = level;
        lv_obj_set_style_bg_color(objects.obd_mil_bt, lv_color_hex(colours[level]), LV_PART_MAIN);
        if (level == 0) {
            lv_obj_add_flag(objects.obd_mil_bt, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(objects.obd_mil_bt, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void view_obd_init(void)
{
    app_settings_t cfg;
    settings_get(&cfg);
    uv_bool(&v_log_params, cfg.log_params);
    uv_int(&v_speed, cfg.pids_per_request);
    uv_bool(&v_log_raw, cfg.log_raw);
    uv_bool(&v_log_all, cfg.log_all_traffic);
    comsm_set_pids_per_request(cfg.pids_per_request);
    datalog_enable_raw(cfg.log_raw);
    datalog_enable_params(cfg.log_params);
    datalog_set_all_traffic(cfg.log_all_traffic);
}

static void read_inputs(const app_settings_t *cfg)
{
    bool params = uv_get_bool(&v_log_params, cfg->log_params);
    bool raw = uv_get_bool(&v_log_raw, cfg->log_raw);
    bool all = uv_get_bool(&v_log_all, cfg->log_all_traffic);
    settings_set_log_params(params);
    settings_set_log_raw(raw);
    settings_set_log_all_traffic(all);
    datalog_enable_params(params);
    datalog_enable_raw(raw);
    datalog_set_all_traffic(all);

    int n = uv_get_int(&v_speed, cfg->pids_per_request);
    if (n >= 1 && n <= OBD_MAX_PIDS_PER_REQUEST) {
        settings_set_pids_per_request((uint8_t)n);
        comsm_set_pids_per_request((uint8_t)n);
    }
}

static void update_texts(void)
{
    update_mil();
    app_settings_t cfg;
    settings_get(&cfg);
    comsm_status_t st;
    comsm_get_status(&st);

    unsigned n = cfg.pids_per_request;
    if (st.state == COMSM_RUNNING && st.pids_per_request < n) {
        uv_strf(&v_speed_text, "Read speed x%u - ECU accepts 1 PID", n);
    } else {
        uv_strf(&v_speed_text, "Read speed x%u - %u PID%s per request", n, n, n > 1 ? "s" : "");
    }

    if (st.state == COMSM_RUNNING) {
        uv_strf(&v_detail, "%u PIDs selected and supported, %u per request\n"
                "%" PRIu32 " responses, %" PRIu32 " timeouts",
                obd_pid_active_count(), st.pids_per_request, st.responses, st.timeouts);
    } else if (obd_pid_active_count() == 0 && st.state == COMSM_IDLE) {
        uv_str(&v_detail, "Tick the parameters to poll in Select parameters.");
    } else {
        uv_str(&v_detail, "");
    }

    datalog_status_t ls;
    datalog_get_status(&ls);
    char raw[96], par[80];
    if (ls.raw_active) {
        snprintf(raw, sizeof(raw), "Raw: %s, %" PRIu32 " frames%s", ls.raw_file, ls.raw_frames,
                 ls.raw_dropped ? " (some dropped)" : "");
    } else {
        snprintf(raw, sizeof(raw), "Raw: %s", cfg.log_raw ? "starts with the monitor or OBD" : "off");
    }
    if (ls.params_active) {
        snprintf(par, sizeof(par), "Parameters: %s, %" PRIu32 " rows", ls.params_file, ls.params_rows);
    } else {
        snprintf(par, sizeof(par), "Parameters: %s", cfg.log_params ? "waiting for OBD data" : "off");
    }
    uv_strf(&v_log_info, "%s\n%s%s%s", raw, par, ls.error[0] ? "\nLast error: " : "", ls.error);

    /* Tell the user once per failure (e.g. no SD card); the logger retries itself. */
    if (ls.error_count != s_log_errors) {
        s_log_errors = ls.error_count;
        view_show_message("Logging", ls.error);
    }
}

void view_obd_update(void)
{
    app_settings_t cfg;
    settings_get(&cfg);
    read_inputs(&cfg);
    update_texts();
}
