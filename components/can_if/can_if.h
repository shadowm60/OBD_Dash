/*
 * can_if - CAN bus interface on top of the ESP-IDF esp_twai driver.
 *
 * Responsibilities:
 *   - start / stop at runtime (bitrate, listen-only)
 *   - own the TWAI node (bitrate, acceptance filter, bus-off recovery)
 *   - transmit queue (frames are copied, caller buffers can be reused at once)
 *   - receive dispatch: frames are copied into subscriber queues by ID range
 *   - bus statistics for diagnostics / UI
 *
 * No knowledge of the OBD protocol, the logger or the UI. (Shared with
 * Esp32_monitor; this copy has two monitor slots and filter owners.)
 */
#ifndef CAN_IF_H
#define CAN_IF_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_IF_MAX_DLC          8
#define CAN_IF_MAX_SUBSCRIBERS  4

typedef struct {
    uint32_t id;
    uint32_t ts_ms;             /* RX / TX-done timestamp (ms since boot); ignored by can_if_send */
    uint8_t  dlc;
    bool     ext;               /* 29-bit identifier */
    uint8_t  data[CAN_IF_MAX_DLC];
    bool     tx;                /* monitor queue only: frame was sent by us */
} can_frame_t;

typedef struct {
    uint32_t bitrate;           /* e.g. 500000 */
    uint32_t filter_id;         /* hardware acceptance filter, 11-bit */
    uint32_t filter_mask;       /* 1 = bit must match, 0 = don't care; 0 accepts all */
    bool     listen_only;       /* never ACK / transmit (can_if_send fails) */
} can_if_config_t;

typedef enum {
    CAN_IF_BUS_ACTIVE = 0,
    CAN_IF_BUS_WARNING,
    CAN_IF_BUS_PASSIVE,
    CAN_IF_BUS_OFF,
    CAN_IF_BUS_STOPPED,
} can_if_bus_state_t;

typedef struct {
    uint32_t rx_frames;
    uint32_t rx_dropped;        /* subscriber queue full */
    uint32_t tx_frames;
    uint32_t tx_failed;         /* not acknowledged / arbitration lost after retries */
    uint32_t tx_retries;        /* extra attempts (lost arbitration, bus error) */
    uint32_t tx_dropped;        /* TX queue full */
    uint32_t bus_errors;
    uint32_t mon_dropped;       /* monitor queue full */
    uint32_t tec;               /* controller TX error counter (0 while stopped) */
    uint32_t rec;               /* controller RX error counter */
    can_if_bus_state_t state;
} can_if_stats_t;

/* Default config: bitrate from Kconfig, hardware filter for OBD-II responses
 * 0x7E8..0x7EF. can_if_set_accept_all() opens it at runtime. */
#define CAN_IF_CONFIG_OBD_DEFAULT() {               \
    .bitrate     = CONFIG_CAN_BITRATE,              \
    .filter_id   = 0x7E8,                           \
    .filter_mask = 0x7F8,                           \
    .listen_only = false,                           \
}

/* Monitor slots: each gets every accepted RX frame and every sent TX frame
 * that passes the monitor filter. */

/* Filter of all monitor slots (CAN monitor, raw log, Dash): only 11-bit IDs
 * with (id & mask) == (filter & mask); mask 0 = every frame (default).
 * OBD subscribers are not affected. */
void can_if_set_monitor_filter(uint32_t filter, uint32_t mask);

/* A frame that crossed another link (BLE ELM327 adapter), given to the monitor
 * slots as if it was on the board CAN: the raw log and the CAN page monitor
 * see that traffic too. Any task, not from an ISR. */
void can_if_inject_monitor(const can_frame_t *f);
typedef enum {
    CAN_IF_MON_LOG = 0,         /* datalog: raw CAN log */
    CAN_IF_MON_VIEW,            /* canmon: bus monitor (CAN page) */
    CAN_IF_MON_DASH,            /* dash_svc: DBC decoding (Dash page) */
    CAN_IF_MON_COUNT,
} can_if_monitor_t;

/* Owners of "accept all IDs" (the filter is open while any of them wants it). */
#define CAN_IF_ALL_LOG      0x01    /* raw log with All traffic */
#define CAN_IF_ALL_MONITOR  0x02    /* bus monitor running */
#define CAN_IF_ALL_DASH     0x04    /* Dash page decoding a broadcast (rusEFI) */
#define CAN_IF_ALL_DIAG     0x08    /* diagnostics with an ECU answering outside 7E8..7EF */

/* Same as can_if_start(). */
esp_err_t can_if_init(const can_if_config_t *cfg);

/* (Re)start the controller with `cfg`: an already running node is stopped
 * first, so this also changes bitrate / listen-only at runtime. Statistics
 * keep counting; see can_if_reset_stats(). Not from an ISR. */
esp_err_t can_if_start(const can_if_config_t *cfg);

/* Stop and delete the node; queued TX frames are discarded. Idempotent. */
void can_if_stop(void);

bool can_if_running(void);

/* Config of the last successful can_if_start(). */
void can_if_get_config(can_if_config_t *out);

void can_if_reset_stats(void);

/* Queue a frame for transmission. timeout_ms = 0 for non-blocking. */
esp_err_t can_if_send(const can_frame_t *frame, int timeout_ms);

/* Copy every received frame with id_min <= id <= id_max into `queue`
 * (item size must be sizeof(can_frame_t)). Frames are dropped if the queue is full. */
esp_err_t can_if_subscribe(uint32_t id_min, uint32_t id_max, QueueHandle_t queue);

/* Change the ID range of an existing subscription (by its queue). */
esp_err_t can_if_set_subscription_range(QueueHandle_t queue, uint32_t id_min, uint32_t id_max);

/* Monitor `slot`: every accepted RX frame and every successfully sent TX frame
 * (tx = true) is copied into `queue`, regardless of subscriptions. NULL
 * detaches. Frames are dropped (mon_dropped) if the queue is full. */
void can_if_set_monitor(can_if_monitor_t slot, QueueHandle_t queue);

/* `owner` (CAN_IF_ALL_*) wants every frame on the bus (hardware filter open)
 * or not any more. Open while any owner wants it, else the configured filter.
 * Subscribers still only get their ID range. Applied by the TX task between
 * frames: the controller is stopped for a moment, which may cost one request. */
void can_if_set_accept_all(uint8_t owner, bool on);

void can_if_get_stats(can_if_stats_t *out);

/* Monotonic milliseconds since boot (ISR safe). */
uint32_t can_if_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* CAN_IF_H */
