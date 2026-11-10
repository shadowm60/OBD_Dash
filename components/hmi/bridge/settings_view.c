/*
 * settings_view - keeps the Settings and System screen widgets and the
 * settings component in sync. Saved values are pushed into the widgets/EEZ variables at start;
 * afterwards widget changes are polled and stored (settings saves lazily).
 *
 * Polling (instead of EEZ actions) is deliberate: the checkbox actions fire on
 * PRESSED, before the checkbox has toggled its state.
 */
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "screens.h"
#include "vars.h"
#include "eez_vars.h"
#include "settings.h"
#include "datalog.h"
#include "comsm.h"
#include "views.h"

extern objects_t objects;

static char     s_last_conn_options[96];
static uint32_t s_log_errors;

static void set_checked(lv_obj_t *cb, bool on)
{
    if (on) {
        lv_obj_add_state(cb, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(cb, LV_STATE_CHECKED);
    }
}

void settings_view_init(void)
{
    app_settings_t cfg;
    settings_get(&cfg);

    set_checked(objects.log_raw_enabled, cfg.log_raw);
    set_checked(objects.log_params_enabled, cfg.log_params);
    set_checked(objects.sys_gauge_anim, cfg.gauge_anim);
    lv_slider_set_value(objects.sys_read_speed, cfg.pids_per_request, LV_ANIM_OFF);
    comsm_set_pids_per_request(cfg.pids_per_request);
    brightness_init(cfg.brightness);
    eez_var_set_int(FLOW_GLOBAL_VARIABLE_RUSEFI_BASE_ADDR, cfg.rusefi_base_id);
    /* Dropdown selection is applied in refresh, once EEZ has filled the options. */
}

static void sync_conn_method(const app_settings_t *cfg)
{
    lv_obj_t *dd = objects.con_method_select;
    const char *opts = lv_dropdown_get_options(dd);

    /* EEZ (re)sets the option list when the screen is first ticked, which
     * resets the selection to 0. Re-apply the stored choice in that case. */
    if (strncmp(opts, s_last_conn_options, sizeof(s_last_conn_options)) != 0) {
        strncpy(s_last_conn_options, opts, sizeof(s_last_conn_options) - 1);
        if (cfg->conn_method < lv_dropdown_get_option_cnt(dd)) {
            lv_dropdown_set_selected(dd, cfg->conn_method);
        }
        return;
    }
    settings_set_conn_method((uint8_t)lv_dropdown_get_selected(dd));
}

/* System > Read speed: PIDs per Service 01 request. */
static void refresh_read_speed(void)
{
    static char s_last[48];
    uint8_t n = (uint8_t)lv_slider_get_value(objects.sys_read_speed);
    settings_set_pids_per_request(n);
    comsm_set_pids_per_request(n);

    comsm_status_t st;
    comsm_get_status(&st);
    char text[sizeof(s_last)];
    if (st.state == COMSM_RUNNING && st.pids_per_request < n) {
        snprintf(text, sizeof(text), "x%u set - ECU accepts 1 PID per request", n);
    } else {
        snprintf(text, sizeof(text), "x%u - %u PID%s per request", n, n, n > 1 ? "s" : "");
    }
    if (strcmp(text, s_last) != 0) {
        strcpy(s_last, text);
        lv_label_set_text(objects.sys_read_speed_value, text);
    }
}

void settings_view_refresh(void)
{
    app_settings_t cfg;
    settings_get(&cfg);

    sync_conn_method(&cfg);
    bool log_raw = lv_obj_has_state(objects.log_raw_enabled, LV_STATE_CHECKED);
    bool log_params = lv_obj_has_state(objects.log_params_enabled, LV_STATE_CHECKED);
    settings_set_log_raw(log_raw);
    settings_set_log_params(log_params);
    datalog_enable_raw(log_raw);
    datalog_enable_params(log_params);
    settings_set_gauge_anim(lv_obj_has_state(objects.sys_gauge_anim, LV_STATE_CHECKED));
    refresh_read_speed();
    uint8_t bright = (uint8_t)lv_slider_get_value(objects.sys_man_brightness);
    settings_set_brightness(bright);
    brightness_set(bright);

    /* Tell the user once per failure (e.g. no SD card); the logger retries itself. */
    datalog_status_t ls;
    datalog_get_status(&ls);
    if (ls.error_count != s_log_errors) {
        s_log_errors = ls.error_count;
        view_show_message("Logging", ls.error);
    }

    int32_t id = eez_var_get_int(FLOW_GLOBAL_VARIABLE_RUSEFI_BASE_ADDR);
    if (id >= 0 && id <= 0x7FF) {
        settings_set_rusefi_base_id((uint16_t)id);
    }
}
