#include "can_if.h"

#include <string.h>
#include <inttypes.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "can_if";

#define TX_QUEUE_LEN        16
#define TX_DONE_TIMEOUT_MS  50
/* On this chip (TWAI v1) any fail_retry_cnt other than -1 means single shot:
 * one attempt, dropped on lost arbitration or a bus error. On a busy car bus
 * our low-priority IDs (0x7DF / 0x7E0) lose arbitration regularly, so failed
 * attempts are retried here. Unlimited hardware retries (-1) are not used:
 * the driver can't abort a frame nobody acknowledges (no ECU on the bus). */
#define TX_ATTEMPTS         16
#define TX_RETRY_WINDOW_MS  20
#define TX_TASK_STACK       3072
#define TX_TASK_PRIO        9

typedef struct {
    uint32_t      id_min;
    uint32_t      id_max;
    QueueHandle_t queue;
} subscriber_t;

static twai_node_handle_t s_node;          /* NULL while stopped */
/* Held by the TX task while it uses the node and by start/stop while they
 * create / delete it, so the node never disappears under a transmit. */
static SemaphoreHandle_t  s_node_lock;
static TaskHandle_t       s_tx_task;
static can_if_config_t    s_cfg;
static QueueHandle_t      s_tx_queue;
static SemaphoreHandle_t  s_tx_done;
static volatile bool      s_tx_ok;
static volatile bool      s_need_recover;
static volatile uint8_t   s_want_accept_all;    /* CAN_IF_ALL_* owners */
static bool               s_accept_all;         /* applied state, TX task only */
static uint32_t           s_filter_id, s_filter_mask;
static bool               s_listen_only;

static subscriber_t       s_subs[CAN_IF_MAX_SUBSCRIBERS];
static volatile uint8_t   s_sub_count;
static QueueHandle_t volatile s_monitor[CAN_IF_MON_COUNT];

static can_if_stats_t     s_stats;
static portMUX_TYPE       s_stats_lock = portMUX_INITIALIZER_UNLOCKED;

uint32_t can_if_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ---------------------------------------------------------------- ISR callbacks */

/* Receive filter of the monitor taps (IO page), mask 0 = everything. */
static volatile uint32_t s_mon_fid, s_mon_fmask;

static inline bool mon_pass(const can_frame_t *f)
{
    uint32_t m = s_mon_fmask;
    if (m == 0) {
        return true;
    }
    return !f->ext && ((f->id ^ s_mon_fid) & m) == 0;
}

void can_if_set_monitor_filter(uint32_t id, uint32_t mask)
{
    s_mon_fmask = 0;
    s_mon_fid = id & 0x7FF;
    s_mon_fmask = mask & 0x7FF;
}

void can_if_inject_monitor(const can_frame_t *f)
{
    if (!mon_pass(f)) {
        return;
    }
    for (int i = 0; i < CAN_IF_MON_COUNT; i++) {
        QueueHandle_t q = s_monitor[i];
        if (q != NULL && xQueueSend(q, f, 0) != pdTRUE) {
            portENTER_CRITICAL(&s_stats_lock);
            s_stats.mon_dropped++;
            portEXIT_CRITICAL(&s_stats_lock);
        }
    }
}

static void monitor_push_from_isr(const can_frame_t *f, BaseType_t *woken)
{
    if (!mon_pass(f)) {
        return;
    }
    for (int i = 0; i < CAN_IF_MON_COUNT; i++) {
        QueueHandle_t q = s_monitor[i];
        if (q != NULL && xQueueSendFromISR(q, f, woken) != pdTRUE) {
            portENTER_CRITICAL_ISR(&s_stats_lock);
            s_stats.mon_dropped++;
            portEXIT_CRITICAL_ISR(&s_stats_lock);
        }
    }
}

