/*
 * Service 01 PID catalogue. Formulas: https://en.wikipedia.org/wiki/OBD-II_PIDs
 *
 * To add a PID: add a signal in vehicle_signals.def (if needed), write a
 * decoder below and add one row to s_pids[]. Nothing else has to change.
 */
#include "obd_pids.h"
#include "obd_pids_priv.h"

#define A d[0]
#define B d[1]
#define AB ((float)((d[0] << 8) | d[1]))

static float dec_percent_a(const uint8_t *d)   { return A * 100.0f / 255.0f; }
static float dec_temp_a(const uint8_t *d)      { return (float)A - 40.0f; }
static float dec_raw_a(const uint8_t *d)       { return (float)A; }
static float dec_raw_ab(const uint8_t *d)      { return AB; }
static float dec_trim(const uint8_t *d)        { return A * 100.0f / 128.0f - 100.0f; }
static float dec_rpm(const uint8_t *d)         { return AB / 4.0f; }
static float dec_timing(const uint8_t *d)      { return A / 2.0f - 64.0f; }
static float dec_maf(const uint8_t *d)         { return AB / 100.0f; }
static float dec_fuel_press(const uint8_t *d)  { return A * 3.0f; }
static float dec_lambda(const uint8_t *d)      { return AB * 2.0f / 65536.0f; }
static float dec_voltage(const uint8_t *d)     { return AB / 1000.0f; }
static float dec_fuel_rate(const uint8_t *d)   { return AB / 20.0f; }

/* PID 01: monitor status. Primary signal = stored DTC count, MIL as side output. */
static float dec_monitor_status(const uint8_t *d)
{
    vd_set(VD_MIL, (A & 0x80) ? 1.0f : 0.0f);
    return (float)(A & 0x7F);
}

const obd_pid_desc_t g_obd_pids[] = {
    /* pid   len rate             signal            decoder */
    { 0x01, 4, OBD_RATE_SLOW,   VD_DTC_COUNT,     dec_monitor_status },
    { 0x04, 1, OBD_RATE_FAST,   VD_ENGINE_LOAD,   dec_percent_a },
    { 0x05, 1, OBD_RATE_SLOW,   VD_COOLANT,       dec_temp_a },
    { 0x06, 1, OBD_RATE_MEDIUM, VD_STFT_B1,       dec_trim },
    { 0x07, 1, OBD_RATE_MEDIUM, VD_LTFT_B1,       dec_trim },
    { 0x08, 1, OBD_RATE_MEDIUM, VD_STFT_B2,       dec_trim },
    { 0x09, 1, OBD_RATE_MEDIUM, VD_LTFT_B2,       dec_trim },
    { 0x0A, 1, OBD_RATE_SLOW,   VD_FUEL_PRESSURE, dec_fuel_press },
    { 0x0B, 1, OBD_RATE_FAST,   VD_MAP,           dec_raw_a },
    { 0x0C, 2, OBD_RATE_FAST,   VD_RPM,           dec_rpm },
    { 0x0D, 1, OBD_RATE_FAST,   VD_SPEED,         dec_raw_a },
    { 0x0E, 1, OBD_RATE_FAST,   VD_TIMING,        dec_timing },
    { 0x0F, 1, OBD_RATE_MEDIUM, VD_INTAKE_TEMP,   dec_temp_a },
    { 0x10, 2, OBD_RATE_FAST,   VD_MAF,           dec_maf },
    { 0x11, 1, OBD_RATE_FAST,   VD_THROTTLE,      dec_percent_a },
    { 0x1F, 2, OBD_RATE_SLOW,   VD_RUNTIME,       dec_raw_ab },
    { 0x24, 4, OBD_RATE_FAST,   VD_LAMBDA,        dec_lambda },
    { 0x2F, 1, OBD_RATE_SLOW,   VD_FUEL_LEVEL,    dec_percent_a },
    { 0x33, 1, OBD_RATE_SLOW,   VD_BARO,          dec_raw_a },
    { 0x34, 4, OBD_RATE_FAST,   VD_LAMBDA,        dec_lambda },
    { 0x42, 2, OBD_RATE_SLOW,   VD_BATTERY,       dec_voltage },
    { 0x44, 2, OBD_RATE_MEDIUM, VD_LAMBDA_CMD,    dec_lambda },
    { 0x45, 1, OBD_RATE_FAST,   VD_THROTTLE_REL,  dec_percent_a },
    { 0x46, 1, OBD_RATE_SLOW,   VD_AMBIENT_TEMP,  dec_temp_a },
    { 0x5C, 1, OBD_RATE_SLOW,   VD_OIL_TEMP,      dec_temp_a },
    { 0x5E, 2, OBD_RATE_MEDIUM, VD_FUEL_RATE,     dec_fuel_rate },
};

const size_t g_obd_pid_count = sizeof(g_obd_pids) / sizeof(g_obd_pids[0]);
