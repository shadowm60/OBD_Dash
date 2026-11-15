#include "comsm.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include "can_if.h"
#include "obd_client.h"
#include "obd_pids.h"
#include "vehicle_data.h"
#include "comsm_diag.h"

static const char *TAG = "comsm";

#define TASK_STACK          4096
#define TASK_PRIO           6
#define CONNECT_ATTEMPTS    3

typedef enum { CMD_CONNECT, CMD_DISCONNECT, CMD_TOGGLE, CMD_READ_DTC, CMD_CLEAR_DTC, CMD_FREEZE_FRAME } cmd_t;

typedef enum { REQ_NONE, REQ_PROBE, REQ_DISCOVERY, REQ_POLL, REQ_DIAG, REQ_UDS, REQ_KEEP } req_kind_t;

static QueueHandle_t  s_cmd_queue;
static comsm_status_t s_status;
static portMUX_TYPE   s_lock = portMUX_INITIALIZER_UNLOCKED;

/* task-private state */
static comsm_state_t  s_state;
static req_kind_t     s_pending;
static int            s_pending_idx[OBD_MAX_PIDS_PER_REQUEST]; /* catalogue indexes for REQ_POLL */
static size_t         s_pending_n;          /* 0 = keepalive */
static volatile uint8_t s_batch_cfg = 1;    /* PIDs per request wanted (UI) */
static volatile uint8_t s_link_cfg = OBD_LINK_CAN;  /* link wanted (UI), applied at connect */
/* broadcast monitor over the adapter (Dash), set by the UI, run in the task */
static volatile bool     s_mon_want;
static volatile uint32_t s_mon_filter, s_mon_mask;
static comsm_frame_cb_t  s_mon_cb;
static void             *s_mon_arg;
static uint32_t          s_mon_applied_filter, s_mon_applied_mask;
static uint32_t          s_mon_retry_ms;
/* UDS request from the Diag page: written by the UI under s_lock, run by the task */
static volatile bool     s_uds_want;
static uint32_t          s_uds_tx, s_uds_rx;
static uint8_t           s_uds_req[7], s_uds_len;
static comsm_uds_result_t s_uds;
static uint32_t          s_uds_last_ms;
static uint32_t          s_cur_tx, s_cur_rx;        /* UDS request being run */
static uint8_t           s_cur_sid, s_cur_sub;
/* open diagnostic session, kept alive with 3E 80 (task only) */
#define KEEP_PERIOD_MS   2000
#define SESSION_IDLE_MS  (10 * 60 * 1000)
static bool              s_sess_on;
static uint8_t           s_sess_type;
static uint32_t          s_sess_tx, s_sess_rx, s_sess_next_ms, s_sess_user_ms;
static uint8_t        s_batch_limit;        /* what this ECU accepts, <= s_batch_cfg */
static uint8_t        s_batch_applied;      /* s_batch_cfg value s_batch_limit was set from */
static uint8_t        s_discovery_pid;
static uint8_t        s_connect_attempts;
static uint8_t        s_consecutive_timeouts;
static uint32_t       s_retry_at_ms;
static uint32_t       s_rate_window_start;
static uint32_t       s_rate_window_count;
static uint32_t       s_last_tx_ms;
static int            s_deferred_job = -1;  /* diag_job_t asked while not connected, run once RUNNING */

const char *comsm_state_name(comsm_state_t st)
{
    switch (st) {
        case COMSM_IDLE:       return "idle";
        case COMSM_CONNECTING: return "connecting";
        case COMSM_DISCOVERY:  return "discovery";
        case COMSM_RUNNING:    return "running";
        case COMSM_RETRY_WAIT: return "retry_wait";
    }
    return "?";
}

/* ---------------------------------------------------------------- status helpers */

static void set_state(comsm_state_t st)
{
    if (st == s_state) {
        return;
    }
    ESP_LOGI(TAG, "%s -> %s", comsm_state_name(s_state), comsm_state_name(st));
    s_state = st;
    portENTER_CRITICAL(&s_lock);
    s_status.state = st;
    s_status.change_count++;
    portEXIT_CRITICAL(&s_lock);
}

static void status_update(void (*fn)(comsm_status_t *s, uint32_t arg), uint32_t arg)
{
    portENTER_CRITICAL(&s_lock);
    fn(&s_status, arg);
    portEXIT_CRITICAL(&s_lock);
}

