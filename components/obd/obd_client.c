#include "obd_client.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "can_if.h"
#include "isotp.h"
#include "sdkconfig.h"

static const char *TAG = "obd_client";

#define RX_QUEUE_LEN        32
#define NRC_RESPONSE_PENDING 0x78

static QueueHandle_t s_rx_queue;
static IsoTpLink     s_link;
static uint8_t       s_tx_buf[64];
static uint8_t       s_rx_buf[OBD_MAX_RESPONSE];

static uint32_t      s_tx_id;           /* 0x7DF (functional) or 0x7E0..0x7E7 */
static uint32_t      s_phys_rx_id;      /* 0 in functional mode */
static uint32_t      s_locked_rx_id;    /* responder for the current request */

static obd_req_status_t s_status;
static uint8_t       s_req[8];
static uint16_t      s_req_len;
static uint32_t      s_deadline_ms;

static uint8_t       s_resp[OBD_MAX_RESPONSE];
static uint16_t      s_resp_len;
static uint8_t       s_nrc;

esp_err_t obd_client_init(void)
{
    s_rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(can_frame_t));
    if (s_rx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    obd_client_set_functional();
    return can_if_subscribe(OBD_ECU_RESP_ID_MIN, OBD_ECU_RESP_ID_MAX, s_rx_queue);
}

void obd_client_set_functional(void)
{
    s_tx_id = OBD_FUNCTIONAL_REQ_ID;
    s_phys_rx_id = 0;
}

void obd_client_set_physical(uint32_t ecu_resp_id)
{
    s_phys_rx_id = ecu_resp_id;
    s_tx_id = ecu_resp_id - 8;
    ESP_LOGI(TAG, "physical addressing: tx 0x%03lX rx 0x%03lX",
             (unsigned long)s_tx_id, (unsigned long)s_phys_rx_id);
}

static void finish(obd_req_status_t st)
{
    s_status = st;
}

esp_err_t obd_client_request(const uint8_t *req, uint16_t len)
{
    if (s_status == OBD_REQ_PENDING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (req == NULL || len == 0 || len > sizeof(s_req)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Throw away anything left over from a previous (timed out) exchange. */
    xQueueReset(s_rx_queue);
    isotp_init_link(&s_link, s_tx_id, s_tx_buf, sizeof(s_tx_buf), s_rx_buf, sizeof(s_rx_buf));

    memcpy(s_req, req, len);
    s_req_len = len;
    s_resp_len = 0;
    s_nrc = 0;
    s_locked_rx_id = s_phys_rx_id;
    s_deadline_ms = can_if_now_ms() + CONFIG_OBD_RESPONSE_TIMEOUT_MS;
    s_status = OBD_REQ_PENDING;

    if (isotp_send(&s_link, req, len) != ISOTP_RET_OK) {
        finish(OBD_REQ_ERROR);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void obd_client_abort(void)
{
    s_status = OBD_REQ_IDLE;
}

/* Does `r` answer the outstanding request? Handles positive and negative responses. */
static void evaluate_response(const uint8_t *r, uint16_t len)
{
    const uint8_t sid = s_req[0];

    if (len >= 3 && r[0] == 0x7F && r[1] == sid) {
        if (r[2] == NRC_RESPONSE_PENDING) {
            s_deadline_ms = can_if_now_ms() + 5000;   /* P2* extended timeout */
            return;
        }
        s_nrc = r[2];
        finish(OBD_REQ_NEGATIVE);
        return;
    }

    if (len < 1 || r[0] != (uint8_t)(sid + 0x40)) {
        ESP_LOGD(TAG, "ignoring unrelated response 0x%02X", len ? r[0] : 0);
        return;
    }
    /* Services with a PID / info type echo it back. */
    if ((sid == 0x01 || sid == 0x02 || sid == 0x09) && s_req_len >= 2) {
        if (len < 2 || r[1] != s_req[1]) {
            ESP_LOGD(TAG, "ignoring response for PID 0x%02X", len >= 2 ? r[1] : 0);
            return;
        }
    }

    memcpy(s_resp, r, len);
    s_resp_len = len;
    finish(OBD_REQ_DONE);
}

obd_req_status_t obd_client_process(uint32_t wait_ms)
{
    /* Terminal states are reported exactly once. */
    if (s_status != OBD_REQ_PENDING) {
        obd_req_status_t st = s_status;
        s_status = OBD_REQ_IDLE;
        if (st == OBD_REQ_IDLE) {
            /* Still drain the queue so stale frames never pile up. */
            xQueueReset(s_rx_queue);
        }
        return st;
    }

    can_frame_t f;
    TickType_t wait = pdMS_TO_TICKS(wait_ms);
    while (xQueueReceive(s_rx_queue, &f, wait) == pdTRUE) {
        wait = 0;
        if (s_locked_rx_id == 0) {
            s_locked_rx_id = f.id;          /* functional: lock onto first responder */
        } else if (f.id != s_locked_rx_id) {
            continue;                       /* another ECU, ignore */
        }
        isotp_on_can_message_with_id(&s_link, f.id, f.data, f.dlc);

        uint16_t out = 0;
        static uint8_t msg[OBD_MAX_RESPONSE];
        if (isotp_receive(&s_link, msg, sizeof(msg), &out) == ISOTP_RET_OK) {
            evaluate_response(msg, out);
            if (s_status != OBD_REQ_PENDING) {
                break;
            }
        }
    }

    isotp_poll(&s_link);

    if (s_status == OBD_REQ_PENDING) {
        if (s_link.send_status == ISOTP_SEND_STATUS_ERROR) {
            finish(OBD_REQ_ERROR);
        } else if ((int32_t)(can_if_now_ms() - s_deadline_ms) > 0) {
            finish(OBD_REQ_TIMEOUT);
        }
    }

    if (s_status == OBD_REQ_PENDING) {
        return OBD_REQ_PENDING;
    }
    obd_req_status_t st = s_status;
    s_status = OBD_REQ_IDLE;
    return st;
}

const uint8_t *obd_client_response(uint16_t *len)
{
    if (len) {
        *len = s_resp_len;
    }
    return s_resp;
}

uint32_t obd_client_responder_id(void)
{
    return s_locked_rx_id;
}

uint8_t obd_client_nrc(void)
{
    return s_nrc;
}
