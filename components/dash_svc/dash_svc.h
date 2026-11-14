/*
 * dash_svc - decodes a CAN broadcast (rusEFI, or any ECU) with a DBC file
 * from the SD card, for the Dash page.
 *
 * The DBC is a *.dbc in /sdcard/db/dbc/ chosen by name (Dash settings), else
 * the first one there, else the first in /sdcard/ (dash_svc_load()).
 * Base ID: an ECU with a configurable CAN base (rusEFI) sends the DBC's
 * messages shifted; dash_svc_set_base() sets where the DBC's lowest 11-bit ID
 * is on the bus, and all 11-bit IDs move with it. While active it gets every
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
    int      dbc_base;          /* lowest 11-bit ID in the DBC, -1 none */
    int      dbc_last;          /* highest 11-bit ID in the DBC */
    int      bus_base;          /* where dbc_base is on the bus */
    uint16_t nmsg, nsig;
    uint32_t skipped;           /* DBC lines not understood */
    uint32_t frames;            /* frames decoded since start */
    uint32_t unknown;           /* frames with IDs not in the DBC */
    uint16_t msgs_seen;         /* DBC messages received at least once */
    uint32_t dropped;           /* queue full */
    char     error[48];         /* "" if fine */
    uint32_t generation;        /* increments with every load: signal indexes change */
} dash_svc_status_t;

typedef struct {
    char     name[32];
    char     unit[12];
    double   value;
    uint32_t age_ms;
    bool     received;          /* false: never received since the load */
    int      decimals;          /* from the DBC resolution (scale) */
    double   min, max;          /* DBC range */
} dash_svc_value_t;

esp_err_t dash_svc_init(void);

/* (Re)load the DBC from the SD card: `name` in DASH_DBC_DIR, "" = the first
 * one found. Blocking (file read), a few 10 ms. The base is kept. */
esp_err_t dash_svc_load(const char *name);

/* Bus ID of the DBC's lowest 11-bit message; 0 = as in the DBC. */
void dash_svc_set_base(uint16_t base);

/* DBC files in DASH_DBC_DIR, sorted, '\n' separated; returns the count. */
int dash_svc_list_files(char *buf, size_t len);

/* Decode while on (CAN tap + open filter), idle while off. */
void dash_svc_set_active(bool on);

void dash_svc_get_status(dash_svc_status_t *out);

/* Text table of all signals: "name  value unit  age", sorted as in the DBC,
 * one line per signal, max_rows lines. Returns the number of signals. */
int dash_svc_format_signals(char *buf, size_t len, int max_rows);

/* Signal index by name (case-insensitive), -1 if none. Valid until the next load. */
int  dash_svc_find(const char *name);
int  dash_svc_signal_count(void);
/* Signal by index; false if the index is out of range. */
bool dash_svc_value(int idx, dash_svc_value_t *out);
/* All signal names, '\n' separated, in DBC order; returns the count. */
int  dash_svc_signal_names(char *buf, size_t len);

/* Latest value of a signal by name; false if unknown or never received.
 * age_ms: time since it was received. */
bool dash_svc_get(const char *name, double *value, uint32_t *age_ms, char *unit, size_t unit_len);

#ifdef __cplusplus
}
#endif

#endif /* DASH_SVC_H */