static void st_reset(comsm_status_t *s, uint32_t arg)       { s->ecu_id = 0; s->supported_pids = 0; s->responses = 0; s->timeouts = 0; s->request_rate = 0; }
static void st_ecu(comsm_status_t *s, uint32_t arg)         { s->ecu_id = arg; }
static void st_supported(comsm_status_t *s, uint32_t arg)   { s->supported_pids = (uint16_t)arg; }
static void st_response(comsm_status_t *s, uint32_t arg)    { s->responses++; }
static void st_timeout(comsm_status_t *s, uint32_t arg)     { s->timeouts++; }
static void st_rate(comsm_status_t *s, uint32_t arg)        { s->request_rate = (float)arg / 10.0f; }
static void st_batch(comsm_status_t *s, uint32_t arg)       { s->pids_per_request = (uint8_t)arg; }

/* ---------------------------------------------------------------- transitions */

static void send_raw(req_kind_t kind, const uint8_t *req, uint16_t len)
{
    /* A send failure is reported as OBD_REQ_ERROR by the next obd_client_process(). */
    obd_client_request(req, len);
    s_pending = kind;
    s_last_tx_ms = can_if_now_ms();
}

static void send_request(req_kind_t kind, uint8_t sid, uint8_t pid)
{
    uint8_t req[2] = { sid, pid };
    send_raw(kind, req, sizeof(req));
}

/* A diag job that waits for the connection gives up. */
static void deferred_fail(const char *why)
{
    if (s_deferred_job >= 0) {
        diag_reject((diag_job_t)s_deferred_job, why);
        s_deferred_job = -1;
    }
}

static void go_idle(void)
{
    deferred_fail("Disconnected");
    diag_abort("Disconnected");
    obd_client_abort();
    s_pending = REQ_NONE;
    vd_invalidate_all();
    status_update(st_reset, 0);
    set_state(COMSM_IDLE);
}

static void go_retry_wait(void)
{
    deferred_fail("No answer from the ECU (ignition on?)");
    diag_abort("Connection lost");
    obd_client_abort();
    s_pending = REQ_NONE;
    vd_invalidate_all();
    s_retry_at_ms = can_if_now_ms() + CONFIG_OBD_RECONNECT_DELAY_MS;
    set_state(COMSM_RETRY_WAIT);
}

static void st_monitoring(comsm_status_t *s, uint32_t arg) { s->monitoring = arg != 0; }

static void monitor_stop(void)
{
    if (obd_client_monitoring()) {
        obd_client_monitor_stop();
        status_update(st_monitoring, 0);
    }
}

/* Start a waiting UDS request (idle only). */
static void uds_service(void)
{
    if (!s_uds_want || s_state != COMSM_IDLE || s_pending != REQ_NONE) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    uint8_t req[7];
    uint8_t len = s_uds_len;
    memcpy(req, s_uds_req, len);
    uint32_t tx = s_uds_tx, rx = s_uds_rx;
    s_uds_want = false;
    portEXIT_CRITICAL(&s_lock);
    obd_client_set_link((obd_link_t)s_link_cfg);
    obd_client_set_ids(tx, rx);
    s_cur_tx = tx;
    s_cur_rx = rx;
    s_cur_sid = req[0];
    s_cur_sub = len > 1 ? req[1] : 0;
    send_raw(REQ_UDS, req, len);
}

static void st_session(comsm_status_t *s, uint32_t arg)
{
    s->session_open = s_sess_on;
    s->session_type = s_sess_type;
    s->session_tx = (uint16_t)s_sess_tx;
    s->session_rx = (uint16_t)s_sess_rx;
}

static void session_end(const char *why)
{
    if (s_sess_on) {
        ESP_LOGI(TAG, "session %02X on %03lX ended (%s)", s_sess_type, (unsigned long)s_sess_tx, why);
        s_sess_on = false;
        status_update(st_session, 0);
    }
}

