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
 */
#include "lvgl.h"
#include "screens.h"
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

void live_view_init(void)
{
    rescale(objects.rpm_gauge, &s_rpm_fix);
    rescale(objects.afr_gauge, &s_afr_fix);
}
