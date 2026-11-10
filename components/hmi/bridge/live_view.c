/*
 * live_view - runtime additions to the live data screens built in EEZ Studio
 * (Drive, Race = "liveData", Tune). Layout lives in the EEZ project; this file
 * only adds what EEZ can't express.
 *
 * Gauge scaling: lv_meter only works with integers, and EEZ evaluates the needle
 * expression as an integer. Each gauge is therefore driven in a finer "raw"
 * unit, and its tick labels are divided back for display:
 *
 *   drive_rpm_gauge  needle = rpm_value (rpm)      EEZ scale 0..8   x1000  labels 0..8
 *   rpm_gauge        needle = rpm_value (rpm)      EEZ scale 0..90  x100   labels 0..90
 *   afr_gauge        needle = afr_value (AFR x10)  EEZ scale 10..20 x10    labels 10..20
 *
 * Performance: drawing a meter (anti-aliased ticks, tick labels, arcs) is the
 * expensive part. On screens with a meter, the static content is rendered once
 * into an image (lv_snapshot, ~750 KB PSRAM per screen); a frame then only blits
 * the image and draws the needles. "Static content" = meter scales plus arcs
 * and labels placed directly on the screen and lying completely inside a meter. Values
 * shown on top of a meter must therefore sit inside a panel (as the EEZ tiles
 * do) - a bare label there would be frozen.
 *
 * Navigation: swipe left / right cycles Drive -> Race -> Tune; swipe up / down
 * or a long press returns to the start screen. All children are made
 * non-clickable so touches reach the screen.
 */
#include "lvgl.h"
#include "esp_log.h"
#include "screens.h"
#include "ui.h"
#include "views.h"

static const char *TAG = "live_view";

extern objects_t objects;

#define NAV_ANIM_MS     200

/* Swipe order. */
static const int16_t s_pages[] = { SCREEN_ID_DRIVE, SCREEN_ID_LIVE_DATA, SCREEN_ID_TUNE };
#define PAGE_COUNT  (sizeof(s_pages) / sizeof(s_pages[0]))

/* ---------------------------------------------------------------- gauge scaling */

typedef struct {
    int32_t raw_per_label;      /* raw units per displayed label unit */
} gauge_fix_t;

static const gauge_fix_t s_rpm1000_fix = { .raw_per_label = 1000 };
static const gauge_fix_t s_rpm100_fix  = { .raw_per_label = 100 };
static const gauge_fix_t s_afr_fix     = { .raw_per_label = AFR_GAUGE_SCALE };

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

static bool is_meter(lv_obj_t *o)  { return lv_obj_check_type(o, &lv_meter_class); }

static void set_needles_opa(lv_obj_t *meter, lv_opa_t opa)
{
    lv_meter_indicator_t *ind;
    _LV_LL_READ(&((lv_meter_t *)meter)->indicator_ll, ind) {
        ind->opa = opa;
    }
}

/* Arc or label lying completely inside a meter: drawn into the cached background. */
static bool is_meter_decoration(lv_obj_t *scr, lv_obj_t *o)
{
    if (!lv_obj_check_type(o, &lv_arc_class) && !lv_obj_check_type(o, &lv_label_class)) {
        return false;
    }
    lv_area_t a, m;
    lv_obj_get_coords(o, &a);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(scr); i++) {
        lv_obj_t *c = lv_obj_get_child(scr, i);
        if (is_meter(c)) {
            lv_obj_get_coords(c, &m);
            if (_lv_area_is_in(&a, &m, 0)) {
                return true;
            }
        }
    }
    return false;
}

typedef enum { ROLE_METER, ROLE_DECORATION, ROLE_LIVE } role_t;

static role_t role_of(lv_obj_t *scr, lv_obj_t *o)
{
    if (is_meter(o)) {
        return ROLE_METER;
    }
    return is_meter_decoration(scr, o) ? ROLE_DECORATION : ROLE_LIVE;
}

