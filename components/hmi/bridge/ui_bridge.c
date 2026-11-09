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
#include "vehicle_data.h"
#include "views.h"

static const char *TAG = "ui_bridge";

#define REFRESH_PERIOD_MS   100
#define STALE_AFTER_MS      3000
#define AFR_STOICH_GASOLINE 14.7f

typedef enum { BIND_INT, BIND_FLOAT } bind_type_t;

typedef struct {
    vd_signal_t signal;
    uint32_t    eez_var;
    bind_type_t type;
    float       scale;          /* displayed = value * scale */
    float       last;           /* cache, NAN = never sent */
} binding_t;

static binding_t s_bindings[] = {
    { VD_RPM,    FLOW_GLOBAL_VARIABLE_RPM_VALUE, BIND_INT,   1.0f, NAN },
    /* AFR x10, rounded: the gauge needle is integer only (see live_view.c) */
    { VD_LAMBDA, FLOW_GLOBAL_VARIABLE_AFR_VALUE, BIND_INT,
      AFR_STOICH_GASOLINE * AFR_GAUGE_SCALE, NAN },
    /* After regenerating EEZ code with the new variables, e.g.:
     * { VD_SPEED,   FLOW_GLOBAL_VARIABLE_VEHICLE_SPEED,   BIND_INT,   1.0f, NAN },
     * { VD_COOLANT, FLOW_GLOBAL_VARIABLE_COOLANT_TEMP,    BIND_INT,   1.0f, NAN },
     * { VD_BATTERY, FLOW_GLOBAL_VARIABLE_BATTERY_VOLTAGE, BIND_FLOAT, 1.0f, NAN },
     */
};

static uint32_t s_last_status_change = UINT32_MAX;
static uint32_t s_status_refresh_ms;

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
        snprintf(buf, len, "ECU %03lX - %u/%u PIDs - %.0f/s",
                 (unsigned long)s->ecu_id, obd_pid_active_count(), s->supported_pids, s->request_rate);
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

static void refresh_signals(void)
{
    for (size_t i = 0; i < sizeof(s_bindings) / sizeof(s_bindings[0]); i++) {
        binding_t *b = &s_bindings[i];
        vd_sample_t smp;
        float v = vd_get(b->signal, STALE_AFTER_MS, &smp) ? smp.value * b->scale : 0.0f;

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
    refresh_signals();
    settings_view_refresh();
    diag_view_refresh();
    pid_select_view_refresh();
}

void hmi_init(void)
{
    ui_init();
    settings_view_init();
    live_view_init();
    diag_view_init();
    lv_timer_create(bridge_timer_cb, REFRESH_PERIOD_MS, NULL);
    ESP_LOGI(TAG, "UI bridge started (%u ms refresh)", REFRESH_PERIOD_MS);
}

void hmi_tick(void)
{
    ui_tick();
}
