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

static const char *TAG = "comsm";

#define TASK_STACK          4096
#define TASK_PRIO           6
#define CONNECT_ATTEMPTS    3

typedef enum { CMD_CONNECT, CMD_DISCONNECT, CMD_TOGGLE } cmd_t;

typedef enum { REQ_NONE, REQ_PROBE, REQ_DISCOVERY, REQ_POLL } req_kind_t;

static QueueHandle_t  s_cmd_queue;
static comsm_status_t s_status;
static portMUX_TYPE   s_lock = portMUX_INITIALIZER_UNLOCKED;

/* task-private state */
static comsm_state_t  s_state;
static req_kind_t     s_pending;
static int            s_pending_idx;        /* catalogue index for REQ_POLL */
static uint8_t        s_discovery_pid;
static uint8_t        s_connect_attempts;
static uint8_t        s_consecutive_timeouts;
static uint32_t       s_retry_at_ms;
static uint32_t       s_rate_window_start;
static uint32_t       s_rate_window_count;

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

/* ---------------------------------------------------------------- transitions */

static void send_request(req_kind_t kind, uint8_t sid, uint8_t pid)
{
    uint8_t req[2] = { sid, pid };
    /* A send failure is reported as OBD_REQ_ERROR by the next obd_client_process(). */
    obd_client_request(req, sizeof(req));
    s_pending = kind;
}

static void go_idle(void)
{
    obd_client_abort();
    s_pending = REQ_NONE;
    vd_invalidate_all();
    status_update(st_reset, 0);
    set_state(COMSM_IDLE);
}

static void go_retry_wait(void)
{
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

    case REQ_POLL:
        if (st == OBD_REQ_DONE) {
            s_consecutive_timeouts = 0;
            s_rate_window_count++;
            status_update(st_response, 0);
            obd_pid_handle_response(r, len);
        } else if (st == OBD_REQ_NEGATIVE) {
            s_consecutive_timeouts = 0;
            uint8_t nrc = obd_client_nrc();
            const obd_pid_desc_t *d = obd_pid_desc(s_pending_idx);
            if (d && (nrc == 0x12 || nrc == 0x31)) {      /* sub-function / out of range */
                obd_pid_mark_unsupported(d->pid);
            }
        } else {
            status_update(st_timeout, 0);
            if (++s_consecutive_timeouts >= CONFIG_OBD_MAX_CONSECUTIVE_TIMEOUTS) {
                ESP_LOGW(TAG, "connection lost (%d timeouts in a row)", s_consecutive_timeouts);
                go_retry_wait();
            }
        }
        break;

    case REQ_NONE:
        break;
    }
}

static void step_running(uint32_t now)
{
    int idx = obd_pid_next_due(now);
    if (idx < 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
        return;
    }
    const obd_pid_desc_t *d = obd_pid_desc(idx);
    obd_pid_mark_polled(idx, now);
    s_pending_idx = idx;
    send_request(REQ_POLL, 0x01, d->pid);
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
