/*
 * Status line (every menu page), Home page, Display page (from Esp32_monitor).
 *   sys_status                          one line: OBD, REC, Wi-Fi, BLE, SD, clock
 *   home_info / home_live / home_services
 *   disp_brightness (two-way), disp_brightness_text, disp_timeout_sel (two-way),
 *   disp_gauge_anim (two-way), sd_text, sys_version
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include "lvgl.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bridge.h"
#include "settings.h"
#include "wifi_svc.h"
#include "ble_svc.h"
#include "comsm.h"
#include "can_if.h"
#include "datalog.h"
#include "canmon.h"
#include "sd_card.h"

#define EPOCH_VALID 1700000000

static uivar_t v_status = UIVAR("sys_status");
static uivar_t v_info = UIVAR("home_info");
static uivar_t v_live = UIVAR("home_live");
static uivar_t v_svcs = UIVAR("home_services");
static uivar_t v_bright = UIVAR("disp_brightness");
static uivar_t v_bright_text = UIVAR("disp_brightness_text");
static uivar_t v_timeout = UIVAR("disp_timeout_sel");
static uivar_t v_anim = UIVAR("disp_gauge_anim");
static uivar_t v_sd = UIVAR("sd_text");
static uivar_t v_version = UIVAR("sys_version");

static uint8_t s_bright, s_timeout;
static bool    s_anim;
static char    s_sd_msg[64];

static bool logging(void)
{
    datalog_status_t ls;
    datalog_get_status(&ls);
    return ls.raw_active || ls.params_active;
}

/* ---------------------------------------------------------------- SD info
 * f_getfree() can scan the whole FAT on a big card: never in the LVGL task. */
static volatile bool     s_sd_busy;
static volatile uint64_t s_sd_total, s_sd_free;
static volatile bool     s_sd_info_ok;
static volatile bool     s_sd_remount;

static void sd_info_task(void *arg)
{
    if (s_sd_remount) {
        s_sd_remount = false;
        waveshare_sd_card_remount();
    }
    uint64_t total = 0, free_b = 0;
    bool ok = waveshare_sd_card_mounted() && esp_vfs_fat_info(MOUNT_POINT, &total, &free_b) == ESP_OK;
    s_sd_total = total;
    s_sd_free = free_b;
    s_sd_info_ok = ok;
    s_sd_busy = false;
    vTaskDelete(NULL);
}

static void sd_info_refresh(bool remount)
{
    if (s_sd_busy) {
        return;
    }
    s_sd_busy = true;
    s_sd_remount = remount;
    if (xTaskCreatePinnedToCore(sd_info_task, "sd_info", 4096, NULL, 2, NULL, 0) != pdPASS) {
        s_sd_busy = false;
    }
}

/* ---------------------------------------------------------------- status line */

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static const char *obd_short(const comsm_status_t *c)
{
    switch (c->state) {
    case COMSM_RUNNING:    return "OBD on";
    case COMSM_CONNECTING:
    case COMSM_DISCOVERY:  return "OBD ...";
    case COMSM_RETRY_WAIT: return "OBD retry";
    default:               return "OBD off";
    }
}

void view_status_update(void)
{
    char clock[12] = "";
    time_t t = time(NULL);
    if (t > EPOCH_VALID) {
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(clock, sizeof(clock), "%H:%M", &tm);
    }
    wifi_svc_status_t w;
    wifi_svc_get_status(&w);
    ble_svc_status_t b;
    ble_svc_get_status(&b);
    comsm_status_t c;
    comsm_get_status(&c);
    canmon_stats_t m;
    canmon_get_stats(&m);
    char mon[24] = "";
    if (m.running) {
        snprintf(mon, sizeof(mon), "    CAN %" PRIu32 "/s", m.fps);
    }
    const char *wifi = w.state == WIFI_SVC_CONNECTED ? LV_SYMBOL_WIFI " on"
                     : w.state == WIFI_SVC_CONNECTING ? LV_SYMBOL_WIFI " ..." : LV_SYMBOL_WIFI " off";
    uv_strf(&v_status, "%s%s%s    %s    %s%s    %s    %s",
            obd_short(&c), logging() ? "  " LV_SYMBOL_SAVE " REC" : "", mon, wifi,
            LV_SYMBOL_BLUETOOTH, b.state == BLE_SVC_CONNECTED ? " on" : "",
            waveshare_sd_card_mounted() ? LV_SYMBOL_SD_CARD : "no SD", clock);
}

/* ---------------------------------------------------------------- home */

