/*
 * rtc_pcf85063 - the board's real-time clock (NXP PCF85063A, I2C 0x51, with a
 * backup cell in the holder on the PCB).
 *
 * Holds UTC. At boot rtc_pcf85063_to_system() sets the system clock from it,
 * so log files get real timestamps without Wi-Fi; after an SNTP sync
 * rtc_pcf85063_from_system() stores the fresh time.
 */
#ifndef RTC_PCF85063_H
#define RTC_PCF85063_H

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Needs i2c_io_init() first. ESP_ERR_NOT_FOUND: no RTC answering. */
esp_err_t rtc_pcf85063_init(void);

/* UTC time from the RTC. ESP_ERR_INVALID_STATE: the oscillator stopped since
 * the time was last set (backup cell empty / removed), the time is not valid. */
esp_err_t rtc_pcf85063_read(time_t *utc);

esp_err_t rtc_pcf85063_write(time_t utc);

/* RTC -> system clock. Returns true if the RTC held a valid time. */
bool rtc_pcf85063_to_system(void);

/* System clock -> RTC (call after SNTP has set the clock). */
esp_err_t rtc_pcf85063_from_system(void);

#ifdef __cplusplus
}
#endif

#endif /* RTC_PCF85063_H */