/* after a UDS answer: follow session changes, push the keepalive back */
static void session_track(bool positive)
{
    uint32_t now = can_if_now_ms();
    if (positive && s_cur_sid == 0x10) {
        if (s_cur_sub == 0x01) {
            if (s_sess_on && s_sess_tx == s_cur_tx) {
                session_end("back to the default session");
            }
        } else {
            if (s_sess_on && s_sess_tx != s_cur_tx) {
                session_end("session on another ECU");
            }
            s_sess_on = true;
            s_sess_type = s_cur_sub;
            s_sess_tx = s_cur_tx;
            s_sess_rx = s_cur_rx;
            status_update(st_session, 0);
            ESP_LOGI(TAG, "session %02X on %03lX: tester present every %d s", s_sess_type,
                     (unsigned long)s_sess_tx, KEEP_PERIOD_MS / 1000);
        }
    }
    if (s_sess_on && s_cur_tx == s_sess_tx) {
        s_sess_next_ms = now + KEEP_PERIOD_MS;  /* any request keeps the session alive */
        s_sess_user_ms = now;
    }
}

/* idle: send 3E 80 to the session's ECU when due */
static void keep_service(void)
{
    if (!s_sess_on || s_state != COMSM_IDLE || s_pending != REQ_NONE || s_uds_want) {
        return;
    }
    uint32_t now = can_if_now_ms();
    if ((int32_t)(now - (s_sess_user_ms + SESSION_IDLE_MS)) >= 0) {
        session_end("10 min without a request");
        return;
    }
    if ((int32_t)(now - s_sess_next_ms) < 0) {
        return;
    }
    static const uint8_t TP[2] = { 0x3E, 0x80 };
    obd_client_set_link((obd_link_t)s_link_cfg);
    obd_client_set_ids(s_sess_tx, s_sess_rx);
    send_raw(REQ_KEEP, TP, sizeof(TP));
    s_sess_next_ms = now + KEEP_PERIOD_MS;
    s_uds_last_ms = now;                        /* keeps the adapter's Dash monitor paused */
}

