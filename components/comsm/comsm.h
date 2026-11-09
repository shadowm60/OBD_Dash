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
#include "vehicle_data.h"

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

/* ------------------------------------------------------------ diagnostics
 * Jobs run in the comsm task while connected; PID polling pauses meanwhile.
 * Results are snapshots; `seq` increments whenever a report changes. */

#define COMSM_MAX_DTCS          32
#define COMSM_MAX_FF_VALUES     24

typedef enum {
    COMSM_DIAG_IDLE = 0,        /* nothing requested yet */
    COMSM_DIAG_BUSY,
    COMSM_DIAG_DONE,
    COMSM_DIAG_FAILED,          /* see `error` */
} comsm_diag_state_t;

typedef enum {
    COMSM_DTC_STORED = 0,       /* Service 03: confirmed */
    COMSM_DTC_PENDING,          /* Service 07: current/last drive cycle */
    COMSM_DTC_PERMANENT,        /* Service 0A: cannot be cleared by tester */
    COMSM_DTC_KIND_COUNT
} comsm_dtc_kind_t;

typedef struct {
    comsm_diag_state_t state;
    uint32_t seq;
    bool     cleared;                           /* report follows a successful clear */
    bool     supported[COMSM_DTC_KIND_COUNT];   /* ECU answered that service */
    uint8_t  count[COMSM_DTC_KIND_COUNT];
    uint16_t codes[COMSM_DTC_KIND_COUNT][COMSM_MAX_DTCS];
    char     error[64];
} comsm_dtc_report_t;

typedef struct {
    uint8_t     pid;
    vd_signal_t signal;         /* name / unit / decimals via vd_info() */
    float       value;
} comsm_ff_value_t;

typedef struct {
    comsm_diag_state_t state;
    uint32_t seq;
    uint16_t dtc;               /* code that stored the frame, 0 = no frame stored */
    uint8_t  count;
    comsm_ff_value_t values[COMSM_MAX_FF_VALUES];
    char     error[64];
} comsm_freeze_frame_t;

void comsm_read_dtcs(void);
/* Service 04: clears stored/pending DTCs and freeze frame, turns the MIL off and
 * resets readiness monitors. Re-reads the DTCs afterwards. Confirm with the user first! */
void comsm_clear_dtcs(void);
void comsm_read_freeze_frame(void);

void comsm_get_dtc_report(comsm_dtc_report_t *out);
void comsm_get_freeze_frame(comsm_freeze_frame_t *out);
const char *comsm_state_name(comsm_state_t st);

#ifdef __cplusplus
}
#endif

#endif /* COMSM_H */
