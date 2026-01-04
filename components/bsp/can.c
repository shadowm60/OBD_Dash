#include "can.h"
#include "isotp.h"

#define DBG_TICK

//todo: later we should probe or get a config on the baudrate to be used
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
static const twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);


static QueueHandle_t tx_task_queue;
static QueueHandle_t rx_task_queue;

static IsoTpLink g_link;

/* Alloc send and receive buffer statically in RAM */
static uint8_t g_isotpRecvBuf[ISOTP_BUFSIZE];
static uint8_t g_isotpSendBuf[ISOTP_BUFSIZE];

static const char *TAG = "CAN";

static void log_twai_status(void) {
    twai_status_info_t status;
    esp_err_t ret = twai_get_status_info(&status);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "TWAI Status:");
        ESP_LOGI(TAG, "State: %d", status.state);
        ESP_LOGI(TAG, "Msgs to TX: %d", status.msgs_to_tx);
        ESP_LOGI(TAG, "Msgs to RX: %d", status.msgs_to_rx);
        ESP_LOGI(TAG, "TX Error Counter: %d", status.tx_error_counter);
        ESP_LOGI(TAG, "RX Error Counter: %d", status.rx_error_counter);
        ESP_LOGI(TAG, "TX Failed: %d", status.tx_failed_count);
        ESP_LOGI(TAG, "RX Missed: %d", status.rx_missed_count);
        ESP_LOGI(TAG, "RX Overrun: %d", status.rx_overrun_count);
        //ESP_LOGI(TAG, "TX Success: %d", status.tx_successful_count);
        //ESP_LOGI(TAG, "RX Success: %d", status.rx_successful_count);
    } else {
        ESP_LOGE(TAG, "Failed to get TWAI status info: %s", esp_err_to_name(ret));
    }
}



uint32_t can_get_timestamp_ms ( void ) {
    TickType_t ticks = xTaskGetTickCount();
    uint32_t time_ms = ticks * portTICK_PERIOD_MS;

    return time_ms;
}

void can_send_message(twai_message_t *msg) {
    if (xQueueSend(tx_task_queue, msg, CAN_BLOCK_MAX) != pdPASS) {
        ESP_LOGW(TAG, "Failed to push sent msg to queue!");
    } else {
        //ESP_LOGI(TAG, "schedule msg for tx!");
    }
}

