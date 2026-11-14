/*
 * dash_svc - decodes a CAN broadcast (rusEFI, or any ECU) with a DBC file
 * from the SD card, for the Dash page.
 *
 * The DBC is the first *.dbc in /sdcard/db/dbc/, else in /sdcard/ (loaded at
 * dash_svc_load(), e.g. when the Dash page opens). While active it gets every
 * frame of the board CAN (monitor slot CAN_IF_MON_DASH, hardware filter
 * open) and decodes the messages the DBC knows, in its own task. Listen only:
 * nothing is sent, OBD can run at the same time. Bitrate = the CAN page's.
 *
 * Thread safe; values are copied out under a lock.
 */
#ifndef DASH_SVC_H
#define DASH_SVC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DASH_DBC_DIR    "/sdcard/db/dbc"

typedef struct {
    bool     loaded;
    bool     active;
    char     file[48];          /* DBC file name, "" if none */
    uint16_t nmsg, nsig;
    uint32_t skipped;           /* DBC lines not understood */
    uint32_t frames;            /* frames decoded since start */
    uint32_t unknown;           /* frames with IDs not in the DBC */
    uint16_t msgs_seen;         /* DBC messages received at least once */
    uint32_t dropped;           /* queue full */
    char     error[48];         /* "" if fine */
} dash_svc_status_t;

esp_err_t dash_svc_init(void);

/* (Re)load the DBC from the SD card. Blocking (file read), a few 10 ms. */
esp_err_t dash_svc_load(void);

/* Decode while on (CAN tap + open filter), idle while off. */
void dash_svc_set_active(bool on);

void dash_svc_get_status(dash_svc_status_t *out);

/* Text table of all signals: "name  value unit  age", sorted as in the DBC,
 * one line per signal, max_rows lines. Returns the number of signals. */
int dash_svc_format_signals(char *buf, size_t len, int max_rows);

/* Latest value of a signal by name; false if unknown or never received.
 * age_ms: time since it was received. */
bool dash_svc_get(const char *name, double *value, uint32_t *age_ms, char *unit, size_t unit_len);

#ifdef __cplusplus
}
#endif

#endif /* DASH_SVC_H */