static void bake_background(lv_obj_t *scr)
{
    uint32_t n = lv_obj_get_child_cnt(scr);
    bool has_meter = false;

    /* Snapshot only meters (without needles) and their decorations. */
    lv_obj_update_layout(scr);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(scr, i);
        switch (role_of(scr, c)) {
        case ROLE_METER:      set_needles_opa(c, LV_OPA_TRANSP); has_meter = true; break;
        case ROLE_LIVE:       lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN); break;
        case ROLE_DECORATION: break;
        }
    }
    lv_img_dsc_t *bg = has_meter ? lv_snapshot_take(scr, LV_IMG_CF_TRUE_COLOR) : NULL;

    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(scr, i);
        role_t r = role_of(scr, c);
        if (r == ROLE_LIVE) {
            /* EEZ may hide some of these itself (e.g. MIL): its tick re-evaluates them. */
            lv_obj_clear_flag(c, LV_OBJ_FLAG_HIDDEN);
        } else if (r == ROLE_METER) {
            set_needles_opa(c, LV_OPA_COVER);
            if (bg != NULL) {
                lv_meter_scale_t *scale;
                _LV_LL_READ(&((lv_meter_t *)c)->scale_ll, scale) {
                    scale->tick_cnt = 0;            /* in the image now; range is kept */
                }
                lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
                lv_obj_set_style_shadow_width(c, 0, LV_PART_MAIN);
            }
        } else if (bg != NULL) {
            lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!has_meter) {
        return;
    }
    if (bg == NULL) {
        ESP_LOGW(TAG, "snapshot failed (out of memory?), gauges drawn the slow way");
        return;
    }

    lv_obj_t *img = lv_img_create(scr);
    lv_img_set_src(img, bg);
    lv_obj_set_pos(img, 0, 0);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_to_index(img, 0);                       /* behind everything */
    ESP_LOGI(TAG, "gauge background cached (%lux%lu)", (unsigned long)bg->header.w, (unsigned long)bg->header.h);
}

/* ---------------------------------------------------------------- navigation */

static void go_to_page(int dir)
{
    int16_t cur = eez_flow_get_current_screen();
    int idx = 0;
    for (int i = 0; i < (int)PAGE_COUNT; i++) {
        if (s_pages[i] == cur) {
            idx = i;
        }
    }
    idx = (idx + dir + PAGE_COUNT) % PAGE_COUNT;
    eez_flow_set_screen(s_pages[idx], dir > 0 ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                        NAV_ANIM_MS, 0);
}

static void nav_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_dir_t dir = indev ? lv_indev_get_gesture_dir(indev) : LV_DIR_NONE;
        if (dir == LV_DIR_NONE) {
            return;
        }
        lv_indev_wait_release(indev);
        if (dir == LV_DIR_LEFT) {
            go_to_page(+1);
            return;
        }
        if (dir == LV_DIR_RIGHT) {
            go_to_page(-1);
            return;
        }
    }
    eez_flow_set_screen(SCREEN_ID_STARTUP, LV_SCR_LOAD_ANIM_FADE_IN, NAV_ANIM_MS, 0);
}

static void make_passive(lv_obj_t *o)
{
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) {
        lv_obj_t *c = lv_obj_get_child(o, i);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        make_passive(c);
    }
}

static void setup_page(lv_obj_t *scr)
{
    make_passive(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);     /* scrolling would eat the swipe */
    lv_obj_add_event_cb(scr, nav_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(scr, nav_cb, LV_EVENT_LONG_PRESSED, NULL);
    bake_background(scr);
}

void live_view_init(void)
{
    rescale(objects.drive_rpm_gauge, &s_rpm1000_fix);
    rescale(objects.rpm_gauge, &s_rpm100_fix);
    rescale(objects.afr_gauge, &s_afr_fix);

    setup_page(objects.drive);
    setup_page(objects.live_data);
    setup_page(objects.tune);
}
