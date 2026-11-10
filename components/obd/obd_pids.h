/*
 * obd_pids - Service 01 PID catalogue, supported/enabled state and poll scheduler.
 *
 * The catalogue (obd_pid_table.c) is const and lives in flash. Only PIDs with a
 * decoder are listed; anything else the ECU reports is tracked as "supported"
 * but never polled.
 *
 * State accessors are thread safe (UI reads/toggles, comsm polls).
 */
#ifndef OBD_PIDS_H
#define OBD_PIDS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "vehicle_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OBD_RATE_FAST = 0,      /* as often as possible (rpm, speed, ...) */
    OBD_RATE_MEDIUM,        /* ~2 Hz */
    OBD_RATE_SLOW,          /* ~0.5 Hz (temperatures, levels) */
} obd_rate_t;

typedef float (*obd_decoder_t)(const uint8_t *d);   /* d = A, B, C, ... */

typedef struct {
    uint8_t       pid;
    uint8_t       len;          /* data bytes after the PID */
    obd_rate_t    rate;
    vd_signal_t   signal;
    obd_decoder_t decode;
} obd_pid_desc_t;

/* --- catalogue --- */
size_t obd_pid_count(void);
const obd_pid_desc_t *obd_pid_desc(size_t idx);
int obd_pid_find(uint8_t pid);                  /* catalogue index or -1 */

/* --- supported / enabled state --- */
void obd_pid_reset_supported(void);
/* Apply a "PIDs supported" bitmap (response to PID 0x00, 0x20, 0x40, ...).
 * Returns true if the next range (base + 0x20) is supported too. */
bool obd_pid_apply_support_bitmap(uint8_t base_pid, uint32_t bitmap);
void obd_pid_mark_unsupported(uint8_t pid);
bool obd_pid_is_supported(uint8_t pid);
uint16_t obd_pid_supported_count(void);         /* catalogue entries supported */

/* Copy of the support bitmap over all PIDs (bit = pid & 7 of byte pid >> 3). */
void obd_pid_get_supported_bitmap(uint8_t out[256 / 8]);

/* User selection, default off. Only enabled AND supported PIDs are polled. */
bool obd_pid_is_enabled(size_t idx);
void obd_pid_set_enabled(size_t idx, bool enabled);
uint16_t obd_pid_active_count(void);            /* enabled and supported */

/* --- polling --- */
/* Up to `max` catalogue indexes to request next (out[] holds max entries).
 * Returns how many; 0 if nothing is due. */
#define OBD_MAX_PIDS_PER_REQUEST 6      /* ISO 15765-4 limit for Service 01 */
size_t obd_pid_next_batch(uint32_t now_ms, size_t max, int out[]);
void obd_pid_mark_polled(size_t idx, uint32_t now_ms);

/* Decode a positive Service 01 response (41 PID data [PID data ...]) into
 * vehicle_data. Returns the number of PIDs decoded. */
size_t obd_pid_handle_response(const uint8_t *resp, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* OBD_PIDS_H */
