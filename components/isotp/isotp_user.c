#include <stdio.h>
#include <stdarg.h>
#include "isotp_user.h"
#include "isotp_defines.h"
#include "can_if.h"
#include "esp_log.h"

static const char *TAG = "isotp";

void isotp_user_debug(const char* message, ...) {
    va_list args;
    va_start(args, message);
    esp_log_writev(ESP_LOG_DEBUG, TAG, message, args);
    va_end(args);
}

int isotp_user_send_can(const uint32_t arbitration_id, const uint8_t* data, const uint8_t size) {
    can_frame_t f = {
        .id  = arbitration_id,
        .dlc = size > CAN_IF_MAX_DLC ? CAN_IF_MAX_DLC : size,
        .ext = arbitration_id > 0x7FF,
    };
    for (uint8_t i = 0; i < f.dlc; i++) {
        f.data[i] = data[i];
    }
    return (can_if_send(&f, 5) == ESP_OK) ? ISOTP_RET_OK : ISOTP_RET_ERROR;
}

/* user implemented, get millisecond */
uint32_t isotp_user_get_ms(void) {
    return can_if_now_ms();
}