esp_err_t comsm_uds_request(uint32_t tx_id, uint32_t rx_id, const uint8_t *req, uint8_t len)
{
    if (len == 0 || len > sizeof(s_uds_req)) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    bool busy = s_state != COMSM_IDLE || s_uds_want || s_uds.state == COMSM_UDS_BUSY;
    if (!busy) {
        s_uds_tx = tx_id;
        s_uds_rx = rx_id;
        memcpy(s_uds_req, req, len);
        s_uds_len = len;
        s_uds.state = COMSM_UDS_BUSY;
        s_uds_want = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return busy ? ESP_ERR_INVALID_STATE : ESP_OK;
}

void comsm_uds_get(comsm_uds_result_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_uds;
    portEXIT_CRITICAL(&s_lock);
}

/* Idle + adapter link + wanted: keep the monitor running, pump it.
 * Returns true while it runs (the task then doesn't sleep on the queue). */
static bool monitor_service(void)
{
    /* not while diagnostics talk to the adapter (pause 3 s after the last request) */
    bool diag = s_uds_want || s_uds.state == COMSM_UDS_BUSY ||
                (int32_t)(can_if_now_ms() - (s_uds_last_ms + 3000)) < 0;
    bool want = s_mon_want && s_state == COMSM_IDLE && s_link_cfg == OBD_LINK_ELM && s_mon_cb != NULL && !diag;
    if (!want) {
        monitor_stop();
        return false;
    }
    if (obd_client_monitoring() &&
        (s_mon_filter != s_mon_applied_filter || s_mon_mask != s_mon_applied_mask)) {
        monitor_stop();                     /* filter changed (base ID): start again */
    }
    if (!obd_client_monitoring()) {
        uint32_t now = can_if_now_ms();
        if ((int32_t)(now - s_mon_retry_ms) < 0) {
            return false;
        }
        obd_client_set_link(OBD_LINK_ELM);
        s_mon_applied_filter = s_mon_filter;
        s_mon_applied_mask = s_mon_mask;
        if (obd_client_monitor_start(s_mon_applied_filter, s_mon_applied_mask) != ESP_OK) {
            s_mon_retry_ms = now + 2000;    /* adapter not connected yet: try again later */
            return false;
        }
        status_update(st_monitoring, 1);
    }
    if (!obd_client_monitor_poll(20, (obd_frame_cb_t)s_mon_cb, s_mon_arg)) {
        status_update(st_monitoring, 0);
        s_mon_retry_ms = can_if_now_ms() + 2000;
        return false;
    }
    return true;
}

void comsm_set_link_monitor(bool on, uint32_t filter, uint32_t mask, comsm_frame_cb_t cb, void *arg)
{
    if (on) {
        s_mon_cb = cb;
        s_mon_arg = arg;
        s_mon_filter = filter;
        s_mon_mask = mask;
    }
    s_mon_want = on;
}

static void session_end(const char *why);

static void start_connect(void)
{
    session_end("OBD connect");
    monitor_stop();                         /* OBD has the adapter now */
    obd_client_abort();
    obd_client_set_link((obd_link_t)s_link_cfg);
    obd_client_set_functional();
    obd_pid_reset_supported();
    status_update(st_reset, 0);
    s_connect_attempts = 0;
    s_consecutive_timeouts = 0;
    s_pending = REQ_NONE;
    set_state(COMSM_CONNECTING);
}

static void start_running(void)
{
    uint16_t n = obd_pid_supported_count();
    status_update(st_supported, n);
    ESP_LOGI(TAG, "discovery complete: %u known PIDs supported", n);
    s_consecutive_timeouts = 0;
    s_batch_applied = 0;                    /* (re)apply the setting in step_running */
    s_rate_window_start = can_if_now_ms();
    s_rate_window_count = 0;
    set_state(COMSM_RUNNING);
    if (s_deferred_job >= 0) {
        diag_start((diag_job_t)s_deferred_job);    /* Read pressed while not connected */
        s_deferred_job = -1;
    }
}

/* Response to 01 00/20/40...: apply bitmap, continue with next range or finish. */
static void handle_support_response(const uint8_t *r, uint16_t len)
{
    if (len < 6) {
        start_running();
        return;
    }
    uint32_t bitmap = ((uint32_t)r[2] << 24) | ((uint32_t)r[3] << 16) | ((uint32_t)r[4] << 8) | r[5];
    ESP_LOGI(TAG, "PIDs 0x%02X: %08lX", r[1], (unsigned long)bitmap);

    if (obd_pid_apply_support_bitmap(r[1], bitmap)) {
        s_discovery_pid = r[1] + 0x20;
        set_state(COMSM_DISCOVERY);
        send_request(REQ_DISCOVERY, 0x01, s_discovery_pid);
    } else {
        start_running();
    }
}

/* ---------------------------------------------------------------- per-state steps */

/* The ECU doesn't handle multi-PID requests: one PID per request until the
 * next connect or setting change. */
static void multi_pid_rejected(const char *why)
{
    if (s_batch_limit > 1) {
        ESP_LOGW(TAG, "multi-PID request %s, using one PID per request", why);
        s_batch_limit = 1;
        status_update(st_batch, 1);
    }
}

static void on_request_finished(obd_req_status_t st)
{
    req_kind_t kind = s_pending;
    s_pending = REQ_NONE;

    uint16_t len = 0;
    const uint8_t *r = obd_client_response(&len);

    switch (kind) {
    case REQ_PROBE:
        if (st == OBD_REQ_DONE) {
            uint32_t ecu = obd_client_responder_id();
            ESP_LOGI(TAG, "ECU 0x%03lX answered", (unsigned long)ecu);
            status_update(st_ecu, ecu);
            obd_client_set_physical(ecu);
            handle_support_response(r, len);
        } else if (++s_connect_attempts >= CONNECT_ATTEMPTS) {
            ESP_LOGW(TAG, "no ECU response after %d attempts", CONNECT_ATTEMPTS);
            go_retry_wait();
        }
        break;

    case REQ_DISCOVERY:
        if (st == OBD_REQ_DONE) {
            handle_support_response(r, len);
        } else {
            /* Extended range not answered: carry on with what we have. */
            start_running();
        }
        break;

    case REQ_POLL: {
        size_t asked = s_pending_n;
        if (st == OBD_REQ_DONE) {
            s_consecutive_timeouts = 0;
            size_t got = obd_pid_handle_response(r, len);
            s_rate_window_count += got;
            status_update(st_response, 0);
            if (asked > 1 && got < asked) {
                multi_pid_rejected("answered only part of it");
            }
        } else if (st == OBD_REQ_NEGATIVE) {
            s_consecutive_timeouts = 0;
            uint8_t nrc = obd_client_nrc();
            if (asked > 1) {
                multi_pid_rejected("negative response");
            } else if (asked == 1 && (nrc == 0x12 || nrc == 0x31)) {   /* sub-function / out of range */
                obd_pid_mark_unsupported(obd_pid_desc((size_t)s_pending_idx[0])->pid);
            }
        } else {
            status_update(st_timeout, 0);
            if (asked > 1) {
                multi_pid_rejected("no response");
            }
            if (++s_consecutive_timeouts >= CONFIG_OBD_MAX_CONSECUTIVE_TIMEOUTS) {
                ESP_LOGW(TAG, "connection lost (%d timeouts in a row)", s_consecutive_timeouts);
                go_retry_wait();
            }
        }
        break;
    }

    case REQ_DIAG:
        diag_on_result(st, r, len, obd_client_nrc());
        break;

    case REQ_UDS: {
        comsm_uds_state_t us = st == OBD_REQ_DONE ? COMSM_UDS_DONE
                             : st == OBD_REQ_NEGATIVE ? COMSM_UDS_NEGATIVE
                             : st == OBD_REQ_TIMEOUT ? COMSM_UDS_TIMEOUT : COMSM_UDS_ERROR;
        portENTER_CRITICAL(&s_lock);
        s_uds.state = us;
        s_uds.nrc = obd_client_nrc();
        s_uds.len = st == OBD_REQ_DONE ? (len > COMSM_UDS_MAX ? COMSM_UDS_MAX : len) : 0;
        if (s_uds.len) {
            memcpy(s_uds.data, r, s_uds.len);
        }
        snprintf(s_uds.error, sizeof(s_uds.error), "%s", us == COMSM_UDS_ERROR ? obd_client_link_error() : "");
        s_uds.seq++;
        portEXIT_CRITICAL(&s_lock);
        s_uds_last_ms = can_if_now_ms();
        session_track(us == COMSM_UDS_DONE);
        obd_client_set_functional();        /* hardware filter back to OBD */
        break;
    }

    case REQ_KEEP:                          /* 3E 80: no answer expected */
        if (st == OBD_REQ_NEGATIVE) {
            ESP_LOGW(TAG, "tester present refused (NRC %02X)", obd_client_nrc());
        }
        obd_client_set_functional();
        break;

    case REQ_NONE:
        break;
    }
}

static void step_running(uint32_t now)
{
    /* Diagnostic jobs take priority over live data polling. */
    if (diag_active()) {
        uint8_t req[4];
        uint16_t len;
        if (diag_next_request(req, &len)) {
            send_raw(REQ_DIAG, req, len);
            return;
        }
    }

    /* Setting changed (or new connection): try it, falling back again if rejected. */
    if (s_batch_applied != s_batch_cfg) {
        s_batch_applied = s_batch_cfg;
        s_batch_limit = s_batch_applied;
        status_update(st_batch, s_batch_limit);
    }
    size_t n = obd_pid_next_batch(now, s_batch_limit, s_pending_idx);
    /* ELM link: keep the answer in one CAN frame, the adapter's fast path
     * (the PIDs left out are simply still due next time). */
    uint16_t max_bytes = obd_client_batch_bytes();
    if (max_bytes && n > 1) {
        unsigned total = 1;                     /* 0x41 */
        size_t k = 0;
        for (; k < n; k++) {
            unsigned add = 1u + obd_pid_desc((size_t)s_pending_idx[k])->len;
            if (k > 0 && total + add > max_bytes) {
                break;
            }
            total += add;
        }
        n = k;
    }
    if (n == 0) {
#if CONFIG_OBD_KEEPALIVE_MS > 0
        /* Optional: detect a lost ECU while nothing is polled. */
        if (now - s_last_tx_ms >= CONFIG_OBD_KEEPALIVE_MS) {
            s_pending_n = 0;
            send_request(REQ_POLL, 0x01, 0x00);
            return;
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(5));
        return;
    }
    uint8_t req[1 + OBD_MAX_PIDS_PER_REQUEST] = { 0x01 };
    for (size_t i = 0; i < n; i++) {
        obd_pid_mark_polled((size_t)s_pending_idx[i], now);
        req[1 + i] = obd_pid_desc((size_t)s_pending_idx[i])->pid;
    }
    s_pending_n = n;
    send_raw(REQ_POLL, req, (uint16_t)(1 + n));
}

static void update_rate(uint32_t now)
{
    uint32_t dt = now - s_rate_window_start;
    if (dt >= 1000) {
        status_update(st_rate, (s_rate_window_count * 10000u) / dt);
        s_rate_window_start = now;
        s_rate_window_count = 0;
    }
}

static void step(void)
{
    if (s_pending != REQ_NONE) {
        obd_req_status_t st = obd_client_process(5);
        if (st != OBD_REQ_PENDING) {
            on_request_finished(st);
        }
        return;
    }

    uint32_t now = can_if_now_ms();
    switch (s_state) {
    case COMSM_IDLE:
        break;
    case COMSM_CONNECTING:
        send_request(REQ_PROBE, 0x01, 0x00);
        break;
    case COMSM_DISCOVERY:
        /* Waiting for a request issued in handle_support_response(). */
        break;
    case COMSM_RUNNING:
        update_rate(now);
        step_running(now);
        break;
    case COMSM_RETRY_WAIT:
        if ((int32_t)(now - s_retry_at_ms) >= 0) {
            start_connect();
        }
        break;
    }
}

static void handle_cmd(cmd_t cmd)
{
    switch (cmd) {
    case CMD_CONNECT:
        if (s_state == COMSM_IDLE) {
            start_connect();
        }
        break;
    case CMD_DISCONNECT:
        go_idle();
        break;
    case CMD_TOGGLE:
        if (s_state == COMSM_IDLE) {
            start_connect();
        } else {
            go_idle();
        }
        break;
    case CMD_READ_DTC:
    case CMD_CLEAR_DTC:
    case CMD_FREEZE_FRAME: {
        diag_job_t job = cmd == CMD_READ_DTC  ? DIAG_JOB_READ_DTC
                       : cmd == CMD_CLEAR_DTC ? DIAG_JOB_CLEAR_DTC
                                              : DIAG_JOB_FREEZE_FRAME;
        if (s_state == COMSM_RUNNING) {
            diag_start(job);
        } else {
            /* Not connected: connect first, the job runs once the ECU is found. */
            if (s_state == COMSM_IDLE) {
                start_connect();
            }
            s_deferred_job = job;
            diag_wait(job);
        }
        break;
    }
    }
}

static void comsm_task(void *arg)
{
    while (1) {
        /* Sleep on the command queue while there is nothing to do on the bus. */
        uds_service();
        keep_service();
        bool busy = (s_state != COMSM_IDLE && s_state != COMSM_RETRY_WAIT) || s_pending != REQ_NONE;
        if (!busy && monitor_service()) {
            busy = true;                    /* the monitor poll waits for the adapter itself */
        }
        TickType_t wait = busy ? 0 : pdMS_TO_TICKS(50);

        cmd_t cmd;
        while (xQueueReceive(s_cmd_queue, &cmd, wait) == pdTRUE) {
            handle_cmd(cmd);
            wait = 0;
        }
        step();
    }
}

/* ---------------------------------------------------------------- public API */

static void post(cmd_t cmd)
{
    if (s_cmd_queue && xQueueSend(s_cmd_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "command queue full");
    }
}

void comsm_connect(void)    { post(CMD_CONNECT); }
void comsm_disconnect(void) { post(CMD_DISCONNECT); }
void comsm_toggle(void)     { post(CMD_TOGGLE); }
void comsm_read_dtcs(void)  { post(CMD_READ_DTC); }
void comsm_clear_dtcs(void) { post(CMD_CLEAR_DTC); }
void comsm_read_freeze_frame(void) { post(CMD_FREEZE_FRAME); }

void comsm_set_pids_per_request(uint8_t n)
{
    if (n < 1) {
        n = 1;
    } else if (n > OBD_MAX_PIDS_PER_REQUEST) {
        n = OBD_MAX_PIDS_PER_REQUEST;
    }
    /* Applied before the next request; also retries after an ECU rejected it. */
    s_batch_cfg = n;
}

void comsm_set_link(uint8_t link)
{
    s_link_cfg = link == OBD_LINK_ELM ? OBD_LINK_ELM : OBD_LINK_CAN;
}

void comsm_get_status(comsm_status_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t comsm_init(void)
{
    s_cmd_queue = xQueueCreate(8, sizeof(cmd_t));
    if (s_cmd_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = obd_client_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "obd_client_init: %s", esp_err_to_name(err));
        return err;
    }
    obd_pid_reset_supported();
    s_state = COMSM_IDLE;
    s_status.state = COMSM_IDLE;

    /* Stack in PSRAM (internal RAM is needed by Wi-Fi / BLE). Allowed because
     * this task never writes flash; it only pauses while someone else does. */
    if (xTaskCreatePinnedToCoreWithCaps(comsm_task, "comsm", TASK_STACK, NULL, TASK_PRIO, NULL,
                                        tskNO_AFFINITY, MALLOC_CAP_SPIRAM) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
