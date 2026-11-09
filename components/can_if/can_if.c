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
#define TX_TASK_STACK       3072
#define TX_TASK_PRIO        9

typedef struct {
    uint32_t      id_min;
    uint32_t      id_max;
    QueueHandle_t queue;
} subscriber_t;

static twai_node_handle_t s_node;
static QueueHandle_t      s_tx_queue;
static SemaphoreHandle_t  s_tx_done;
static volatile bool      s_tx_ok;
static volatile bool      s_need_recover;

static subscriber_t       s_subs[CAN_IF_MAX_SUBSCRIBERS];
static volatile uint8_t   s_sub_count;
static QueueHandle_t volatile s_monitor;

static can_if_stats_t     s_stats;
static portMUX_TYPE       s_stats_lock = portMUX_INITIALIZER_UNLOCKED;

uint32_t can_if_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ---------------------------------------------------------------- ISR callbacks */

static void monitor_push_from_isr(const can_frame_t *f, BaseType_t *woken)
{
    QueueHandle_t q = s_monitor;
    if (q != NULL && xQueueSendFromISR(q, f, woken) != pdTRUE) {
        portENTER_CRITICAL_ISR(&s_stats_lock);
        s_stats.mon_dropped++;
        portEXIT_CRITICAL_ISR(&s_stats_lock);
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

static void can_tx_task(void *arg)
{
    /* esp_twai keeps a pointer to the frame until TX completes,
     * so the frame and its buffer live here and we wait for tx_done. */
    static can_frame_t  cur;
    static twai_frame_t tx;

    while (1) {
        if (s_need_recover) {
            s_need_recover = false;
            ESP_LOGW(TAG, "bus-off, starting recovery");
            xQueueReset(s_tx_queue);
            esp_err_t err = twai_node_recover(s_node);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "recover failed: %s", esp_err_to_name(err));
            }
        }

        if (xQueueReceive(s_tx_queue, &cur, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        memset(&tx, 0, sizeof(tx));
        tx.header.id  = cur.id;
        tx.header.ide = cur.ext;
        tx.header.dlc = cur.dlc;
        tx.buffer     = cur.data;
        tx.buffer_len = cur.dlc;

        xSemaphoreTake(s_tx_done, 0);   /* clear stale completion */
        esp_err_t err = twai_node_transmit(s_node, &tx, TX_DONE_TIMEOUT_MS);
        bool ok = false;
        if (err == ESP_OK && xSemaphoreTake(s_tx_done, pdMS_TO_TICKS(TX_DONE_TIMEOUT_MS)) == pdTRUE) {
            ok = s_tx_ok;
        }

        portENTER_CRITICAL(&s_stats_lock);
        if (ok) {
            s_stats.tx_frames++;
        } else {
            s_stats.tx_failed++;
        }
        portEXIT_CRITICAL(&s_stats_lock);

        QueueHandle_t mon = s_monitor;
        if (ok && mon != NULL) {
            cur.ts_ms = can_if_now_ms();
            cur.tx = true;
            if (xQueueSend(mon, &cur, 0) != pdTRUE) {
                portENTER_CRITICAL(&s_stats_lock);
                s_stats.mon_dropped++;
                portEXIT_CRITICAL(&s_stats_lock);
            }
        }
    }
}

/* ---------------------------------------------------------------- public API */

esp_err_t can_if_send(const can_frame_t *frame, int timeout_ms)
{
    if (frame == NULL || frame->dlc > CAN_IF_MAX_DLC || s_tx_queue == NULL) {
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

void can_if_set_monitor(QueueHandle_t queue)
{
    s_monitor = queue;
}

void can_if_get_stats(can_if_stats_t *out)
{
    portENTER_CRITICAL(&s_stats_lock);
    *out = s_stats;
    portEXIT_CRITICAL(&s_stats_lock);
}

esp_err_t can_if_init(const can_if_config_t *cfg)
{
    s_stats.state = CAN_IF_BUS_STOPPED;

    s_tx_queue = xQueueCreate(TX_QUEUE_LEN, sizeof(can_frame_t));
    s_tx_done  = xSemaphoreCreateBinary();
    if (s_tx_queue == NULL || s_tx_done == NULL) {
        return ESP_ERR_NO_MEM;
    }

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
    };
    ESP_RETURN_ON_ERROR(twai_new_node_onchip(&node_cfg, &s_node), TAG, "node create");

    if (cfg->filter_mask != 0) {
        twai_mask_filter_config_t flt = {
            .id = cfg->filter_id,
            .mask = cfg->filter_mask,
            .is_ext = false,
        };
        ESP_RETURN_ON_ERROR(twai_node_config_mask_filter(s_node, 0, &flt), TAG, "filter");
    }

    twai_event_callbacks_t cbs = {
        .on_rx_done = on_rx_done,
        .on_tx_done = on_tx_done,
        .on_state_change = on_state_change,
        .on_error = on_error,
    };
    ESP_RETURN_ON_ERROR(twai_node_register_event_callbacks(s_node, &cbs, NULL), TAG, "callbacks");
    ESP_RETURN_ON_ERROR(twai_node_enable(s_node), TAG, "enable");
    s_stats.state = CAN_IF_BUS_ACTIVE;

    if (xTaskCreate(can_tx_task, "can_tx", TX_TASK_STACK, NULL, TX_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "started: %" PRIu32 " bit/s, filter 0x%03" PRIX32 "/0x%03" PRIX32,
             cfg->bitrate, cfg->filter_id, cfg->filter_mask);
    return ESP_OK;
}
