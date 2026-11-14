/*
 * hmi - builds the EEZ UI and runs the bridge timers.
 *
 * Menu pages (tools/obd_ui.py) are driven by name: global variables
 * (uivar.c), native actions (actions.c) and screens (nav.c). The live views
 * and OBD controllers are driven by ui_bridge.c (connection, needles, live
 * values, DTC / freeze frame / parameter list). Services are polled from
 * lv_timers; nothing else in the firmware touches LVGL.
 */
#include "hmi.h"

#include "lvgl.h"
#include "ui.h"
#include "screens.h"
#include "eez_vars.h"
#include "bridge.h"
#include "settings.h"

#define FAST_PERIOD_MS      250     /* status line, menu pages, inputs */
#define SLOW_PERIOD_MS      1000    /* home page */
#define TICK_FALLBACK_MS    100     /* EEZ tick at least this often */

static uint32_t s_last_tick;

/* EEZ tick (variables -> widgets) only when something changed, plus a
 * periodic one for the flow engine (from Esp32_monitor). */
static void apply_vars(bool force)
{
    if (eez_vars_dirty || force) {
        eez_vars_dirty = false;
        s_last_tick = lv_tick_get();
        ui_tick();
    }
}

static void fast_cb(lv_timer_t *t)
{
    view_status_update();
    view_wifi_update(nav_is("wifi"));
    view_ble_update(nav_is("ble"));
    view_elm_update(nav_is("ble_console"));
    view_dash_update(nav_is("dash"));
    view_can_update(nav_is("can"));
    view_io_update(nav_is("io"));
    view_link_update(nav_is("io"));
    view_display_update(nav_is("display"));
    screen_poll();
    apply_vars(false);      /* draw in this same lv_timer_handler() pass */
}

static void slow_cb(lv_timer_t *t)
{
    view_home_update(nav_is("home"));
    apply_vars(false);
}

void hmi_init(void)
{
    app_settings_t cfg;
    settings_get(&cfg);

    ui_init();
    /* EEZ 0.29 offers only Montserrat: the ID table wants fixed width. */
    if (objects.can_id_table != NULL) {
        lv_obj_set_style_text_font(objects.can_id_table, &lv_font_unscii_8, 0);
    }
    if (objects.dash_signal_table != NULL) {
        lv_obj_set_style_text_font(objects.dash_signal_table, &lv_font_unscii_8, 0);
    }
    screen_init(cfg.brightness, cfg.screen_timeout_idx);
    view_home_init();
    view_display_init();
    view_wifi_init();
    view_ble_init();
    view_obd_init();
    view_can_init();
    view_dash_init();
    view_link_init();
    obd_bridge_init();
    nav_init(cfg.page);

    fast_cb(NULL);
    slow_cb(NULL);
    lv_timer_create(fast_cb, FAST_PERIOD_MS, NULL);
    lv_timer_create(slow_cb, SLOW_PERIOD_MS, NULL);
}

void hmi_tick(void)
{
    /* after every lv_timer_handler(): catches variables written by event
     * handlers and actions, plus a periodic EEZ tick for the flow engine */
    apply_vars(lv_tick_elaps(s_last_tick) >= TICK_FALLBACK_MS);
}
