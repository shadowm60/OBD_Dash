/*
 * Diagnostic jobs (DTC read / clear, freeze frame) as small step sequencers.
 *
 * comsm calls diag_next_request() to get the next request of the active job and
 * feeds the outcome back via diag_on_result(). Results are published into
 * snapshots guarded by a spinlock so the UI can read them at any time.
 */
#include "comsm.h"
#include "comsm_diag.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "obd_dtc.h"
#include "obd_pids.h"

static const char *TAG = "comsm_diag";

typedef enum {
    /* DTC read (also tail of clear) */
    STEP_CLEAR,
    STEP_STORED,
    STEP_PENDING,
    STEP_PERMANENT,
    /* freeze frame */
    STEP_FF_SUPPORT,
    STEP_FF_DTC,
    STEP_FF_VALUES,
    STEP_DONE,
} step_t;

static bool    s_active;
static step_t  s_step;
static bool    s_is_ff;                 /* job publishes to the freeze frame report */
static uint8_t s_ff_base;               /* current 02 xx 00 support range */
static uint8_t s_ff_supported[256 / 8];
static size_t  s_ff_idx;                /* catalogue index being read */

/* Reports: written by the comsm task, copied out by the UI. */
static comsm_dtc_report_t   s_dtc;
static comsm_freeze_frame_t s_ff;
static comsm_dtc_report_t   s_dtc_work;  /* built during a job, published at the end */
static comsm_freeze_frame_t s_ff_work;
static portMUX_TYPE         s_lock = portMUX_INITIALIZER_UNLOCKED;

/* ---------------------------------------------------------------- publishing */

static void publish_dtc(comsm_diag_state_t st)
{
    portENTER_CRITICAL(&s_lock);
    s_dtc_work.state = st;
    s_dtc_work.seq = s_dtc.seq + 1;
    s_dtc = s_dtc_work;
    portEXIT_CRITICAL(&s_lock);
}

static void publish_ff(comsm_diag_state_t st)
{
    portENTER_CRITICAL(&s_lock);
    s_ff_work.state = st;
    s_ff_work.seq = s_ff.seq + 1;
    s_ff = s_ff_work;
    portEXIT_CRITICAL(&s_lock);
}

static void finish(comsm_diag_state_t st, const char *error)
{
    if (s_is_ff) {
        if (error) {
            snprintf(s_ff_work.error, sizeof(s_ff_work.error), "%s", error);
        }
        publish_ff(st);
    } else {
        if (error) {
            snprintf(s_dtc_work.error, sizeof(s_dtc_work.error), "%s", error);
        }
        publish_dtc(st);
    }
    if (error) {
        ESP_LOGW(TAG, "job failed: %s", error);
    } else {
        ESP_LOGI(TAG, "job done");
    }
    s_active = false;
    s_step = STEP_DONE;
}

/* ---------------------------------------------------------------- job control */

void diag_start(diag_job_t job)
{
    if (s_active) {
        ESP_LOGW(TAG, "job already running, request ignored");
        return;
    }
    s_active = true;
    s_is_ff = (job == DIAG_JOB_FREEZE_FRAME);

    if (s_is_ff) {
        memset(&s_ff_work, 0, sizeof(s_ff_work));
        memset(s_ff_supported, 0, sizeof(s_ff_supported));
        s_ff_base = 0x00;
        s_ff_idx = 0;
        s_step = STEP_FF_SUPPORT;
        publish_ff(COMSM_DIAG_BUSY);
    } else {
        memset(&s_dtc_work, 0, sizeof(s_dtc_work));
        s_step = (job == DIAG_JOB_CLEAR_DTC) ? STEP_CLEAR : STEP_STORED;
        publish_dtc(COMSM_DIAG_BUSY);
    }
}

void diag_reject(diag_job_t job, const char *reason)
{
    if (s_active) {
        return;     /* don't clobber a running job's report */
    }
    s_is_ff = (job == DIAG_JOB_FREEZE_FRAME);
    if (s_is_ff) {
        memset(&s_ff_work, 0, sizeof(s_ff_work));
    } else {
        memset(&s_dtc_work, 0, sizeof(s_dtc_work));
    }
    s_active = true;    /* so finish() runs the normal path */
    finish(COMSM_DIAG_FAILED, reason);
}

void diag_abort(const char *reason)
{
    if (s_active) {
        finish(COMSM_DIAG_FAILED, reason);
    }
}

bool diag_active(void)
{
    return s_active;
}

/* ---------------------------------------------------------------- freeze frame helpers */

static bool ff_bit(uint8_t pid)
{
    return (s_ff_supported[pid >> 3] >> (pid & 7)) & 1;
}

