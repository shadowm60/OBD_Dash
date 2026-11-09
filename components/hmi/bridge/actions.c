/*
 * EEZ Studio action implementations (declared in src/ui/actions.h).
 * Keep these thin: translate UI events into comsm / obd calls.
 */
#include <stdio.h>
#include <stdint.h>
#include "actions.h"
#include "screens.h"
#include "esp_log.h"
#include "comsm.h"
#include "obd_pids.h"
#include "vehicle_data.h"

static const char *TAG = "hmi_actions";

extern objects_t objects;

void action_log_raw_enabled(lv_event_t *e)
{
    /* SD logging: phase 4 */
}

void action_log_params_enabled(lv_event_t *e)
{
    /* SD logging: phase 4 */
}

static void pid_checkbox_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    size_t idx = (size_t)(intptr_t)lv_obj_get_user_data(cb);
    obd_pid_set_enabled(idx, lv_obj_has_state(cb, LV_STATE_CHECKED));
}

/* PID selection list: one checkbox per catalogue PID. While connected,
 * PIDs the ECU does not support are shown disabled. */
void action_obd_srv_screen_loaded(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SCREEN_LOADED) {
        return;
    }

    comsm_status_t st;
    comsm_get_status(&st);
    bool known_support = (st.state == COMSM_RUNNING);

    lv_obj_t *list = objects.obd_srv_table;
    lv_obj_clean(list);

    for (size_t i = 0; i < obd_pid_count(); i++) {
        const obd_pid_desc_t *d = obd_pid_desc(i);
        const vd_signal_info_t *info = vd_info(d->signal);

        char txt[48];
        snprintf(txt, sizeof(txt), "%02X  %s", d->pid, info ? info->name : "?");

        lv_obj_t *item = lv_list_add_btn(list, NULL, NULL);
        lv_obj_t *cb = lv_checkbox_create(item);
        lv_checkbox_set_text(cb, txt);
        lv_obj_set_style_text_font(cb, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

        if (obd_pid_is_enabled(i)) {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
        if (known_support && !obd_pid_is_supported(d->pid)) {
            lv_obj_add_state(cb, LV_STATE_DISABLED);
        }
        lv_obj_set_user_data(cb, (void *)(intptr_t)i);
        lv_obj_add_event_cb(cb, pid_checkbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    ESP_LOGD(TAG, "PID list built (%u entries)", (unsigned)obd_pid_count());
}

/* Connect / Disconnect / Cancel button */
void action_conn_button_pressed(lv_event_t *e)
{
    comsm_toggle();
}
