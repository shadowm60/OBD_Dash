/* Internal to obd: ELM327 backend of obd_client (see obd_elm.c). */
#ifndef OBD_ELM_H
#define OBD_ELM_H

#include <stdint.h>
#include <stdbool.h>
#include "obd_client.h"

#define OBD_ELM_MAX_MSG OBD_MAX_RESPONSE

typedef enum {
    OBD_ELM_IDLE,
    OBD_ELM_PENDING,
    OBD_ELM_PROMPT,     /* the adapter finished: messages (if any) were delivered */
    OBD_ELM_ERROR,      /* see obd_elm_error() */
} obd_elm_result_t;

typedef void (*obd_elm_msg_cb_t)(uint32_t id, const uint8_t *msg, uint16_t len, void *arg);

void obd_elm_set_io(const obd_elm_io_t *io);
void obd_elm_invalidate(void);
bool obd_elm_ready(void);
const char *obd_elm_error(void);

/* rx_id 0 = functional (any responder). frames = CAN frames the answer will
 * take (1..15), 0 = unknown: the adapter then waits its timeout for more. */
esp_err_t obd_elm_request(uint32_t tx_id, uint32_t rx_id, const uint8_t *req, uint16_t len, uint8_t frames);
obd_elm_result_t obd_elm_poll(uint32_t wait_ms, obd_elm_msg_cb_t cb, void *arg);
void obd_elm_extend(uint32_t ms);
void obd_elm_abort(void);
/* Monitor (ATMA) for a CAN broadcast: frames whose 11-bit ID matches filter
 * under mask. Not while a request is pending; requests need monitor_stop. */
typedef void (*obd_elm_frame_cb_t)(uint32_t id, const uint8_t *data, uint8_t dlc, void *arg);
esp_err_t obd_elm_monitor_start(uint32_t filter, uint32_t mask);
bool      obd_elm_monitor_poll(uint32_t wait_ms, obd_elm_frame_cb_t cb, void *arg);   /* false: stopped */
void      obd_elm_monitor_stop(void);
bool      obd_elm_monitoring(void);
uint32_t  obd_elm_monitor_restarts(void);

/* true: answers that fit one CAN frame (7 bytes) return without the adapter timeout. */
bool obd_elm_single_frame_fast(void);

#endif
