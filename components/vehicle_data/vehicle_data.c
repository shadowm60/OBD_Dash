#include "vehicle_data.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"

static const vd_signal_info_t s_info[VD_COUNT] = {
#define VD_SIGNAL(id, n, u, mn, mx, dec) [VD_##id] = { .name = n, .unit = u, .min = mn, .max = mx, .decimals = dec },
#include "vehicle_signals.def"
#undef VD_SIGNAL
};

static vd_sample_t  s_samples[VD_COUNT];
static uint32_t     s_generation;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void vd_init(void)
{
    vd_invalidate_all();
}

void vd_set(vd_signal_t sig, float value)
{
    if (sig >= VD_COUNT) {
        return;
    }
    uint32_t ts = now_ms();
    portENTER_CRITICAL(&s_lock);
    s_samples[sig].value = value;
    s_samples[sig].ts_ms = ts;
    s_samples[sig].valid = true;
    s_generation++;
    portEXIT_CRITICAL(&s_lock);
}

void vd_invalidate(vd_signal_t sig)
{
    if (sig >= VD_COUNT) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_samples[sig].valid = false;
    s_generation++;
    portEXIT_CRITICAL(&s_lock);
}

void vd_invalidate_all(void)
{
    portENTER_CRITICAL(&s_lock);
    for (int i = 0; i < VD_COUNT; i++) {
        s_samples[i].valid = false;
    }
    s_generation++;
    portEXIT_CRITICAL(&s_lock);
}

bool vd_get(vd_signal_t sig, uint32_t max_age_ms, vd_sample_t *out)
{
    if (sig >= VD_COUNT || out == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    *out = s_samples[sig];
    portEXIT_CRITICAL(&s_lock);

    if (!out->valid) {
        return false;
    }
    if (max_age_ms != 0 && (now_ms() - out->ts_ms) > max_age_ms) {
        return false;
    }
    return true;
}

void vd_snapshot(vd_sample_t out[VD_COUNT])
{
    portENTER_CRITICAL(&s_lock);
    memcpy(out, s_samples, sizeof(s_samples));
    portEXIT_CRITICAL(&s_lock);
}

uint32_t vd_generation(void)
{
    return s_generation;
}

const vd_signal_info_t *vd_info(vd_signal_t sig)
{
    return (sig < VD_COUNT) ? &s_info[sig] : NULL;
}
