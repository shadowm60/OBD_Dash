/*
 * screen - things every page needs that EEZ bindings can't express:
 *
 *   keyboard   one lv_keyboard on the top layer, shown whenever any text area
 *              of any page gets the focus (found by type, not by name)
 *   timeout    backlight off (CH422G EXIO2) after the inactivity time chosen
 *              on the Display page; the touch that wakes the screen is
 *              swallowed so it can't press a button by accident;
 *              screen_wake() / screen_hold() let the Route page switch it on
 *              before a turn and keep it on until the turn is passed
 *   brightness brightness.c
 */
#include "lvgl.h"
#include "esp_log.h"
#include "bridge.h"
#include "i2c_io.h"

static const char *TAG = "screen";

/* index = disp_timeout_sel on the Display page */
static const uint32_t TIMEOUT_MS[] = { 0, 30000, 60000, 300000, 600000 };
#define N_TIMEOUTS (sizeof(TIMEOUT_MS) / sizeof(TIMEOUT_MS[0]))

static lv_obj_t *s_kb;
static lv_obj_t *s_blocker;     /* exists while the screen is off */
static uint32_t  s_timeout_ms;
static bool      s_hold;        /* no timeout while set */

/* ---------------------------------------------------------------- keyboard */

static void kb_hide(void)
{
    lv_obj_t *ta = lv_keyboard_get_textarea(s_kb);
    lv_keyboard_set_textarea(s_kb, NULL);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    if (ta != NULL) {
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    }
}

static void ta_event_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_FOCUSED:
    case LV_EVENT_CLICKED:
        lv_keyboard_set_textarea(s_kb, ta);
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        break;
    case LV_EVENT_DEFOCUSED:
        kb_hide();
        break;
    default:
        break;
    }
}

static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) {
        kb_hide();
    }
}

static void hook_textareas(lv_obj_t *o)
{
    if (lv_obj_check_type(o, &lv_textarea_class)) {
        lv_obj_add_event_cb(o, ta_event_cb, LV_EVENT_ALL, NULL);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) {
        hook_textareas(lv_obj_get_child(o, i));
    }
}

static void kb_init(void)
{
    s_kb = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_kb, LV_PCT(100), 220);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_ALL, NULL);

    /* every screen EEZ created */
    lv_disp_t *d = lv_disp_get_default();
    for (uint32_t i = 0; i < d->screen_cnt; i++) {
        hook_textareas(d->screens[i]);
    }
}

/* ---------------------------------------------------------------- timeout */

static void blocker_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_PRESSED) {
        wavesahre_rgb_lcd_bl_on();
        ESP_LOGI(TAG, "screen on");
    } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
        lv_obj_del_async(s_blocker);
        s_blocker = NULL;
    }
}

void screen_off(void)
{
    if (s_blocker != NULL) {
        return;
    }
    kb_hide();
    /* transparent, clickable, above everything: eats the waking touch */
    s_blocker = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_blocker);
    lv_obj_set_size(s_blocker, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_blocker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_blocker, blocker_cb, LV_EVENT_ALL, NULL);
    wavesahre_rgb_lcd_bl_off();
    ESP_LOGI(TAG, "screen off");
}

bool screen_is_off(void)
{
    return s_blocker != NULL;
}

void screen_wake(void)
{
    if (s_blocker != NULL) {
        lv_obj_del(s_blocker);
        s_blocker = NULL;
        wavesahre_rgb_lcd_bl_on();
        ESP_LOGI(TAG, "screen on (woken by the app)");
    }
    lv_disp_trig_activity(NULL);
}

void screen_hold(bool hold)
{
    if (s_hold && !hold) {
        lv_disp_trig_activity(NULL);    /* the timeout counts from the release */
    }
    s_hold = hold;
}

void screen_set_timeout(uint8_t idx)
{
    s_timeout_ms = idx < N_TIMEOUTS ? TIMEOUT_MS[idx] : 0;
}

void screen_poll(void)
{
    if (s_timeout_ms != 0 && !s_hold && s_blocker == NULL && lv_disp_get_inactive_time(NULL) > s_timeout_ms) {
        screen_off();
    }
}

void screen_init(uint8_t brightness, uint8_t timeout_idx)
{
    kb_init();
    brightness_init(brightness);    /* the shade goes above the keyboard: dims it too */
    screen_set_timeout(timeout_idx);
}
