/*
 * datalog - SD card logging, runs in its own low priority task.
 *
 *   raw     every CAN frame seen / sent, candump log format
 *           /sdcard/can_NNN.log   "(12.345000) can0 7E8#0441<...>"
 *           (only OBD responses, or every frame on the bus with
 *           datalog_set_all_traffic(true))
 *   events  with the raw log: connection state, timeouts, CAN error counters
 *           /sdcard/evt_NNN.txt
 *   params  one CSV row per CONFIG_DATALOG_PARAM_PERIOD_MS with the latest value
 *           of every polled PID; empty cell = no fresh value
 *           /sdcard/par_NNN.csv   new file on every (re)connect or PID selection change
 *
 * File names and times: once the clock is set (SNTP over Wi-Fi, kept by the
 * RTC) files are named <prefix>_YYYYMMDD_HHMMSS (local time) and timestamped
 * in Unix time (the CSV also gets a local time column); before that they are
 * numbered and timestamped in seconds since boot. Fixed per file at open.
 * Files are synced every CONFIG_DATALOG_SYNC_PERIOD_MS and closed when logging
 * is switched off.
 */
#ifndef DATALOG_H
#define DATALOG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     raw_active;        /* file open and being written */
    bool     params_active;
    char     raw_file[32];      /* current or last file name, "" if none */
    char     params_file[32];
    uint32_t raw_frames;        /* frames written to the current raw file */
    uint32_t raw_dropped;       /* frames lost (queue full) since start */
    uint32_t params_rows;
    uint32_t error_count;       /* increments on every open / write failure */
    char     error[48];         /* last error, "" if none */
    bool     evt_active;
    char     evt_file[32];
} datalog_status_t;

/* dir: mount point of the SD card, e.g. "/sdcard". Logging starts disabled. */
esp_err_t datalog_init(const char *dir);

/* Idempotent; safe to call from any task. */
void datalog_enable_raw(bool on);
void datalog_enable_params(bool on);
/* Raw log: capture every frame on the bus instead of the OBD conversation only.
 * Opens the CAN hardware filter while a raw log is being written. */
void datalog_set_all_traffic(bool on);

void datalog_get_status(datalog_status_t *out);

/* true if `name` (plain file name in the log directory) is open for writing
 * right now; such a file must not be deleted (web_svc). */
bool datalog_is_open(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* DATALOG_H */
