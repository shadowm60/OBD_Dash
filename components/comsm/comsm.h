/*
 * comsm - communication state machine.
 *
 * Owns the OBD client in its own task. Other tasks (UI) only post commands and
 * read a status snapshot; they never block on the bus.
 *
 *   IDLE --connect--> CONNECTING --0100 ok--> DISCOVERY --done--> RUNNING
 *     ^                    |                                         |
 *     |                no answer                             N timeouts in a row
 *     |                    v                                         v
 *     +---disconnect--- RETRY_WAIT <---------------------------------+
 *                          (auto reconnect after CONFIG_OBD_RECONNECT_DELAY_MS)
 */
#ifndef COMSM_H
#define COMSM_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COMSM_IDLE = 0,
    COMSM_CONNECTING,
    COMSM_DISCOVERY,
    COMSM_RUNNING,
    COMSM_RETRY_WAIT,
} comsm_state_t;

typedef struct {
    comsm_state_t state;
    uint32_t ecu_id;            /* responding ECU (0x7E8..), 0 if none */
    uint16_t supported_pids;    /* catalogue PIDs the ECU supports */
    uint32_t responses;         /* successful responses since connect */
    uint32_t timeouts;          /* timeouts since connect */
    float    request_rate;      /* responses per second (last ~1 s) */
    uint32_t change_count;      /* increments on every state change */
} comsm_status_t;

esp_err_t comsm_init(void);

void comsm_connect(void);
void comsm_disconnect(void);
/* Connect when idle, otherwise disconnect / cancel. Used by the UI button. */
void comsm_toggle(void);

void comsm_get_status(comsm_status_t *out);
const char *comsm_state_name(comsm_state_t st);

#ifdef __cplusplus
}
#endif

#endif /* COMSM_H */
