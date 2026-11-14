/*
 * uivar - EEZ global variables by name (see bridge.h).
 * Also provides the name lookup declared in eez_var_table.h.
 */
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <inttypes.h>
#include "bridge.h"
#include "eez_vars.h"
#include "eez_var_table.h"
#include "esp_log.h"

static const char *TAG = "uivar";

bool eez_name_equal(const char *a, const char *b)
{
    while (*a || *b) {
        if (*a == '_') { a++; continue; }
        if (*b == '_') { b++; continue; }
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return true;
}

int32_t eez_var_lookup(const char *name)
{
    for (unsigned i = 0; i < eez_var_table_len; i++) {
        if (eez_name_equal(eez_var_table[i].name, name)) {
            return (int32_t)eez_var_table[i].index;
        }
    }
    return -1;
}

static bool resolve(uivar_t *v)
{
    if (v->idx == -2) {
        v->idx = eez_var_lookup(v->name);
        if (v->idx < 0) {
            ESP_LOGW(TAG, "UI has no variable '%s' (ignored)", v->name);
        }
    }
    return v->idx >= 0;
}

bool uv_exists(uivar_t *v)
{
    return resolve(v);
}

static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h = (h ^ (uint8_t)*s++) * 16777619u;
    }
    return h;
}

void uv_str(uivar_t *v, const char *s)
{
    if (!resolve(v)) {
        return;
    }
    uint32_t h = fnv1a(s ? s : "");
    if (v->written && h == v->hash) {
        return;
    }
    v->hash = h;
    v->written = true;
    eez_var_set_string(v->idx, s);
}

void uv_strf(uivar_t *v, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    uv_str(v, buf);
}

/* Numbers and booleans are compared with the live value: inputs two-way
 * bound to the variable may have changed it since we wrote it. */
void uv_int(uivar_t *v, int32_t x)
{
    if (resolve(v) && eez_var_get_int(v->idx) != x) {
        eez_var_set_int(v->idx, x);
    }
}

void uv_bool(uivar_t *v, bool b)
{
    if (resolve(v) && eez_var_get_bool(v->idx) != b) {
        eez_var_set_bool(v->idx, b);
    }
}

int32_t uv_get_int(uivar_t *v, int32_t dflt)
{
    return resolve(v) ? eez_var_get_int(v->idx) : dflt;
}

bool uv_get_bool(uivar_t *v, bool dflt)
{
    return resolve(v) ? eez_var_get_bool(v->idx) : dflt;
}

void uv_get_str(uivar_t *v, char *buf, size_t len)
{
    if (resolve(v)) {
        eez_var_get_string(v->idx, buf, len);
    } else if (len > 0) {
        buf[0] = '\0';
    }
}

const char *fmt_duration(uint32_t s, char *buf, size_t len)
{
    uint32_t d = s / 86400;
    s %= 86400;
    if (d > 0) {
        snprintf(buf, len, "%" PRIu32 "d %02u:%02u:%02u", d, (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    } else {
        snprintf(buf, len, "%02u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    }
    return buf;
}