void view_home_init(void)
{
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    const esp_app_desc_t *app = esp_app_get_description();
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uv_strf(&v_info,
            "Board:  Waveshare ESP32-S3-Touch-LCD-5\n"
            "Chip:  ESP32-S3 rev %d.%d, %d cores, 240 MHz\n"
            "Flash:  %" PRIu32 " MB    PSRAM:  %u MB\n"
            "Firmware:  OBD_Dash %s\n"
            "Built:  %s %s\n"
            "ESP-IDF:  %s    LVGL:  %d.%d.%d\n"
            "MAC:  %02X:%02X:%02X:%02X:%02X:%02X",
            ci.revision / 100, ci.revision % 100, ci.cores,
            flash / (1024 * 1024), (unsigned)(esp_psram_get_size() / (1024 * 1024)),
            app->version, app->date, app->time, app->idf_ver,
            LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    uv_strf(&v_version, "OBD_Dash %s, built %s %s\nESP-IDF %s, LVGL %d.%d.%d, EEZ Studio UI",
            app->version, app->date, app->time, app->idf_ver,
            LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
}

static const char *bus_state(can_if_bus_state_t s)
{
    switch (s) {
    case CAN_IF_BUS_ACTIVE:  return "active";
    case CAN_IF_BUS_WARNING: return "warning";
    case CAN_IF_BUS_PASSIVE: return "error passive";
    case CAN_IF_BUS_OFF:     return "bus off";
    default:                 return "stopped";
    }
}

void view_home_update(bool visible)
{
    if (!visible) {
        return;
    }
    char up[24], clock[48] = "not set (Wi-Fi sets it, the RTC keeps it)";
    time_t t = time(NULL);
    if (t > EPOCH_VALID) {
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(clock, sizeof(clock), "%Y-%m-%d %H:%M:%S", &tm);
    }
    uv_strf(&v_live,
            "Uptime:  %s\n"
            "Time:  %s\n"
            "Internal RAM free:  %u kB (min %u kB)\n"
            "Largest internal block:  %u kB\n"
            "PSRAM free:  %u kB\n"
            "Reset reason:  %d",
            fmt_duration(uptime_s(), up, sizeof(up)), clock,
            (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
            (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
            (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
            (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
            (int)esp_reset_reason());

    comsm_status_t c;
    comsm_get_status(&c);
    can_if_stats_t cs;
    can_if_get_stats(&cs);
    wifi_svc_status_t w;
    wifi_svc_get_status(&w);
    ble_svc_status_t b;
    ble_svc_get_status(&b);
    datalog_status_t ls;
    datalog_get_status(&ls);

    char obd[64], can[96], wifi[80], ble[80], log[80];
    if (c.state == COMSM_RUNNING) {
        snprintf(obd, sizeof(obd), "ECU %03" PRIX32 ", %.0f values/s", c.ecu_id, c.request_rate);
    } else {
        snprintf(obd, sizeof(obd), "%s", comsm_state_name(c.state));
    }
    snprintf(can, sizeof(can), "%s, rx %" PRIu32 " tx %" PRIu32 " (failed %" PRIu32 ", retries %" PRIu32 ")",
             bus_state(cs.state), cs.rx_frames, cs.tx_frames, cs.tx_failed, cs.tx_retries);
    if (w.state == WIFI_SVC_CONNECTED) {
        snprintf(wifi, sizeof(wifi), "connected to %s, %s, %d dBm", w.ssid, w.ip, w.rssi);
    } else {
        snprintf(wifi, sizeof(wifi), "%s", w.state == WIFI_SVC_CONNECTING ? "connecting..." : "not connected");
    }
    if (b.state == BLE_SVC_CONNECTED) {
        snprintf(ble, sizeof(ble), "connected to %s %s", b.peer_name, b.peer_addr);
    } else {
        snprintf(ble, sizeof(ble), "%s, %d bonds", b.state == BLE_SVC_OFF ? "starting" : "idle", b.nbonds);
    }
    if (ls.raw_active || ls.params_active) {
        snprintf(log, sizeof(log), "%s%s%s", ls.raw_active ? ls.raw_file : "",
                 ls.raw_active && ls.params_active ? ", " : "", ls.params_active ? ls.params_file : "");
    } else {
        snprintf(log, sizeof(log), "off");
    }
    uv_strf(&v_svcs, "OBD:  %s\nCAN:  %s\nWi-Fi:  %s\nBLE:  %s\nSD card:  %s    Logging:  %s",
            obd, can, wifi, ble, waveshare_sd_card_mounted() ? "mounted" : "not mounted", log);
}

/* ---------------------------------------------------------------- display */

void view_display_init(void)
{
    app_settings_t cfg;
    settings_get(&cfg);
    s_bright = cfg.brightness;
    s_timeout = cfg.screen_timeout_idx;
    s_anim = cfg.gauge_anim;
    uv_int(&v_bright, s_bright);
    uv_int(&v_timeout, s_timeout);
    uv_bool(&v_anim, s_anim);
    uv_strf(&v_bright_text, "Brightness %u %%", s_bright);
    sd_info_refresh(false);
}

void view_display_update(bool visible)
{
    int b = uv_get_int(&v_bright, s_bright);
    int t = uv_get_int(&v_timeout, s_timeout);
    bool a = uv_get_bool(&v_anim, s_anim);
    if (b != s_bright || t != s_timeout || a != s_anim) {
        if (b < 10) b = 10;
        if (b > 100) b = 100;
        s_bright = b;
        s_timeout = t;
        s_anim = a;
        brightness_set(s_bright);
        screen_set_timeout(s_timeout);
        uv_strf(&v_bright_text, "Brightness %u %%", s_bright);
        app_settings_t cfg;
        settings_get(&cfg);
        cfg.brightness = s_bright;
        cfg.screen_timeout_idx = s_timeout;
        cfg.gauge_anim = s_anim;
        settings_put(&cfg);
    }
    if (!visible) {
        return;
    }
    if (s_sd_busy) {
        uv_str(&v_sd, "SD card: checking...");
    } else if (s_sd_info_ok) {
        uv_strf(&v_sd, "SD card: mounted at " MOUNT_POINT "\n%.1f GB free of %.1f GB\n%s",
                s_sd_free / 1e9, s_sd_total / 1e9, s_sd_msg);
    } else {
        uv_strf(&v_sd, "SD card: not mounted\nInsert a FAT32 card and press Remount.\n%s", s_sd_msg);
    }
}

void view_display_sd_remount(void)
{
    if (logging()) {
        snprintf(s_sd_msg, sizeof(s_sd_msg), "Switch logging off first (OBD / CAN page).");
        return;
    }
    s_sd_msg[0] = '\0';
    sd_info_refresh(true);
}

void view_display_restart(void)
{
    datalog_enable_raw(false);          /* the logger closes its files */
    datalog_enable_params(false);
    settings_flush();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}
