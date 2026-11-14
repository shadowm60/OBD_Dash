/*
 * io_svc - the board's isolated IO on the green terminal block, the I2C bus
 * scan and the chip temperature (IO page).
 *
 *   DI0 / DI1  optocoupler inputs, 5-36 V between DIN0/1 and DI COM (either
 *              polarity: NPN or PNP sensors, dry contacts). Read through the
 *              CH422G (EXIO0 / EXIO5) every 100 ms, but only while
 *              io_svc_set_fast(true) (IO page open): each read releases all
 *              CH422G IO pins for ~0.3 ms (SD CS, LCD reset, backlight enable,
 *              see board/i2c_io.c). Active = current flows.
 *   DO0 / DO1  optocoupler-isolated open-drain outputs, 5-36 V, max 450 mA
 *              each (CH422G OC0 / OC1). Off at power-up, not stored.
 *
 * All calls are non-blocking and thread safe; poll io_svc_get_status().
 */
#ifndef IO_SVC_H
#define IO_SVC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     di_ok;             /* last read worked */
    bool     di[2];             /* true = input active */
    uint32_t di_changes[2];     /* since start / io_svc_reset() */
    uint32_t di_last_change_s[2];   /* uptime s of the last change, 0 = none */
    bool     do_on[2];
    bool     temp_ok;
    float    chip_temp_c;
    bool     i2c_scanning;
    uint32_t i2c_scan_count;    /* increments with every finished scan */
    char     i2c_result[160];
} io_svc_status_t;

esp_err_t io_svc_init(void);

void io_svc_set_fast(bool fast);
esp_err_t io_svc_set_do(int n, bool on);
void io_svc_reset(void);            /* DI change counters */
esp_err_t io_svc_i2c_scan(void);    /* async, see i2c_scan_count */

void io_svc_get_status(io_svc_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* IO_SVC_H */
