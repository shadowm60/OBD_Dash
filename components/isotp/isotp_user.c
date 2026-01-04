#include <stdio.h>
#include <stdlib.h>
#include "isotp_user.h"
#include "isotp_defines.h"
#include "can.h"
#include "esp_log.h"
#include <stdarg.h>

static const char *TAG = "ISOTP_DEBUG";

void isotp_user_debug(const char* message, ...) {
    va_list args;
    va_start(args, message);
    ESP_LOGW(TAG, "%s", "");  // Initialize log line (optional)
    esp_log_writev(ESP_LOG_WARN, TAG, message, args);
    va_end(args);
}


int  isotp_user_send_can(const uint32_t arbitration_id, const uint8_t* data, const uint8_t size) {
    uint8_t i;
    twai_message_t tp_message = {
        // Message type and format settings
        .extd = 0,              // Standard Format message (11-bit ID)
        .rtr = 0,               // Send a data frame
        .ss = 1,                // Is single shot (won't retry on error or NACK)
        .self = 0,              // Not a self reception request
        .dlc_non_comp = 0,      // DLC is less than 8
        // Message ID and payload
        .identifier = arbitration_id,
        .data_length_code = size,
        .data = {0},
    };

    for (i=0; i<size; i++) {
        tp_message.data[i] = data[i];
    }

    can_send_message(&tp_message);

    return ISOTP_RET_OK;
}

/* user implemented, get millisecond */
uint32_t isotp_user_get_ms(void) {
    return can_get_timestamp_ms();
}