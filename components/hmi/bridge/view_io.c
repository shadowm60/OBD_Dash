/*
 * IO page: isolated inputs DI0 / DI1, outputs DO0 / DO1, I2C scan, chip
 * temperature (io_svc).
 *   io_di0_text, io_di1_text, io_di_info, io_do0_text, io_do1_text,
 *   io_chip_text, i2c_text
 * The inputs are only read while this page is open (io_svc_set_fast).
 */
#include <stdio.h>
#include <inttypes.h>
#include "esp_timer.h"
#include "bridge.h"
#include "io_svc.h"

static uivar_t v_di0 = UIVAR("io_di0_text");
static uivar_t v_di1 = UIVAR("io_di1_text");
static uivar_t v_info = UIVAR("io_di_info");
static uivar_t v_do0 = UIVAR("io_do0_text");
static uivar_t v_do1 = UIVAR("io_do1_text");
static uivar_t v_chip = UIVAR("io_chip_text");
static uivar_t v_i2c = UIVAR("i2c_text");

static void ago(char *buf, size_t len, uint32_t at_s)
{
    if (at_s == 0) {
        snprintf(buf, len, "-");
        return;
    }
    char d[24];
    fmt_duration((uint32_t)(esp_timer_get_time() / 1000000) - at_s, d, sizeof(d));
    snprintf(buf, len, "%s ago", d);
}

void view_io_update(bool visible)
{
    io_svc_set_fast(visible);
    if (!visible) {
        return;
    }
    io_svc_status_t st;
    io_svc_get_status(&st);
    if (st.di_ok) {
        uv_strf(&v_di0, "DI0:  %s", st.di[0] ? LV_SYMBOL_OK " ACTIVE" : "off");
        uv_strf(&v_di1, "DI1:  %s", st.di[1] ? LV_SYMBOL_OK " ACTIVE" : "off");
    } else {
        uv_str(&v_di0, "DI0:  ?");
        uv_str(&v_di1, "DI1:  ? (read failed)");
    }
    char a0[40], a1[40];
    ago(a0, sizeof(a0), st.di_last_change_s[0]);
    ago(a1, sizeof(a1), st.di_last_change_s[1]);
    uv_strf(&v_info, "DI0: %" PRIu32 " changes, last %s\nDI1: %" PRIu32 " changes, last %s\n"
            "Read every 100 ms while this page is open.",
            st.di_changes[0], a0, st.di_changes[1], a1);
    uv_strf(&v_do0, "DO0:  %s", st.do_on[0] ? "ON  (tap: off)" : "off  (tap: on)");
    uv_strf(&v_do1, "DO1:  %s", st.do_on[1] ? "ON  (tap: off)" : "off  (tap: on)");
    if (st.temp_ok) {
        uv_strf(&v_chip, "Chip temperature:  %.1f C", st.chip_temp_c);
    } else {
        uv_str(&v_chip, "Chip temperature:  -");
    }
    uv_str(&v_i2c, st.i2c_scanning ? "scanning..." : st.i2c_result);
}

static void toggle(int n)
{
    io_svc_status_t st;
    io_svc_get_status(&st);
    io_svc_set_do(n, !st.do_on[n]);
}

void view_io_toggle_do0(void) { toggle(0); }
void view_io_toggle_do1(void) { toggle(1); }
void view_io_reset(void)      { io_svc_reset(); }
void view_io_i2c_scan(void)   { io_svc_i2c_scan(); }
