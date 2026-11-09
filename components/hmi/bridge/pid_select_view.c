/*
 * pid_select_view - Settings > Select Params.
 *
 * Only the PIDs checked here are polled. A PID can only be checked if the ECU
 * supports it: while connected the live discovery result is used, otherwise
 * the support bitmap of the last ECU (persisted in settings). Unsupported PIDs
 * are listed greyed out at the bottom; their stored selection is kept, so a
 * different car that supports them picks them up again.
 */
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_log.h"
#include "screens.h"
#include "comsm.h"
#include "obd_pids.h"
#include "vehicle_data.h"
#include "settings.h"
#include "views.h"

static const char *TAG = "pid_select";

#define BITMAP_LEN  (256 / 8)

extern objects_t objects;

static uint8_t s_shown_bitmap[BITMAP_LEN];  /* support info the list was built with */
static bool    s_shown_valid;
static bool    s_built;

static inline bool bm_get(const uint8_t *bm, uint8_t pid)
{
    return (bm[pid >> 3] >> (pid & 7)) & 1;
}

/* Live result while running, otherwise the last ECU's. False if unknown. */
static bool current_support(uint8_t bm[BITMAP_LEN])
{
    comsm_status_t st;
    comsm_get_status(&st);
    if (st.state == COMSM_RUNNING) {
        obd_pid_get_supported_bitmap(bm);
        return true;
    }
    return settings_get_ecu_supported(bm);
}

static void checkbox_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    size_t idx = (size_t)(intptr_t)lv_obj_get_user_data(cb);
    bool on = lv_obj_has_state(cb, LV_STATE_CHECKED);
    obd_pid_set_enabled(idx, on);
    settings_set_pid_selected(obd_pid_desc(idx)->pid, on);
    ESP_LOGI(TAG, "PID %02X %s", obd_pid_desc(idx)->pid, on ? "selected" : "deselected");
}

static void add_entry(lv_obj_t *list, size_t idx, bool supported)
{
    const obd_pid_desc_t *d = obd_pid_desc(idx);
    const vd_signal_info_t *info = vd_info(d->signal);

    char txt[64];
    snprintf(txt, sizeof(txt), "%02X  %s%s", d->pid, info ? info->name : "?",
             supported ? "" : "  (not supported)");

    lv_obj_t *item = lv_list_add_btn(list, NULL, NULL);
    lv_obj_t *cb = lv_checkbox_create(item);
    lv_checkbox_set_text(cb, txt);
    lv_obj_set_style_text_font(cb, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (supported) {
        if (obd_pid_is_enabled(idx)) {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
        lv_obj_set_user_data(cb, (void *)(intptr_t)idx);
        lv_obj_add_event_cb(cb, checkbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
    } else {
        lv_obj_add_state(cb, LV_STATE_DISABLED);
    }
}

void pid_select_view_build(void)
{
    s_shown_valid = current_support(s_shown_bitmap);
    s_built = true;

    lv_obj_t *list = objects.obd_srv_table;
    lv_obj_clean(list);

    if (!s_shown_valid) {
        lv_obj_t *t = lv_list_add_text(list, "Connect to the ECU once to see which parameters it supports.");
        lv_obj_set_style_text_font(t, &lv_font_montserrat_20, LV_PART_MAIN);
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    }

    /* supported first, then the rest greyed out */
    size_t shown = 0;
    for (int pass = 0; pass < 2; pass++) {
        bool want_supported = (pass == 0);
        for (size_t i = 0; i < obd_pid_count(); i++) {
            bool sup = s_shown_valid && bm_get(s_shown_bitmap, obd_pid_desc(i)->pid);
            if (sup == want_supported) {
                add_entry(list, i, sup);
                shown += sup;
            }
        }
    }
    ESP_LOGI(TAG, "list built: %u of %u PIDs selectable", (unsigned)shown, (unsigned)obd_pid_count());
}

void pid_select_view_refresh(void)
{
    comsm_status_t st;
    comsm_get_status(&st);

    uint8_t bm[BITMAP_LEN];
    if (st.state == COMSM_RUNNING) {
        /* remember this ECU's support so the list works while disconnected */
        obd_pid_get_supported_bitmap(bm);
        settings_set_ecu_supported(bm);     /* no-op unless it changed */
    }

    if (!s_built || lv_scr_act() != lv_obj_get_screen(objects.obd_srv_table)) {
        return;
    }
    bool valid = current_support(bm);
    if (valid != s_shown_valid || (valid && memcmp(bm, s_shown_bitmap, sizeof(bm)) != 0)) {
        pid_select_view_build();
    }
}
