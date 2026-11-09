/*
 * vehicle_data - thread-safe store of the latest value of every vehicle signal.
 *
 * Producers (comsm/OBD) call vd_set(); consumers (UI bridge, logger) read with
 * vd_get() or vd_snapshot(). Every sample carries a timestamp so consumers can
 * show stale data as "--". No dependency on CAN, OBD or LVGL.
 */
#ifndef VEHICLE_DATA_H
#define VEHICLE_DATA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
#define VD_SIGNAL(id, name, unit, min, max, dec) VD_##id,
#include "vehicle_signals.def"
#undef VD_SIGNAL
    VD_COUNT,
    VD_NONE = VD_COUNT
} vd_signal_t;

typedef struct {
    const char *name;
    const char *unit;
    float       min;
    float       max;
    uint8_t     decimals;
} vd_signal_info_t;

typedef struct {
    float    value;
    uint32_t ts_ms;     /* time of last update (ms since boot) */
    bool     valid;
} vd_sample_t;

void vd_init(void);

void vd_set(vd_signal_t sig, float value);
void vd_invalidate(vd_signal_t sig);
void vd_invalidate_all(void);

/* Returns true if the signal holds a valid value not older than max_age_ms
 * (max_age_ms = 0 disables the age check). `out` is always filled. */
bool vd_get(vd_signal_t sig, uint32_t max_age_ms, vd_sample_t *out);

/* Copy all samples at once (consistent view for logging). */
void vd_snapshot(vd_sample_t out[VD_COUNT]);

/* Incremented on every vd_set(); cheap "anything changed?" check. */
uint32_t vd_generation(void);

const vd_signal_info_t *vd_info(vd_signal_t sig);

#ifdef __cplusplus
}
#endif

#endif /* VEHICLE_DATA_H */
