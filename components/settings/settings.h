/*
 * settings - persistent user configuration (NVS).
 *
 * All values live in one versioned blob. Setters update RAM immediately and
 * schedule a save SAVE_DELAY_MS later, so rapid UI changes (e.g. +/- taps)
 * cause a single flash write. Thread safe.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SETTINGS_CONN_OBD    = 0,
    SETTINGS_CONN_RUSEFI = 1,
} settings_conn_method_t;

typedef struct {
    uint8_t  conn_method;           /* settings_conn_method_t */
    uint16_t rusefi_base_id;
    bool     log_raw;
    bool     log_params;
    uint8_t  pid_selected[256 / 8]; /* bitmap by PID number, 1 = poll this PID */
    bool     ecu_support_valid;     /* ecu_supported holds the last ECU's answer */
    uint8_t  ecu_supported[256 / 8];/* Service 01 support bitmap of the last ECU */
} app_settings_t;

/* Initialises NVS (erasing it if the partition is corrupt/outdated) and loads
 * the stored settings, falling back to defaults. */
esp_err_t settings_init(void);

void settings_get(app_settings_t *out);

void settings_set_conn_method(uint8_t method);
void settings_set_rusefi_base_id(uint16_t id);
void settings_set_log_raw(bool on);
void settings_set_log_params(bool on);
/* PID selection (Settings > Select Params). Opt-in: only selected PIDs are polled. */
void settings_set_pid_selected(uint8_t pid, bool selected);
bool settings_pid_selected(uint8_t pid);

/* Support bitmap of the last connected ECU, so the PID list can be filtered
 * while disconnected. get returns false if no ECU has been seen yet. */
void settings_set_ecu_supported(const uint8_t bitmap[256 / 8]);
bool settings_get_ecu_supported(uint8_t bitmap[256 / 8]);

/* Write pending changes now (e.g. before restart). */
esp_err_t settings_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_H */
