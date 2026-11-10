/*
 * ui_bridge - the only place that maps application data to EEZ variables.
 *
 * Runs as an lv_timer inside the LVGL task: it pulls a snapshot from comsm and
 * vehicle_data and pushes changed values into EEZ global variables. Producers
 * never touch LVGL, so the bus side can't block (or crash) the UI.
 *
 * When new variables are added in EEZ Studio and the code is regenerated,
 * bind them by adding a row to s_bindings[] below.
 */
#include <stdio.h>
#include <math.h>
#include "lvgl.h"
#include "esp_log.h"
#include "ui.h"
#include "vars.h"
#include "hmi.h"
#include "eez_vars.h"
#include "comsm.h"
#include "obd_pids.h"
#include "datalog.h"
#include "vehicle_data.h"
#include "settings.h"
#include "views.h"

static const char *TAG = "ui_bridge";

#define REFRESH_PERIOD_MS   100
#define SIGNAL_PERIOD_MS    20      /* needle animation runs at this rate */
#define ANIM_MIN_MS         60      /* needle travel time limits per sample */
#define ANIM_MAX_MS         600
#define ANIM_DROP_MS        300     /* travel to 0 when a value goes stale */
#define STALE_AFTER_MS      10000   /* safety net: disconnect / deselect invalidate at once */
#define AFR_STOICH_GASOLINE 14.7f

typedef enum { BIND_INT, BIND_FLOAT } bind_type_t;

typedef struct {
    vd_signal_t signal;
    uint32_t    eez_var;
    bind_type_t type;
    float       scale;          /* displayed = value * scale */
    float       step;           /* > 0: needle smoothing, output quantised to this step */
    float       last;           /* cache, NAN = never sent */
    /* needle animation (step > 0) */
    float       shown;          /* current needle value, NAN = none yet */
    float       from, to;       /* current segment */
    uint32_t    seg_start, seg_ms;
    uint32_t    sample_ts;      /* ts of the sample `to` came from, 0 = none */
    float       interval_ms;    /* average time between samples */
} binding_t;

static binding_t s_bindings[] = {
    { .signal = VD_RPM, .eez_var = FLOW_GLOBAL_VARIABLE_RPM_VALUE, .type = BIND_INT,
      .scale = 1.0f, .step = 10.0f, .last = NAN, .shown = NAN },
    /* AFR x100, rounded: the gauge needle is integer only (see live_view.c) */
    { .signal = VD_LAMBDA, .eez_var = FLOW_GLOBAL_VARIABLE_AFR_VALUE, .type = BIND_INT,
      .scale = AFR_STOICH_GASOLINE * AFR_GAUGE_SCALE, .step = 1.0f, .last = NAN, .shown = NAN },
    /* After regenerating EEZ code with the new variables, e.g.:
     * { .signal = VD_SPEED, .eez_var = FLOW_GLOBAL_VARIABLE_VEHICLE_SPEED, .type = BIND_INT,
     *   .scale = 1.0f, .last = NAN, .shown = NAN },
     * step > 0 only for needles; numeric readouts should show the real value.
     */
};

static uint32_t s_last_status_change = UINT32_MAX;
static uint32_t s_status_refresh_ms;

static bool logging_active(void)
{
    datalog_status_t ls;
    datalog_get_status(&ls);
    return ls.raw_active || ls.params_active;
}

static const char *button_text(comsm_state_t st)
{
    switch (st) {
        case COMSM_IDLE:    return "Connect";
        case COMSM_RUNNING: return "Disconnect";
        default:            return "Cancel";
    }
}

static void format_status(const comsm_status_t *s, char *buf, size_t len)
{
    switch (s->state) {
    case COMSM_IDLE:
        snprintf(buf, len, "Not connected");
        break;
    case COMSM_CONNECTING:
        snprintf(buf, len, "Connecting...");
        break;
    case COMSM_DISCOVERY:
        snprintf(buf, len, "ECU %03lX - reading supported PIDs", (unsigned long)s->ecu_id);
        break;
    case COMSM_RUNNING:
        snprintf(buf, len, "ECU %03lX - %u/%u PIDs - %.0f values/s%s",
                 (unsigned long)s->ecu_id, obd_pid_active_count(), s->supported_pids, s->request_rate,
                 logging_active() ? " - REC" : "");
        break;
    case COMSM_RETRY_WAIT:
        snprintf(buf, len, "No response - retrying");
        break;
    }
}

