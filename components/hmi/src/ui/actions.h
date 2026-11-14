#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_conn_button_pressed(lv_event_t * e);
extern void action_nav_home(lv_event_t * e);
extern void action_nav_obd(lv_event_t * e);
extern void action_nav_readdtc(lv_event_t * e);
extern void action_nav_can(lv_event_t * e);
extern void action_nav_wifi(lv_event_t * e);
extern void action_nav_ble(lv_event_t * e);
extern void action_nav_io(lv_event_t * e);
extern void action_nav_display(lv_event_t * e);
extern void action_nav_params(lv_event_t * e);
extern void action_nav_freeze(lv_event_t * e);
extern void action_obd_drive(lv_event_t * e);
extern void action_obd_race(lv_event_t * e);
extern void action_obd_tune(lv_event_t * e);
extern void action_wifi_scan(lv_event_t * e);
extern void action_wifi_connect(lv_event_t * e);
extern void action_wifi_disconnect(lv_event_t * e);
extern void action_wifi_forget(lv_event_t * e);
extern void action_ble_scan(lv_event_t * e);
extern void action_ble_connect(lv_event_t * e);
extern void action_ble_disconnect(lv_event_t * e);
extern void action_ble_forget(lv_event_t * e);
extern void action_sd_remount(lv_event_t * e);
extern void action_screen_off(lv_event_t * e);
extern void action_restart(lv_event_t * e);
extern void action_can_start_stop(lv_event_t * e);
extern void action_can_reset(lv_event_t * e);
extern void action_io_do0_toggle(lv_event_t * e);
extern void action_io_do1_toggle(lv_event_t * e);
extern void action_io_reset(lv_event_t * e);
extern void action_i2c_scan(lv_event_t * e);
extern void action_nav_dash(lv_event_t * e);
extern void action_nav_diag(lv_event_t * e);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/