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

#define SETTINGS_PAGE_LEN   24

typedef struct {
    uint8_t  conn_method;           /* settings_conn_method_t */
    uint16_t rusefi_base_id;
    bool     log_raw;
    bool     log_params;
    uint8_t  pid_selected[256 / 8]; /* bitmap by PID number, 1 = poll this PID */
    bool     ecu_support_valid;     /* ecu_supported holds the last ECU's answer */
    uint8_t  ecu_supported[256 / 8];/* Service 01 support bitmap of the last ECU */
    /* Fields added later go at the end: a shorter stored blob of the same
     * version is loaded and the new fields keep their defaults. */
    bool     gauge_anim;            /* animate gauge needles between samples */
    uint8_t  pids_per_request;      /* Service 01 multi-PID request size, 1..6 */
    uint8_t  brightness;            /* display brightness, 10..100 % */
    bool     log_all_traffic;       /* raw log: every bus frame, not only OBD */
    bool     wifi_autoconnect;      /* connect to the stored Wi-Fi network at boot */
    char     page[SETTINGS_PAGE_LEN];   /* EEZ screen name of the last page, restored at boot */
    uint8_t  screen_timeout_idx;    /* Display page list index, 0 = never */
    uint8_t  can_bitrate_idx;       /* CAN page: 0 125k, 1 250k, 2 500k, 3 1M */
    uint8_t  obd_link;              /* OBD page: 0 board CAN, 1 BLE ELM327 adapter (obd_link_t) */
    uint16_t dash_base;             /* Dash: bus ID of the DBC's lowest 11-bit message, 0 = as in the DBC */
    char     dash_dbc[32];          /* Dash: DBC file in /sdcard/db/dbc, "" = the first one */
    uint8_t  link_filter;           /* IO page receive filter: 0 auto, 1 all, 2 OBD, 3 Dash DBC, 4 custom */
    uint16_t link_filter_id;        /* custom filter: 11-bit ID ... */
    uint16_t link_filter_mask;      /* ... and mask (1 = bit must match) */
} app_settings_t;

/* Initialises NVS (erasing it if the partition is corrupt/outdated) and loads
 * the stored settings, falling back to defaults. */
esp_err_t settings_init(void);

void settings_get(app_settings_t *out);

/* Copy, change, store (saved later if anything differs):
 *   app_settings_t s; settings_get(&s); s.x = 1; settings_put(&s); */
void settings_put(const app_settings_t *in);

void settings_set_conn_method(uint8_t method);
void settings_set_rusefi_base_id(uint16_t id);
void settings_set_log_raw(bool on);
void settings_set_log_params(bool on);
void settings_set_gauge_anim(bool on);
void settings_set_pids_per_request(uint8_t n);
void settings_set_brightness(uint8_t percent);
void settings_set_log_all_traffic(bool on);
void settings_set_wifi_autoconnect(bool on);
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
