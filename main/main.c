/*
 * OBD_Dash - application entry point. Only wiring and init order live here.
 *
 *   board        LCD, touch, IO expander, SD card, LVGL port
 *   hmi          EEZ UI + bridge (runs inside the LVGL task)
 *   can_if       TWAI driver, TX queue, RX dispatch
 *   comsm        connection state machine, owns the OBD client
 *   vehicle_data latest value of every signal (OBD -> UI / logger)
 */
#include "esp_log.h"
#include "waveshare_rgb_lcd_port.h"
#include "sd_card.h"
#include "hmi.h"
#include "can_if.h"
#include "comsm.h"
#include "vehicle_data.h"

static const char *TAG = "app_main";

void app_main(void)
{
    vd_init();

    i2c_io_init();
    lvgl_port_set_ui_hooks(hmi_init, hmi_tick);
    waveshare_esp32_s3_rgb_lcd_init();      /* starts the LVGL task -> hmi_init() */
    wavesahre_rgb_lcd_bl_on();
    ESP_LOGI(TAG, "display up");

    if (waveshare_sd_card_init() != ESP_OK) {
        ESP_LOGW(TAG, "no SD card, logging disabled");
    }

    const can_if_config_t can_cfg = CAN_IF_CONFIG_OBD_DEFAULT();
    ESP_ERROR_CHECK(can_if_init(&can_cfg));
    ESP_ERROR_CHECK(comsm_init());

    ESP_LOGI(TAG, "init done, press Connect to start");
}
