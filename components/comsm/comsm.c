#include "comsm.h"

#include "esp_log.h"
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

typedef enum { REQ_NONE, REQ_PROBE, REQ_DISCOVERY, REQ_POLL, REQ_DIAG } req_kind_t;

static QueueHandle_t  s_cmd_queue;
static comsm_status_t s_status;
static portMUX_TYPE   s_lock = portMUX_INITIALIZER_UNLOCKED;

/* task-private state */
static comsm_state_t  s_state;
static req_kind_t     s_pending;
static int            s_pending_idx[OBD_MAX_PIDS_PER_REQUEST]; /* catalogue indexes for REQ_POLL */
static size_t         s_pending_n;          /* 0 = keepalive */
static volatile uint8_t s_batch_cfg = 1;    /* PIDs per request wanted (UI) */
static uint8_t        s_batch_limit;        /* what this ECU accepts, <= s_batch_cfg */
static uint8_t        s_batch_applied;      /* s_batch_cfg value s_batch_limit was set from */
static uint8_t        s_discovery_pid;
static uint8_t        s_connect_attempts;
static uint8_t        s_consecutive_timeouts;
static uint32_t       s_retry_at_ms;
static uint32_t       s_rate_window_start;
static uint32_t       s_rate_window_count;
static uint32_t       s_last_tx_ms;

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

static void go_idle(void)
{
    diag_abort("Disconnected");
    obd_client_abort();
    s_pending = REQ_NONE;
    vd_invalidate_all();
    status_update(st_reset, 0);
    set_state(COMSM_IDLE);
}

static void go_retry_wait(void)
{
    diag_abort("Connection lost");
    obd_client_abort();
    s_pending = REQ_NONE;
    vd_invalidate_all();
    s_retry_at_ms = can_if_now_ms() + CONFIG_OBD_RECONNECT_DELAY_MS;
    set_state(COMSM_RETRY_WAIT);
}

static void start_connect(void)
{
    obd_client_abort();
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
            diag_reject(job, "Not connected");
        }
        break;
    }
    }
}

static void comsm_task(void *arg)
{
    while (1) {
        /* Sleep on the command queue while there is nothing to do on the bus. */
        bool busy = (s_state != COMSM_IDLE && s_state != COMSM_RETRY_WAIT) || s_pending != REQ_NONE;
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

    if (xTaskCreate(comsm_task, "comsm", TASK_STACK, NULL, TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