static void can_receive_task(void *arg) {
    while (1) {
        // Check if alert happened
        uint32_t alerts_triggered;                                           // Variable to hold triggered alerts
        twai_read_alerts(&alerts_triggered, pdMS_TO_TICKS(1));               // Read triggered alerts
        twai_status_info_t twaistatus;                                       // Variable to hold TWAI status information
        twai_get_status_info(&twaistatus);                                   // Get TWAI status information

        // Handle alerts
        if (alerts_triggered & TWAI_ALERT_ERR_PASS)
        {                                                                              // Check for error passive alert
            ESP_LOGI(TAG, "Alert: TWAI controller has become error passive."); // Log error passive alert
        }
        if (alerts_triggered & TWAI_ALERT_BUS_ERROR)
        {                                                                                                  // Check for bus error alert
            ESP_LOGI(TAG, "Alert: A (Bit, Stuff, CRC, Form, ACK) error has occurred on the bus."); // Log bus error alert
            ESP_LOGI(TAG, "Bus error count: %" PRIu32, twaistatus.bus_error_count);                // Log bus error count
        }

        if (alerts_triggered & TWAI_ALERT_RX_QUEUE_FULL)
        {                                                                                              // Check for RX queue full alert
            ESP_LOGI(TAG, "Alert: The RX queue is full causing a received frame to be lost."); // Log RX queue full alert
            ESP_LOGI(TAG, "RX buffered: %" PRIu32, twaistatus.msgs_to_rx);                     // Log buffered RX messages
            ESP_LOGI(TAG, "RX missed: %" PRIu32, twaistatus.rx_missed_count);                  // Log missed RX messages
            ESP_LOGI(TAG, "RX overrun %" PRIu32, twaistatus.rx_overrun_count);                 // Log RX overrun count
        }

        // Check if message is received
        if (alerts_triggered & TWAI_ALERT_RX_DATA)
        {   // If RX data alert is triggered
            // One or more messages received. Handle all.
            twai_message_t message; // Variable to hold received message
            while (twai_receive(&message, 0) == ESP_OK)
            {                               // Receive messages until none are left
                if (xQueueSend(rx_task_queue, &message, CAN_BLOCK_MAX) != pdPASS) {
                    ESP_LOGW(TAG, "Failed to push msg to queue!");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1)); 

    }
    vTaskDelete(NULL);
}

static void can_send_task(void *arg) {
    while (1) {
        twai_message_t msg;
        if (xQueueReceive(tx_task_queue, &msg, CAN_BLOCK_MAX) == pdPASS) {
            //ESP_LOGI(TAG, "Transmit");
            twai_transmit(&msg, CAN_BLOCK_MAX);
        }   
        vTaskDelay(pdMS_TO_TICKS(1));      
    }
    vTaskDelete(NULL);    
}

static void can_process_task(void *arg) {

    static uint32_t tstamp;
    uint32_t ctstamp;
    twai_message_t msg;

#ifdef DBG_TICK
    twai_message_t ping_message = {
        // Message type and format settings
        .extd = 0,              // Standard Format message (11-bit ID)
        .rtr = 0,               // Send a data frame
        .ss = 1,                // Is single shot (won't retry on error or NACK)
        .self = 0,              // Not a self reception request
        .dlc_non_comp = 0,      // DLC is less than 8
        // Message ID and payload
        .identifier = 0x700,
        .data_length_code = 1,
        .data = {0xaa},
        };    
    
    tstamp = can_get_timestamp_ms();
#endif

    while(1) {
        //vTaskDelay(pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(1)); 

        if (xQueueReceive(rx_task_queue, &msg, CAN_BLOCK_MAX) == pdPASS) {
            //process message
            if (ECU_CAN_ID == msg.identifier) {
                isotp_on_can_message(&g_link, msg.data, msg.data_length_code);
            }
        }

        //poll the isotp cyclic function
        isotp_poll(&g_link);
#ifdef DBG_TICK
        ctstamp = can_get_timestamp_ms();
        if (ctstamp > tstamp) {
            ctstamp = ctstamp - tstamp;
        } else {
            ctstamp = tstamp - ctstamp;
        }

        if (ctstamp > 1000) {
            tstamp = can_get_timestamp_ms();
            can_send_message(&ping_message);
#ifdef _PRINT_CAN_STATUS_            
            log_twai_status();
#endif
            //ESP_LOGI(TAG, "Tx Ping");

        }
#endif        
    }
    vTaskDelete(NULL);
}


void can_send_isotp_message( uint8_t *data, uint8_t len) {
    //ESP_LOGI(TAG, "can_send_isotp_message");
    isotp_send(&g_link, data, len);
}

int can_get_isotp_message(uint8_t *payload, const uint16_t payload_size, uint16_t *out_size) {
    int ret;
    ret = isotp_receive(&g_link, payload, payload_size, out_size);
    return ret;
}

void init_can( void ) {
    tx_task_queue = xQueueCreate(RX_QUEUE_SIZE, sizeof(twai_message_t));
    rx_task_queue = xQueueCreate(TX_QUEUE_SIZE, sizeof(twai_message_t));

    if ((tx_task_queue == NULL) || (rx_task_queue == NULL)) {
        ESP_LOGE(TAG, "Failed to create Queue");
    }

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(TAG, "Driver installed");

}

void init_can_tasks(void) {

    //ESP_ERROR_CHECK(twai_start());
    if (ESP_OK == twai_start()) {
        ESP_LOGI(TAG, "OK twai_start()");

        // Reconfigure alerts to detect frame receive, Bus-Off error and RX queue full states
        uint32_t alerts_to_enable = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL; // Configure alerts
        if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK)
        {
            ESP_LOGI(TAG, "CAN Alerts reconfigured"); // Log alert reconfiguration success
            
            xTaskCreate(can_receive_task, "TWAI_rx", 4096, NULL, RX_TASK_PRIO, NULL);
            xTaskCreate(can_send_task, "TWAI_tx", 4096, NULL, TX_TASK_PRIO, NULL);
            xTaskCreate(can_process_task, "TWAI_ctrl", 4096, NULL, PROC_TSK_PRIO, NULL);
    
            ESP_LOGI(TAG, "TASKS created!");        

            //setup isotp
            isotp_init_link(&g_link, TESTER_CAN_ID,
                            g_isotpSendBuf, sizeof(g_isotpSendBuf), 
					        g_isotpRecvBuf, sizeof(g_isotpRecvBuf));
            ESP_LOGI(TAG, "ISOTP Init done");        
            ESP_LOGI(TAG, "Driver started");
        }
        else
        {
            ESP_LOGI(TAG, "Failed to reconfigure alerts"); // Log alert reconfiguration failure
        }        
    } else {
        ESP_LOGI(TAG, "Faild twai_start()");
    }
}
//send from isr example
//BaseType_t xHigherPriorityTaskWoken = pdFALSE;
//xQueueSendFromISR(myQueue, &valueToSend, &xHigherPriorityTaskWoken);
//portYIELD_FROM_ISR(xHigherPriorityTaskWoken);


//static const twai_message_t ping_message = {
//    // Message type and format settings
//    .extd = 0,              // Standard Format message (11-bit ID)
//    .rtr = 0,               // Send a data frame
//    .ss = 1,                // Is single shot (won't retry on error or NACK)
//    .self = 0,              // Not a self reception request
//    .dlc_non_comp = 0,      // DLC is less than 8
//    // Message ID and payload
//    .identifier = ID_MASTER_PING,
//    .data_length_code = 0,
//    .data = {0},
//};