static bool on_rx_done(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *ctx)
{
    uint8_t buf[CAN_IF_MAX_DLC];
    twai_frame_t rx = { .buffer = buf, .buffer_len = sizeof(buf) };
    BaseType_t woken = pdFALSE;

    if (twai_node_receive_from_isr(handle, &rx) != ESP_OK) {
        return false;
    }

    can_frame_t f = {
        .id    = rx.header.id,
        .ts_ms = can_if_now_ms(),
        .dlc   = rx.header.dlc > CAN_IF_MAX_DLC ? CAN_IF_MAX_DLC : rx.header.dlc,
        .ext   = rx.header.ide,
        .tx    = false,
    };
    memcpy(f.data, buf, f.dlc);

    portENTER_CRITICAL_ISR(&s_stats_lock);
    s_stats.rx_frames++;
    portEXIT_CRITICAL_ISR(&s_stats_lock);

    monitor_push_from_isr(&f, &woken);

    for (uint8_t i = 0; i < s_sub_count; i++) {
        if (f.id >= s_subs[i].id_min && f.id <= s_subs[i].id_max) {
            if (xQueueSendFromISR(s_subs[i].queue, &f, &woken) != pdTRUE) {
                portENTER_CRITICAL_ISR(&s_stats_lock);
                s_stats.rx_dropped++;
                portEXIT_CRITICAL_ISR(&s_stats_lock);
            }
        }
    }
    return woken == pdTRUE;
}

static bool on_tx_done(twai_node_handle_t handle, const twai_tx_done_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    s_tx_ok = edata->is_tx_success;
    xSemaphoreGiveFromISR(s_tx_done, &woken);
    return woken == pdTRUE;
}

static bool on_state_change(twai_node_handle_t handle, const twai_state_change_event_data_t *edata, void *ctx)
{
    can_if_bus_state_t st;
    switch (edata->new_sta) {
        case TWAI_ERROR_ACTIVE:  st = CAN_IF_BUS_ACTIVE;  break;
        case TWAI_ERROR_WARNING: st = CAN_IF_BUS_WARNING; break;
        case TWAI_ERROR_PASSIVE: st = CAN_IF_BUS_PASSIVE; break;
        default:                 st = CAN_IF_BUS_OFF;     break;
    }
    portENTER_CRITICAL_ISR(&s_stats_lock);
    s_stats.state = st;
    portEXIT_CRITICAL_ISR(&s_stats_lock);

    if (st == CAN_IF_BUS_OFF) {
        s_need_recover = true;      /* handled in TX task, recovery is not ISR safe */
    }
    return false;
}

static bool on_error(twai_node_handle_t handle, const twai_error_event_data_t *edata, void *ctx)
{
    portENTER_CRITICAL_ISR(&s_stats_lock);
    s_stats.bus_errors++;
    portEXIT_CRITICAL_ISR(&s_stats_lock);
    return false;
}

/* ---------------------------------------------------------------- TX task */

/* Mask 0 = accept all. The driver only takes a filter while the node is stopped. */
static esp_err_t set_mask(uint32_t id, uint32_t mask)
{
    twai_mask_filter_config_t flt = { .id = id, .mask = mask, .is_ext = false };
    return twai_node_config_mask_filter(s_node, 0, &flt);
}

static void apply_filter(bool accept_all)
{
    esp_err_t err = twai_node_disable(s_node);
    if (err == ESP_OK) {
        err = accept_all ? set_mask(0, 0) : set_mask(s_filter_id, s_filter_mask);
        esp_err_t err2 = twai_node_enable(s_node);
        err = (err != ESP_OK) ? err : err2;
    }
    s_accept_all = accept_all;      /* don't retry in a loop on failure */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "filter change failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "receive filter: %s", accept_all ? "all IDs" : "configured filter");
}

