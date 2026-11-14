/*
 * obd_client - one-request-at-a-time OBD-II / UDS transport over ISO-TP.
 *
 * Not thread safe by design: owned and driven by a single task (comsm).
 *
 * Addressing (ISO 15765-4, 11-bit):
 *   functional: requests to 0x7DF, first responder in 0x7E8..0x7EF is locked
 *               for the rest of that request (other ECUs are ignored)
 *   physical:   requests to <ecu>-8, only <ecu> is accepted
 *
 * Link: the board's CAN (TWAI + ISO-TP, default) or an ELM327 adapter on a
 * serial link (BLE), same requests and results. The adapter is set to CAN
 * 11-bit 500 kbit/s and does ISO-TP itself (single frame requests only).
 */
#ifndef OBD_CLIENT_H
#define OBD_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OBD_FUNCTIONAL_REQ_ID   0x7DF
#define OBD_ECU_RESP_ID_MIN     0x7E8
#define OBD_ECU_RESP_ID_MAX     0x7EF
#define OBD_MAX_RESPONSE        256

typedef enum {
    OBD_REQ_IDLE = 0,
    OBD_REQ_PENDING,
    OBD_REQ_DONE,       /* positive response available */
    OBD_REQ_NEGATIVE,   /* 0x7F response, see obd_client_nrc() */
    OBD_REQ_TIMEOUT,
    OBD_REQ_ERROR,      /* transport error (TX failed, ISO-TP error) */
} obd_req_status_t;

typedef enum {
    OBD_LINK_CAN = 0,       /* board CAN (TWAI) */
    OBD_LINK_ELM = 1,       /* ELM327 adapter, serial I/O from obd_client_set_elm_io() */
} obd_link_t;

/* Serial I/O of the ELM327 adapter (e.g. ble_svc_serial_*). read waits up to
 * wait_ms for the first byte. */
typedef struct {
    bool      (*ready)(void);
    esp_err_t (*write)(const void *data, size_t len);
    size_t    (*read)(void *buf, size_t max, uint32_t wait_ms);
} obd_elm_io_t;

esp_err_t obd_client_init(void);

/* Not while a request is pending (comsm switches only while idle). */
void        obd_client_set_link(obd_link_t link);
obd_link_t  obd_client_link(void);
void        obd_client_set_elm_io(const obd_elm_io_t *io);
/* Something else (console) talked to the adapter: set it up again. */
void        obd_client_elm_invalidate(void);
/* Largest Service 01 answer (bytes, incl. 0x41) a batch should produce on
 * this link, 0 = no limit. ELM link: 7, one CAN frame, the fast path. */
uint16_t    obd_client_batch_bytes(void);
/* Last link problem, e.g. "adapter not connected", "CAN ERROR"; "" if none. */
const char *obd_client_link_error(void);

void obd_client_set_functional(void);
void obd_client_set_physical(uint32_t ecu_resp_id);

/* Start a request (payload = service [+ data]). Fails if one is already pending. */
esp_err_t obd_client_request(const uint8_t *req, uint16_t len);

/* Drop a pending request (late responses are ignored). */
void obd_client_abort(void);

/* Pump received frames and timers. Blocks up to wait_ms for the next frame.
 * Returns the current status; DONE/NEGATIVE/TIMEOUT/ERROR are reported once
 * and the client returns to IDLE on the next call. */
obd_req_status_t obd_client_process(uint32_t wait_ms);

/* Valid after OBD_REQ_DONE until the next request. */
const uint8_t *obd_client_response(uint16_t *len);
uint32_t obd_client_responder_id(void);
uint8_t  obd_client_nrc(void);

#ifdef __cplusplus
}
#endif

#endif /* OBD_CLIENT_H */
