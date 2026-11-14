/*
 * Internal to hmi/bridge. Everything here runs in the LVGL task (lv_timers,
 * event callbacks, EEZ native actions): the EEZ flow engine is not thread
 * safe, and services are only reached through their thread-safe APIs.
 *
 * Two ways the bridge reaches the EEZ project:
 *   - by name (uivar.c / nav.c, from Esp32_monitor): the menu pages
 *     (tools/obd_ui.py); a variable missing from the UI is ignored
 *   - by generated enum / objects.* (vars.h, screens.h): the live views and
 *     the OBD controllers that existed before (ui_bridge, live_*, diag_view,
 *     pid_select_view)
 */
#ifndef HMI_BRIDGE_H
#define HMI_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

/* ---------------------------------------------------------------- uivar.c
 * EEZ global variables by name, resolved on first use. Setters only write when
 * the value differs, so unchanged texts are not redrawn. */
typedef struct {
    const char *name;
    int32_t     idx;        /* -2 = not resolved yet, -1 = not in the UI */
    uint32_t    hash;       /* last string written */
    bool        written;
} uivar_t;

#define UIVAR(n) { .name = (n), .idx = -2 }

void    uv_str(uivar_t *v, const char *s);
void    uv_strf(uivar_t *v, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void    uv_int(uivar_t *v, int32_t x);
void    uv_bool(uivar_t *v, bool b);
int32_t uv_get_int(uivar_t *v, int32_t dflt);
bool    uv_get_bool(uivar_t *v, bool dflt);
void    uv_get_str(uivar_t *v, char *buf, size_t len);
bool    uv_exists(uivar_t *v);

const char *fmt_duration(uint32_t s, char *buf, size_t len);    /* "1d 02:03:04" */

/* ---------------------------------------------------------------- nav.c
 * Pages by EEZ screen name; the page shown last is restored at power-up. */
void        nav_init(const char *page);     /* stored page name, "" = first */
void        nav_go(const char *page);
void        nav_go_anim(const char *page, lv_scr_load_anim_t anim, uint32_t ms);
const char *nav_current(void);              /* EEZ screen name, lower case */
bool        nav_is(const char *page);

/* ---------------------------------------------------------------- screen.c
 * keyboard for text areas, screen timeout / wake */
void screen_init(uint8_t brightness, uint8_t timeout_idx);
void screen_set_timeout(uint8_t timeout_idx);
void screen_off(void);
bool screen_is_off(void);
void screen_wake(void);
void screen_hold(bool hold);
void screen_poll(void);                     /* 250 ms */

/* brightness.c */
void brightness_init(uint8_t percent);
void brightness_set(uint8_t percent);

/* ---------------------------------------------------------------- menu page views
 * init: once after ui_init(); update: from the bridge timers; `visible` = the
 * page is on screen (expensive texts only then). */
void view_status_update(void);              /* status line, every page */
void view_home_init(void);
void view_home_update(bool visible);
void view_display_init(void);
void view_display_update(bool visible);
void view_display_sd_remount(void);
void view_display_restart(void);
void view_wifi_init(void);
void view_wifi_update(bool visible);
void view_wifi_scan(void);
void view_wifi_connect(void);
void view_wifi_disconnect(void);
void view_wifi_forget(void);
void view_ble_init(void);
void view_ble_update(bool visible);
void view_ble_scan(void);
void view_ble_connect(void);
void view_ble_disconnect(void);
void view_ble_forget(void);
/* CAN page: bus monitor and bus settings (view_can.c) */
void     view_can_init(void);
void     view_can_update(bool visible);
void     view_can_start_stop(void);
void     view_can_reset(void);
void     view_can_prepare_obd(void);        /* before OBD connects: leave listen-only */
uint32_t view_can_bitrate(uint8_t idx);     /* can_bitrate_idx -> bit/s */
/* IO page (view_io.c) */
void view_io_update(bool visible);
void view_io_toggle_do0(void);
void view_io_toggle_do1(void);
void view_io_reset(void);
void view_io_i2c_scan(void);
/* OBD + CAN page options, logging (view_obd.c) */
void view_obd_init(void);
void view_obd_update(void);                 /* 100 ms, ui_bridge */

/* ---------------------------------------------------------------- OBD controllers */

/* afr_value is published as AFR x this, see live_view.c */
#define AFR_GAUGE_SCALE 100

void obd_bridge_init(void);                 /* ui_bridge.c: connection + signal timers */
void live_view_init(void);
void diag_view_init(void);
void diag_view_refresh(void);
void pid_select_view_refresh(void);
void pid_select_view_build(void);           /* Select parameters list, built when the page opens */
void live_values_refresh(void);
void live_values_reset_peaks(void);         /* Race view peak hold */

/* Modal message box with an OK button. */
void view_show_message(const char *title, const char *text);

#endif /* HMI_BRIDGE_H */
