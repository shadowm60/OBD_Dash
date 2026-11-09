/* Internal to comsm: diagnostic job sequencer. Called only from the comsm task. */
#ifndef COMSM_DIAG_H
#define COMSM_DIAG_H

#include <stdint.h>
#include <stdbool.h>
#include "obd_client.h"

typedef enum {
    DIAG_JOB_READ_DTC,
    DIAG_JOB_CLEAR_DTC,
    DIAG_JOB_FREEZE_FRAME,
} diag_job_t;

void diag_start(diag_job_t job);
/* Fail a requested job immediately (e.g. not connected). */
void diag_reject(diag_job_t job, const char *reason);
void diag_abort(const char *reason);
bool diag_active(void);

/* Next request of the running job. Returns false if the job just finished. */
bool diag_next_request(uint8_t *req, uint16_t *len);
void diag_on_result(obd_req_status_t st, const uint8_t *r, uint16_t len, uint8_t nrc);

#endif
