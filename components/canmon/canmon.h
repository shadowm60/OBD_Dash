/*
 * canmon - CAN bus monitor (the CAN page), from Esp32_monitor's canmon
 * without its logger (datalog writes the raw log here).
 *
 *   - statistics: frames/s, peak, estimated bus load, error counters
 *   - a table of every ID seen (RX and our own TX): DLC, last data, count, period
 *   - bus settings at runtime: bitrate, listen-only (never ACKs or sends;
 *     only sensible while OBD is disconnected - the caller checks that)
 *
 * While running it gets every frame through can_if's CAN_IF_MON_VIEW slot and
 * keeps the hardware filter open (all IDs). One task, commands are queued:
 * all calls are non-blocking and safe from the LVGL task.
 */
#ifndef CANMON_H
#define CANMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     running;           /* monitor on (filter open, frames counted) */
    uint32_t bitrate;           /* bus settings in use */
    bool     listen_only;
    const char *state;          /* "stopped", "active", "warning", "error passive", "bus-off" */
    uint32_t frames;            /* since start / reset */
    uint32_t fps;               /* last second */
    uint32_t fps_peak;
    uint32_t std_frames, ext_frames;
    uint8_t  load_pct;          /* estimated bus load, last second */
    uint32_t ids;               /* distinct IDs in the table */
    bool     id_table_full;
    uint32_t bus_errors;
    uint32_t tec, rec;
    uint32_t dropped;           /* frames lost before the monitor (queue full) */
    uint32_t uptime_s;          /* since start */
} canmon_stats_t;

esp_err_t canmon_init(void);

esp_err_t canmon_start(void);
esp_err_t canmon_stop(void);                        /* takes effect at once */
esp_err_t canmon_reset(void);                       /* counters and the ID table */
/* Restart the bus with another bitrate / listen-only (OBD filter kept). */
esp_err_t canmon_set_bus(uint32_t bitrate, bool listen_only);

void canmon_get_stats(canmon_stats_t *out);

/* Logs where the monitor task is (diagnostics, e.g. a command not taken). */
void canmon_debug_log(void);

/* Writes the ID table as fixed-width text (sorted by ID), at most `max_rows`
 * rows. Returns the number of IDs in the table. */
int canmon_format_ids(char *buf, size_t len, int max_rows);

#ifdef __cplusplus
}
#endif

#endif /* CANMON_H */
