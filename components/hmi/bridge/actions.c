/*
 * EEZ Studio native actions (declared in the generated src/ui/actions.h).
 * Every action in the EEZ project needs a function here or the link fails.
 * Keep them thin: the work is in the views.
 */
#include "actions.h"
#include "bridge.h"
#include "comsm.h"

/* sidebar */
void action_nav_home(lv_event_t *e)        { nav_go("home"); }
void action_nav_obd(lv_event_t *e)         { nav_go("obd"); }
void action_nav_readdtc(lv_event_t *e)     { nav_go("readDTC"); }
void action_nav_can(lv_event_t *e)         { nav_go("can"); }
void action_nav_wifi(lv_event_t *e)        { nav_go("wifi"); }
void action_nav_ble(lv_event_t *e)         { nav_go("ble"); }
void action_nav_io(lv_event_t *e)          { nav_go("io"); }
void action_nav_display(lv_event_t *e)     { nav_go("display"); }
void action_nav_dash(lv_event_t *e)        { nav_go("dash"); }
void action_nav_diag(lv_event_t *e)        { nav_go("diag"); }

/* sub pages */
void action_nav_params(lv_event_t *e)
{
    pid_select_view_build();                /* list reflects the ECU support right now */
    nav_go("select_params");
}
void action_nav_freeze(lv_event_t *e)      { nav_go("freezFrame"); }   /* SCREEN_LOADED reads it */

/* OBD page */
void action_conn_button_pressed(lv_event_t *e)
{
    comsm_status_t st;
    comsm_get_status(&st);
    if (st.state == COMSM_IDLE) {
        view_can_prepare_obd();             /* listen-only can't send requests */
    }
    comsm_toggle();
}
void action_obd_drive(lv_event_t *e)       { nav_go_anim("drive", LV_SCR_LOAD_ANIM_FADE_IN, 200); }
void action_obd_race(lv_event_t *e)        { nav_go_anim("liveData", LV_SCR_LOAD_ANIM_FADE_IN, 200); }
void action_obd_tune(lv_event_t *e)        { nav_go_anim("tune", LV_SCR_LOAD_ANIM_FADE_IN, 200); }

/* CAN page */
void action_can_start_stop(lv_event_t *e)  { view_can_start_stop(); }
void action_can_reset(lv_event_t *e)       { view_can_reset(); }

/* IO page */
void action_io_do0_toggle(lv_event_t *e)   { view_io_toggle_do0(); }
void action_io_do1_toggle(lv_event_t *e)   { view_io_toggle_do1(); }
void action_io_reset(lv_event_t *e)        { view_io_reset(); }
void action_i2c_scan(lv_event_t *e)        { view_io_i2c_scan(); }

/* Wi-Fi / BLE */
void action_wifi_scan(lv_event_t *e)       { view_wifi_scan(); }
void action_wifi_connect(lv_event_t *e)    { view_wifi_connect(); }
void action_wifi_disconnect(lv_event_t *e) { view_wifi_disconnect(); }
void action_wifi_forget(lv_event_t *e)     { view_wifi_forget(); }
void action_ble_scan(lv_event_t *e)        { view_ble_scan(); }
void action_ble_connect(lv_event_t *e)     { view_ble_connect(); }
void action_ble_disconnect(lv_event_t *e)  { view_ble_disconnect(); }
void action_ble_forget(lv_event_t *e)      { view_ble_forget(); }

/* Display & system */
void action_sd_remount(lv_event_t *e)      { view_display_sd_remount(); }
void action_screen_off(lv_event_t *e)      { screen_off(); }
void action_restart(lv_event_t *e)         { view_display_restart(); }