static void can_tx_task(void *arg)
{
    /* esp_twai keeps a pointer to the frame until TX completes,
     * so the frame and its buffer live here and we wait for tx_done. */
    static can_frame_t  cur;
    static twai_frame_t tx;

    while (1) {
        if (s_node == NULL) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        xSemaphoreTake(s_node_lock, portMAX_DELAY);
        if (s_node == NULL) {
            xSemaphoreGive(s_node_lock);
            continue;
        }
        if (s_need_recover) {
            s_need_recover = false;
            ESP_LOGW(TAG, "bus-off, starting recovery");
            xQueueReset(s_tx_queue);
            esp_err_t err = twai_node_recover(s_node);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "recover failed: %s", esp_err_to_name(err));
            }
        }

        if ((s_want_accept_all != 0) != s_accept_all) {
            apply_filter(s_want_accept_all != 0);
        }

        if (xQueueReceive(s_tx_queue, &cur, pdMS_TO_TICKS(100)) != pdTRUE) {
            xSemaphoreGive(s_node_lock);
            continue;
        }

        bool ok = false;
        int attempts = 0;
        uint32_t start = can_if_now_ms();
        do {
            memset(&tx, 0, sizeof(tx));
            tx.header.id  = cur.id;
            tx.header.ide = cur.ext;
            tx.header.dlc = cur.dlc;
            tx.buffer     = cur.data;
            tx.buffer_len = cur.dlc;

            attempts++;
            xSemaphoreTake(s_tx_done, 0);   /* clear stale completion */
            esp_err_t err = twai_node_transmit(s_node, &tx, TX_DONE_TIMEOUT_MS);
            if (err != ESP_OK || xSemaphoreTake(s_tx_done, pdMS_TO_TICKS(TX_DONE_TIMEOUT_MS)) != pdTRUE) {
                break;      /* no completion: the driver may still own the frame, don't resend */
            }
            ok = s_tx_ok;
        } while (!ok && attempts < TX_ATTEMPTS && can_if_now_ms() - start < TX_RETRY_WINDOW_MS);

        xSemaphoreGive(s_node_lock);

        portENTER_CRITICAL(&s_stats_lock);
        s_stats.tx_retries += attempts - 1;
        if (ok) {
            s_stats.tx_frames++;
        } else {
            s_stats.tx_failed++;
        }
        portEXIT_CRITICAL(&s_stats_lock);

        if (ok) {
            cur.ts_ms = can_if_now_ms();
            cur.tx = true;
            for (int i = 0; i < CAN_IF_MON_COUNT && mon_pass(&cur); i++) {
                QueueHandle_t mon = s_monitor[i];
                if (mon != NULL && xQueueSend(mon, &cur, 0) != pdTRUE) {
                    portENTER_CRITICAL(&s_stats_lock);
                    s_stats.mon_dropped++;
                    portEXIT_CRITICAL(&s_stats_lock);
                }
            }
        }
    }
}

/* ---------------------------------------------------------------- public API */

esp_err_t can_if_send(const can_frame_t *frame, int timeout_ms)
{
    if (frame == NULL || frame->dlc > CAN_IF_MAX_DLC || s_tx_queue == NULL || s_node == NULL || s_listen_only) {
        return ESP_ERR_INVALID_ARG;
    }
    TickType_t to = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xQueueSend(s_tx_queue, frame, to) != pdTRUE) {
        portENTER_CRITICAL(&s_stats_lock);
        s_stats.tx_dropped++;
        portEXIT_CRITICAL(&s_stats_lock);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t can_if_subscribe(uint32_t id_min, uint32_t id_max, QueueHandle_t queue)
{
    if (queue == NULL || id_min > id_max) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_sub_count >= CAN_IF_MAX_SUBSCRIBERS) {
        return ESP_ERR_NO_MEM;
    }
    /* Fill the slot before publishing the new count to the ISR. */
    s_subs[s_sub_count] = (subscriber_t){ .id_min = id_min, .id_max = id_max, .queue = queue };
    __sync_synchronize();
    s_sub_count++;
    return ESP_OK;
}

void can_if_set_monitor(can_if_monitor_t slot, QueueHandle_t queue)
{
    if (slot < CAN_IF_MON_COUNT) {
        s_monitor[slot] = queue;
    }
}

void can_if_set_accept_all(uint8_t owner, bool on)
{
    portENTER_CRITICAL(&s_stats_lock);
    s_want_accept_all = on ? (s_want_accept_all | owner) : (s_want_accept_all & ~owner);
    portEXIT_CRITICAL(&s_stats_lock);
}

