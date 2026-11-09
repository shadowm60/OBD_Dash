/*
 * OBD_Dash - application entry point. Only wiring and init order live here.
 *
 *   board        LCD, touch, IO expander, SD card, LVGL port
 *   hmi          EEZ UI + bridge (runs inside the LVGL task)
 *   can_if       TWAI driver, TX queue, RX dispatch
 *   comsm        connection state machine, owns the OBD client
 *   vehicle_data latest value of every signal (OBD -> UI / logger)
 *   settings     persistent user configuration (NVS)
 *   datalog      SD card logging (raw CAN, parameters); switched by the Settings screen
 */
#include "esp_log.h"
#include "waveshare_rgb_lcd_port.h"
#include "sd_card.h"
#include "hmi.h"
#include "can_if.h"
#include "comsm.h"
#include "vehicle_data.h"
#include "settings.h"
#include "obd_pids.h"
#include "datalog.h"

static const char *TAG = "app_main";

/* Restore the user's PID selection before the UI or comsm can use it. */
static void apply_pid_selection(void)
{
    for (size_t i = 0; i < obd_pid_count(); i++) {
        obd_pid_set_enabled(i, settings_pid_selected(obd_pid_desc(i)->pid));
    }
}

void app_main(void)
{
    settings_init();            /* NVS: must come before anything reading settings */
    vd_init();
    apply_pid_selection();

    i2c_io_init();
    lvgl_port_set_ui_hooks(hmi_init, hmi_tick);
    waveshare_esp32_s3_rgb_lcd_init();      /* starts the LVGL task -> hmi_init() */
    wavesahre_rgb_lcd_bl_on();
    ESP_LOGI(TAG, "display up");

    if (waveshare_sd_card_init() != ESP_OK) {
        ESP_LOGW(TAG, "no SD card, logging will fail until one is inserted and the board restarted");
    }

    const can_if_config_t can_cfg = CAN_IF_CONFIG_OBD_DEFAULT();
    ESP_ERROR_CHECK(can_if_init(&can_cfg));
    ESP_ERROR_CHECK(comsm_init());
    ESP_ERROR_CHECK(datalog_init(MOUNT_POINT));

    ESP_LOGI(TAG, "init done, press Connect to start");
}
