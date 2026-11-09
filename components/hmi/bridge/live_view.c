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
 * Performance: drawing the scales (anti-aliased ticks, tick labels, arcs) is
 * by far the most expensive part, and it was redone for every needle move.
 * After setup, the whole static screen is rendered once into an image
 * (lv_snapshot, ~750 KB in PSRAM). Ticks, meter backgrounds, arcs and labels are
 * then removed from the live widgets, so a frame only blits the image and draws
 * the needles. Consequence: labels/arcs on this screen are static. A widget that
 * must stay live needs to be excluded in is_static_decoration().
 *
 * Navigation: the EEZ screen has no way back, so a swipe (any direction) or a
 * long press anywhere goes back to the previous screen. Gauges and arcs are made
 * non-clickable so touches reach the screen (and the arcs can't be dragged).
 */
#include "lvgl.h"
#include "esp_log.h"
#include "screens.h"
#include "ui.h"
#include "views.h"

static const char *TAG = "live_view";

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

/* ---------------------------------------------------------------- background cache */

static void set_needles_opa(lv_obj_t *meter, lv_opa_t opa)
{
    lv_meter_indicator_t *ind;
    _LV_LL_READ(&((lv_meter_t *)meter)->indicator_ll, ind) {
        ind->opa = opa;
    }
}

static bool is_meter(lv_obj_t *o)  { return lv_obj_check_type(o, &lv_meter_class); }

static bool is_static_decoration(lv_obj_t *o)
{
    return lv_obj_check_type(o, &lv_arc_class) || lv_obj_check_type(o, &lv_label_class);
}

static void bake_background(lv_obj_t *scr)
{
    uint32_t n = lv_obj_get_child_cnt(scr);

    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(scr, i);
        if (is_meter(c)) {
            set_needles_opa(c, LV_OPA_TRANSP);
        }
    }
    lv_obj_update_layout(scr);
    lv_img_dsc_t *bg = lv_snapshot_take(scr, LV_IMG_CF_TRUE_COLOR);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(scr, i);
        if (is_meter(c)) {
            set_needles_opa(c, LV_OPA_COVER);
        }
    }
    if (bg == NULL) {
        ESP_LOGW(TAG, "snapshot failed (out of memory?), gauges drawn the slow way");
        return;
    }

    /* Strip what is now in the image. */
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(scr, i);
        if (is_meter(c)) {
            lv_meter_scale_t *scale;
            _LV_LL_READ(&((lv_meter_t *)c)->scale_ll, scale) {
                scale->tick_cnt = 0;            /* nothing to draw, range is kept */
            }
            lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
            lv_obj_set_style_shadow_width(c, 0, LV_PART_MAIN);
        } else if (is_static_decoration(c)) {
            lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_t *img = lv_img_create(scr);
    lv_img_set_src(img, bg);
    lv_obj_set_pos(img, 0, 0);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_to_index(img, 0);                       /* behind the needles */
    ESP_LOGI(TAG, "gauge background cached (%lux%lu)", (unsigned long)bg->header.w, (unsigned long)bg->header.h);
}

/* ---------------------------------------------------------------- navigation */

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
    bake_background(scr);
}
