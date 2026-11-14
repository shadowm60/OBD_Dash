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
 *   wifi_svc     Wi-Fi station: scan / connect, SNTP clock (-> RTC)
 *   ble_svc      Bluetooth LE central (NimBLE): scan, connect, pair
 *   canmon       CAN bus monitor for the CAN page (ID table, rates, bus load)
 *
 * Init order matters for internal RAM (see Esp32_monitor): the display takes
 * its large contiguous blocks before Wi-Fi and the BT controller fragment /
 * use up internal RAM. Free memory is logged after every step.
 */
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "i2c_io.h"
#include "rtc_pcf85063.h"
#include "wifi_svc.h"
#include "ble_svc.h"
#include "waveshare_rgb_lcd_port.h"
#include "sd_card.h"
#include "hmi.h"
#include "can_if.h"
#include "comsm.h"
#include "vehicle_data.h"
#include "settings.h"
#include "obd_pids.h"
#include "datalog.h"
#include "canmon.h"

static const char *TAG = "app_main";

static void log_heap(const char *step)
{
    ESP_LOGI(TAG, "after %-8s internal free %3u KB (largest block %3u KB), PSRAM free %u KB", step,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

/* SNTP set the clock: keep it in the RTC for the next power-up without Wi-Fi. */
static void on_time_synced(void)
{
    rtc_pcf85063_from_system();
}

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

    setenv("TZ", CONFIG_APP_TZ, 1);
    tzset();
    i2c_io_init();
    if (rtc_pcf85063_init() == ESP_OK) {
        rtc_pcf85063_to_system();           /* real timestamps without Wi-Fi */
    }
    log_heap("board");

    lvgl_port_set_ui_hooks(hmi_init, hmi_tick);
    waveshare_esp32_s3_rgb_lcd_init();      /* starts the LVGL task -> hmi_init() */
    wavesahre_rgb_lcd_bl_on();
    log_heap("display");

    if (waveshare_sd_card_init() != ESP_OK) {
        ESP_LOGW(TAG, "no SD card, logging will fail until one is inserted and the board restarted");
    }

    static const uint32_t BITRATES[] = { 125000, 250000, 500000, 1000000 };    /* = CAN page list */
    app_settings_t boot_cfg;
    settings_get(&boot_cfg);
    can_if_config_t can_cfg = CAN_IF_CONFIG_OBD_DEFAULT();
    can_cfg.bitrate = BITRATES[boot_cfg.can_bitrate_idx < 4 ? boot_cfg.can_bitrate_idx : 2];
    ESP_ERROR_CHECK(can_if_init(&can_cfg));
    ESP_ERROR_CHECK(canmon_init());
    ESP_ERROR_CHECK(comsm_init());
    ESP_ERROR_CHECK(datalog_init(MOUNT_POINT));
    log_heap("obd");

    app_settings_t cfg;
    settings_get(&cfg);
    wifi_svc_set_sync_cb(on_time_synced);
    if (wifi_svc_init(cfg.wifi_autoconnect) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed");
    }
    log_heap("wifi");
    if (ble_svc_init() != ESP_OK) {
        ESP_LOGE(TAG, "BLE init failed");
    }
    log_heap("ble");

    ESP_LOGI(TAG, "init done, press Connect to start");
}
