/*
 * live_values - text values for the Drive / Race / Tune screens.
 *
 * Every value tile in EEZ shows a string variable (v_*). This file formats the
 * vehicle signals into those strings: fixed decimals, "--" when the value is
 * not polled or stale. Strings are only pushed to EEZ when the text changes.
 *
 * To show another value: add a string variable + tile in EEZ Studio, rebuild
 * the UI (tools/eez-build.sh) and add a row to s_values[].
 */
#include <stdio.h>
#include <string.h>
#include "vars.h"
#include "eez_vars.h"
#include "vehicle_data.h"
#include "views.h"

#define STALE_AFTER_MS      10000   /* safety net: disconnect / deselect invalidate at once */
#define AFR_STOICH          14.7f
#define BARO_DEFAULT_KPA    101.3f  /* boost reference when PID 33 is not polled */

typedef bool (*value_fn_t)(float *out);

typedef struct {
    uint32_t    var;            /* FLOW_GLOBAL_VARIABLE_V_* */
    vd_signal_t signal;         /* used when fn == NULL */
    value_fn_t  fn;             /* derived values */
    uint8_t     decimals;
    char        last[12];
} live_value_t;

static bool get(vd_signal_t sig, float *out)
{
    vd_sample_t s;
    if (!vd_get(sig, STALE_AFTER_MS, &s)) {
        return false;
    }
    *out = s.value;
    return true;
}

static bool afr(float *out)
{
    float l;
    if (!get(VD_LAMBDA, &l)) {
        return false;
    }
    *out = l * AFR_STOICH;
    return true;
}

static bool afr_cmd(float *out)
{
    float l;
    if (!get(VD_LAMBDA_CMD, &l)) {
        return false;
    }
    *out = l * AFR_STOICH;
    return true;
}

/* Boost in bar relative to ambient: MAP - BARO (PID 0B - PID 33). */
static bool boost(float *out)
{
    float map, baro;
    if (!get(VD_MAP, &map)) {
        return false;
    }
    if (!get(VD_BARO, &baro)) {
        baro = BARO_DEFAULT_KPA;
    }
    *out = (map - baro) / 100.0f;
    return true;
}

#define SIG(var, sig, dec)  { FLOW_GLOBAL_VARIABLE_##var, VD_##sig, NULL, dec, "" }
#define FN(var, fn, dec)    { FLOW_GLOBAL_VARIABLE_##var, VD_NONE, fn, dec, "" }

static live_value_t s_values[] = {
    SIG(V_RPM,        RPM,         0),
    SIG(V_SPEED,      SPEED,       0),
    SIG(V_LOAD,       ENGINE_LOAD, 0),
    SIG(V_THROTTLE,   THROTTLE,    0),
    SIG(V_MAP,        MAP,         0),
    FN (V_BOOST,      boost,       2),
    SIG(V_MAF,        MAF,         1),
    SIG(V_TIMING,     TIMING,      1),
    SIG(V_COOLANT,    COOLANT,     0),
    SIG(V_IAT,        INTAKE_TEMP, 0),
    SIG(V_OIL,        OIL_TEMP,    0),
    SIG(V_FUEL_LEVEL, FUEL_LEVEL,  0),
    SIG(V_BATTERY,    BATTERY,     1),
    SIG(V_STFT1,      STFT_B1,     1),
    SIG(V_LTFT1,      LTFT_B1,     1),
    SIG(V_STFT2,      STFT_B2,     1),
    SIG(V_LTFT2,      LTFT_B2,     1),
    FN (V_AFR,        afr,         1),
    FN (V_AFR_CMD,    afr_cmd,     1),
    SIG(V_FUEL_RATE,  FUEL_RATE,   1),
};

static int  s_mil = -1;         /* -1 = never sent */
static char s_mil_text[24];

static void refresh_mil(void)
{
    float mil, dtcs;
    int on = get(VD_MIL, &mil) && mil >= 0.5f;
    if (on != s_mil) {
        s_mil = on;
        eez_var_set_bool(FLOW_GLOBAL_VARIABLE_MIL_ON, on);
    }

    char text[sizeof(s_mil_text)];
    if (get(VD_DTC_COUNT, &dtcs) && dtcs >= 1.0f) {
        snprintf(text, sizeof(text), "CHECK ENGINE (%d)", (int)dtcs);
    } else {
        snprintf(text, sizeof(text), "CHECK ENGINE");
    }
    if (strcmp(text, s_mil_text) != 0) {
        strcpy(s_mil_text, text);
        eez_var_set_string(FLOW_GLOBAL_VARIABLE_MIL_TEXT, text);
    }
}

void live_values_refresh(void)
{
    for (size_t i = 0; i < sizeof(s_values) / sizeof(s_values[0]); i++) {
        live_value_t *v = &s_values[i];
        float x;
        bool ok = v->fn ? v->fn(&x) : get(v->signal, &x);

        char text[sizeof(v->last)];
        if (ok) {
            snprintf(text, sizeof(text), "%.*f", v->decimals, x);
            if (strcmp(text, "-0") == 0 || strcmp(text, "-0.0") == 0 || strcmp(text, "-0.00") == 0) {
                memmove(text, text + 1, strlen(text));      /* no "-0.0" flicker */
            }
        } else {
            strcpy(text, "--");
        }
        if (strcmp(text, v->last) != 0) {
            strcpy(v->last, text);
            eez_var_set_string(v->var, text);
        }
    }
    refresh_mil();
}
