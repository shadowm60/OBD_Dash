/*
 * settings_view - keeps the Settings screen widgets and the settings component
 * in sync. Saved values are pushed into the widgets/EEZ variables at start;
 * afterwards widget changes are polled and stored (settings saves lazily).
 *
 * Polling (instead of EEZ actions) is deliberate: the checkbox actions fire on
 * PRESSED, before the checkbox has toggled its state.
 */
#include <string.h>
#include "lvgl.h"
#include "screens.h"
#include "vars.h"
#include "eez_vars.h"
#include "settings.h"
#include "datalog.h"
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