/* Advance s_ff_idx to the next catalogue PID that is in the freeze frame. */
static bool ff_next_value_pid(void)
{
    for (; s_ff_idx < obd_pid_count(); s_ff_idx++) {
        const obd_pid_desc_t *d = obd_pid_desc(s_ff_idx);
        /* PID 01 has side effects on live data (MIL) and no meaning in a frame. */
        if (d->pid != 0x01 && ff_bit(d->pid)) {
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------- sequencing */

bool diag_next_request(uint8_t *req, uint16_t *len)
{
    if (!s_active) {
        return false;
    }

    switch (s_step) {
    case STEP_CLEAR:     req[0] = 0x04; *len = 1; return true;
    case STEP_STORED:    req[0] = 0x03; *len = 1; return true;
    case STEP_PENDING:   req[0] = 0x07; *len = 1; return true;
    case STEP_PERMANENT: req[0] = 0x0A; *len = 1; return true;

    case STEP_FF_SUPPORT:
        req[0] = 0x02; req[1] = s_ff_base; req[2] = 0x00; *len = 3;
        return true;
    case STEP_FF_DTC:
        req[0] = 0x02; req[1] = 0x02; req[2] = 0x00; *len = 3;
        return true;
    case STEP_FF_VALUES:
        if (s_ff_work.count >= COMSM_MAX_FF_VALUES || !ff_next_value_pid()) {
            finish(COMSM_DIAG_DONE, NULL);
            return false;
        }
        req[0] = 0x02; req[1] = obd_pid_desc(s_ff_idx)->pid; req[2] = 0x00; *len = 3;
        return true;

    case STEP_DONE:
        break;
    }
    s_active = false;
    return false;
}

static void store_dtcs(comsm_dtc_kind_t kind, const uint8_t *r, uint16_t len)
{
    s_dtc_work.supported[kind] = true;
    s_dtc_work.count[kind] = (uint8_t)obd_dtc_parse(r, len, s_dtc_work.codes[kind], COMSM_MAX_DTCS);
}

void diag_on_result(obd_req_status_t st, const uint8_t *r, uint16_t len, uint8_t nrc)
{
    if (!s_active) {
        return;
    }
    bool ok = (st == OBD_REQ_DONE);
    char err[64];

    switch (s_step) {
    case STEP_CLEAR:
        if (ok) {
            ESP_LOGI(TAG, "DTCs cleared");
            s_dtc_work.cleared = true;
            s_step = STEP_STORED;
        } else if (st == OBD_REQ_NEGATIVE) {
            /* 0x22 conditionsNotCorrect: typically engine running */
            snprintf(err, sizeof(err), "Clear refused (NRC %02X)%s", nrc,
                     nrc == 0x22 ? " - stop engine, ignition on" : "");
            finish(COMSM_DIAG_FAILED, err);
        } else {
            finish(COMSM_DIAG_FAILED, "Clear: no response");
        }
        break;

    case STEP_STORED:
        if (!ok) {
            finish(COMSM_DIAG_FAILED, st == OBD_REQ_NEGATIVE ? "Read DTCs refused" : "Read DTCs: no response");
            break;
        }
        store_dtcs(COMSM_DTC_STORED, r, len);
        s_step = STEP_PENDING;
        break;

    case STEP_PENDING:
        if (ok) {
            store_dtcs(COMSM_DTC_PENDING, r, len);
        }
        s_step = STEP_PERMANENT;
        break;

    case STEP_PERMANENT:
        if (ok) {
            store_dtcs(COMSM_DTC_PERMANENT, r, len);
        }
        finish(COMSM_DIAG_DONE, NULL);
        break;

    case STEP_FF_SUPPORT:
        /* 42 base 00 A B C D */
        if (!ok || len < 7) {
            if (s_ff_base == 0x00) {
                finish(COMSM_DIAG_FAILED, "Freeze frame not supported");
            } else {
                s_step = STEP_FF_DTC;
            }
            break;
        } else {
            uint32_t bm = ((uint32_t)r[3] << 24) | ((uint32_t)r[4] << 16) | ((uint32_t)r[5] << 8) | r[6];
            for (int i = 0; i < 32 && s_ff_base + 1 + i <= 0xFF; i++) {
                if (bm & (1u << (31 - i))) {
                    uint8_t pid = (uint8_t)(s_ff_base + 1 + i);
                    s_ff_supported[pid >> 3] |= (uint8_t)(1u << (pid & 7));
                }
            }
            if ((bm & 1u) && s_ff_base < 0xE0) {
                s_ff_base += 0x20;
            } else {
                s_step = STEP_FF_DTC;
            }
        }
        break;

    case STEP_FF_DTC:
        /* 42 02 00 hi lo */
        if (ok && len >= 5) {
            s_ff_work.dtc = (uint16_t)((r[3] << 8) | r[4]);
        }
        if (s_ff_work.dtc == 0) {
            /* No frame stored: nothing else to read. */
            finish(COMSM_DIAG_DONE, NULL);
        } else {
            s_step = STEP_FF_VALUES;
        }
        break;

    case STEP_FF_VALUES: {
        /* 42 pid 00 data... */
        const obd_pid_desc_t *d = obd_pid_desc(s_ff_idx);
        if (ok && len >= 3 + d->len) {
            comsm_ff_value_t *v = &s_ff_work.values[s_ff_work.count++];
            v->pid = d->pid;
            v->signal = d->signal;
            v->value = d->decode(&r[3]);
        }
        s_ff_idx++;
        break;
    }

    case STEP_DONE:
        break;
    }
}

/* ---------------------------------------------------------------- public getters */

void comsm_get_dtc_report(comsm_dtc_report_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_dtc;
    portEXIT_CRITICAL(&s_lock);
}

void comsm_get_freeze_frame(comsm_freeze_frame_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_ff;
    portEXIT_CRITICAL(&s_lock);
}
