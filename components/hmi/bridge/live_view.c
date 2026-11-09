/*
 * live_view - fixes for the live data gauges drawn by EEZ.
 *
 * lv_meter only works with integers, and EEZ evaluates the needle expression as an
 * integer. Each gauge is therefore driven in a finer "raw" unit, and its tick
 * labels are divided back for display:
 *
 *   rpm_gauge  needle = rpm_value (real rpm)  scale 0..9000   labels /100 ("RPM x100")
 *   afr_gauge  needle = afr_value (AFR x10)   scale 100..200  labels /10
 *
 * The scale ranges in the EEZ project (0..90, 10..20) are overridden here after
 * ui_init(), so a regenerated UI keeps working as long as the widgets keep these
 * identifiers.
 *
 * Navigation: the EEZ screen has no way back, so a swipe (any direction) or a
 * long press anywhere goes back to the previous screen. Gauges and arcs are made
 * non-clickable so touches reach the screen (and the arcs can't be dragged).
 */
#include "lvgl.h"
#include "screens.h"
#include "ui.h"
#include "views.h"

extern objects_t objects;

typedef struct {
    int32_t raw_per_label;      /* raw units per displayed label unit */
} gauge_fix_t;

static const gauge_fix_t s_rpm_fix = { .raw_per_label = 100 };
static const gauge_fix_t s_afr_fix = { .raw_per_label = AFR_GAUGE_SCALE };

static void tick_label_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    const gauge_fix_t *fix = lv_event_get_user_data(e);
    if (dsc->type == LV_METER_DRAW_PART_TICK && dsc->text != NULL) {
        /* buffer in lv_meter.c is 16 bytes */
        lv_snprintf(dsc->text, 16, "%" LV_PRId32, dsc->value / fix->raw_per_label);
    }
}

static void rescale(lv_obj_t *meter, const gauge_fix_t *fix)
{
    lv_meter_t *m = (lv_meter_t *)meter;
    lv_meter_scale_t *scale = _lv_ll_get_head(&m->scale_ll);
    if (scale == NULL) {
        return;
    }
    lv_meter_set_scale_range(meter, scale, scale->min * fix->raw_per_label,
                             scale->max * fix->raw_per_label, scale->angle_range, scale->rotation);
    lv_obj_add_event_cb(meter, tick_label_cb, LV_EVENT_DRAW_PART_BEGIN, (void *)fix);
}

static void go_back_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_get_act();
        if (indev == NULL || lv_indev_get_gesture_dir(indev) == LV_DIR_NONE) {
            return;
        }
        lv_indev_wait_release(indev);
    }
    /* Same as the EEZ "back" on other screens; fall back to the start screen
     * if the gauges were not opened via a push (empty screen stack). */
    eez_flow_pop_screen(LV_SCR_LOAD_ANIM_FADE_IN, 200, 0);
    if (eez_flow_get_current_screen() == SCREEN_ID_LIVE_DATA) {
        eez_flow_set_screen(SCREEN_ID_STARTUP, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0);
    }
}

void live_view_init(void)
{
    lv_obj_t *scr = objects.live_data;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(scr); i++) {
        lv_obj_t *child = lv_obj_get_child(scr, i);
        lv_obj_clear_flag(child, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);     /* scrolling would eat the swipe */
    lv_obj_add_event_cb(scr, go_back_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(scr, go_back_cb, LV_EVENT_LONG_PRESSED, NULL);

    rescale(objects.rpm_gauge, &s_rpm_fix);
    rescale(objects.afr_gauge, &s_afr_fix);
}