static void refresh_connection(uint32_t now)
{
    comsm_status_t s;
    comsm_get_status(&s);

    bool changed = s.change_count != s_last_status_change;
    bool periodic = (s.state == COMSM_RUNNING) && (now - s_status_refresh_ms >= 1000);
    if (!changed && !periodic) {
        return;
    }
    s_last_status_change = s.change_count;
    s_status_refresh_ms = now;

    char text[64];
    format_status(&s, text, sizeof(text));
    eez_var_set_string(FLOW_GLOBAL_VARIABLE_CONN_STATUS, text);
    if (changed) {
        eez_var_set_string(FLOW_GLOBAL_VARIABLE_CONN_BUTTON_TEXT, button_text(s.state));
    }
}

/*
 * Needle animation. Each new sample starts a linear move from where the needle
 * is now to the new value, lasting about as long as the gap between samples.
 * The needle so arrives roughly when the next sample does and keeps moving
 * instead of jump - stop - jump. Cost: the needle lags by up to one sample
 * interval. With animation off the needle shows each sample as it arrives.
 */
static float animate(binding_t *b, bool valid, const vd_sample_t *smp, float target, uint32_t now)
{
    bool new_seg = false;
    uint32_t seg_ms = ANIM_DROP_MS;

    if (valid && smp->ts_ms != b->sample_ts) {
        if (b->sample_ts != 0) {
            float gap = (float)(smp->ts_ms - b->sample_ts);
            b->interval_ms = (b->interval_ms > 0.0f) ? b->interval_ms * 0.7f + gap * 0.3f : gap;
        }
        b->sample_ts = smp->ts_ms;
        seg_ms = (uint32_t)b->interval_ms;
        new_seg = true;
    } else if (!valid && b->sample_ts != 0) {
        b->sample_ts = 0;               /* stale / disconnected: drop to 0 */
        new_seg = true;
    }

    if (isnan(b->shown)) {
        b->shown = target;
        b->to = target;
        return target;
    }
    if (new_seg) {
        b->from = b->shown;
        b->to = target;
        b->seg_start = now;
        b->seg_ms = seg_ms < ANIM_MIN_MS ? ANIM_MIN_MS : (seg_ms > ANIM_MAX_MS ? ANIM_MAX_MS : seg_ms);
    }
    uint32_t t = now - b->seg_start;
    b->shown = (t >= b->seg_ms) ? b->to : b->from + (b->to - b->from) * (float)t / (float)b->seg_ms;
    return b->shown;
}

static void refresh_signals(void)
{
    app_settings_t cfg;
    settings_get(&cfg);
    uint32_t now = lv_tick_get();

    for (size_t i = 0; i < sizeof(s_bindings) / sizeof(s_bindings[0]); i++) {
        binding_t *b = &s_bindings[i];
        vd_sample_t smp;
        bool valid = vd_get(b->signal, STALE_AFTER_MS, &smp);
        float v = valid ? smp.value * b->scale : 0.0f;

        if (b->step > 0.0f) {
            if (cfg.gauge_anim) {
                v = animate(b, valid, &smp, v, now);
            } else {
                b->shown = NAN;         /* restart cleanly when switched back on */
                b->sample_ts = 0;
            }
            /* quantise: no redraw for sub-pixel needle moves */
            v = roundf(v / b->step) * b->step;
        }
        if (b->type == BIND_INT) {
            v = roundf(v);
        }
        if (v == b->last) {
            continue;
        }
        b->last = v;
        if (b->type == BIND_INT) {
            eez_var_set_int(b->eez_var, (int32_t)v);
        } else {
            eez_var_set_float(b->eez_var, v);
        }
    }
}

static void bridge_timer_cb(lv_timer_t *t)
{
    uint32_t now = lv_tick_get();
    refresh_connection(now);
    settings_view_refresh();
    diag_view_refresh();
    pid_select_view_refresh();
    live_values_refresh();
}

static void signal_timer_cb(lv_timer_t *t)
{
    refresh_signals();
}

void hmi_init(void)
{
    ui_init();
    settings_view_init();
    live_view_init();
    diag_view_init();
    lv_timer_create(bridge_timer_cb, REFRESH_PERIOD_MS, NULL);
    lv_timer_create(signal_timer_cb, SIGNAL_PERIOD_MS, NULL);
    ESP_LOGI(TAG, "UI bridge started (%u ms refresh)", REFRESH_PERIOD_MS);
}

void hmi_tick(void)
{
    ui_tick();
}
