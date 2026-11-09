/*
 * can_if - CAN bus interface on top of the ESP-IDF esp_twai driver.
 *
 * Responsibilities:
 *   - own the TWAI node (bitrate, acceptance filter, bus-off recovery)
 *   - transmit queue (frames are copied, caller buffers can be reused at once)
 *   - receive dispatch: frames are copied into subscriber queues by ID range
 *   - bus statistics for diagnostics / UI
 *
 * No knowledge of ISO-TP, OBD or the UI.
 */
#ifndef CAN_IF_H
#define CAN_IF_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_IF_MAX_DLC          8
#define CAN_IF_MAX_SUBSCRIBERS  4

typedef struct {
    uint32_t id;
    uint32_t ts_ms;             /* receive timestamp (ms since boot), unused for TX */
    uint8_t  dlc;
    bool     ext;               /* 29-bit identifier */
    uint8_t  data[CAN_IF_MAX_DLC];
} can_frame_t;

typedef struct {
    uint32_t bitrate;           /* e.g. 500000 */
    uint32_t filter_id;         /* hardware acceptance filter, 11-bit */
    uint32_t filter_mask;       /* 1 = bit must match, 0 = don't care; 0 accepts all */
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
    uint32_t tx_dropped;        /* TX queue full */
    uint32_t bus_errors;
    can_if_bus_state_t state;
} can_if_stats_t;

/* Default config: bitrate from Kconfig, filter for OBD-II responses 0x7E8..0x7EF. */
#define CAN_IF_CONFIG_OBD_DEFAULT() {           \
    .bitrate     = CONFIG_CAN_BITRATE,          \
    .filter_id   = 0x7E8,                       \
    .filter_mask = 0x7F8,                       \
}

esp_err_t can_if_init(const can_if_config_t *cfg);

/* Queue a frame for transmission. timeout_ms = 0 for non-blocking. */
esp_err_t can_if_send(const can_frame_t *frame, int timeout_ms);

/* Copy every received frame with id_min <= id <= id_max into `queue`
 * (item size must be sizeof(can_frame_t)). Frames are dropped if the queue is full. */
esp_err_t can_if_subscribe(uint32_t id_min, uint32_t id_max, QueueHandle_t queue);

void can_if_get_stats(can_if_stats_t *out);

/* Monotonic milliseconds since boot (ISR safe). */
uint32_t can_if_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* CAN_IF_H */