void can_if_get_stats(can_if_stats_t *out)
{
    portENTER_CRITICAL(&s_stats_lock);
    *out = s_stats;
    portEXIT_CRITICAL(&s_stats_lock);

    out->tec = out->rec = 0;
    if (s_node_lock != NULL && xSemaphoreTake(s_node_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        twai_node_status_t st;
        if (s_node != NULL && twai_node_get_info(s_node, &st, NULL) == ESP_OK) {
            out->tec = st.tx_error_count;
            out->rec = st.rx_error_count;
        }
        xSemaphoreGive(s_node_lock);
    }
}

void can_if_reset_stats(void)
{
    portENTER_CRITICAL(&s_stats_lock);
    can_if_bus_state_t st = s_stats.state;
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.state = st;
    portEXIT_CRITICAL(&s_stats_lock);
}

bool can_if_running(void)
{
    return s_node != NULL;
}

void can_if_get_config(can_if_config_t *out)
{
    *out = s_cfg;
}

/* Create, configure and enable the node. Called with s_node_lock held. */
static esp_err_t node_create(const can_if_config_t *cfg)
{
    twai_node_handle_t node = NULL;
    twai_onchip_node_config_t node_cfg = {
        .io_cfg = {
            .tx = CONFIG_CAN_TX,
            .rx = CONFIG_CAN_RX,
            .quanta_clk_out = -1,
            .bus_off_indicator = -1,
        },
        .bit_timing = { .bitrate = cfg->bitrate },
        .fail_retry_cnt = 3,        /* don't retry forever when nobody ACKs */
        .tx_queue_depth = 4,
        .flags.enable_listen_only = cfg->listen_only,
    };
    ESP_RETURN_ON_ERROR(twai_new_node_onchip(&node_cfg, &node), TAG, "node create");

    s_node = node;      /* set_mask() uses s_node */
    s_listen_only = cfg->listen_only;
    s_filter_id = cfg->filter_id;
    s_filter_mask = cfg->filter_mask;
    s_accept_all = false;           /* new node: OBD filter; the TX task re-applies "all IDs" if wanted */
    s_need_recover = false;

    twai_event_callbacks_t cbs = {
        .on_rx_done = on_rx_done,
        .on_tx_done = on_tx_done,
        .on_state_change = on_state_change,
        .on_error = on_error,
    };
    esp_err_t err = set_mask(s_filter_id, s_filter_mask);
    if (err == ESP_OK) {
        err = twai_node_register_event_callbacks(node, &cbs, NULL);
    }
    if (err == ESP_OK) {
        err = twai_node_enable(node);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "node setup failed: %s", esp_err_to_name(err));
        twai_node_delete(node);
        s_node = NULL;
        return err;
    }
    return ESP_OK;
}

esp_err_t can_if_start(const can_if_config_t *cfg)
{
    if (cfg == NULL || cfg->bitrate == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_node_lock == NULL) {
        s_node_lock = xSemaphoreCreateMutex();
        s_tx_queue  = xQueueCreate(TX_QUEUE_LEN, sizeof(can_frame_t));
        s_tx_done   = xSemaphoreCreateBinary();
        if (s_node_lock == NULL || s_tx_queue == NULL || s_tx_done == NULL) {
            return ESP_ERR_NO_MEM;
        }
        s_stats.state = CAN_IF_BUS_STOPPED;
    }

    can_if_stop();

    xSemaphoreTake(s_node_lock, portMAX_DELAY);
    esp_err_t err = node_create(cfg);
    if (err == ESP_OK) {
        s_cfg = *cfg;
        portENTER_CRITICAL(&s_stats_lock);
        s_stats.state = CAN_IF_BUS_ACTIVE;
        portEXIT_CRITICAL(&s_stats_lock);
    }
    xSemaphoreGive(s_node_lock);
    ESP_RETURN_ON_ERROR(err, TAG, "start");

    if (s_tx_task == NULL &&
        xTaskCreatePinnedToCore(can_tx_task, "can_tx", TX_TASK_STACK, NULL, TX_TASK_PRIO, &s_tx_task, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "started: %" PRIu32 " bit/s, filter 0x%03" PRIX32 "/0x%03" PRIX32 "%s",
             cfg->bitrate, cfg->filter_id, cfg->filter_mask, cfg->listen_only ? ", listen only" : "");
    return ESP_OK;
}

void can_if_stop(void)
{
    if (s_node_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_node_lock, portMAX_DELAY);
    if (s_node != NULL) {
        twai_node_handle_t node = s_node;
        s_node = NULL;
        twai_node_disable(node);
        twai_node_delete(node);
        xQueueReset(s_tx_queue);
        ESP_LOGI(TAG, "stopped");
    }
    portENTER_CRITICAL(&s_stats_lock);
    s_stats.state = CAN_IF_BUS_STOPPED;
    portEXIT_CRITICAL(&s_stats_lock);
    xSemaphoreGive(s_node_lock);
}

esp_err_t can_if_init(const can_if_config_t *cfg)
{
    return can_if_start(cfg);
}
