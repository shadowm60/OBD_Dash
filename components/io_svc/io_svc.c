#include "io_svc.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/temperature_sensor.h"
#include "i2c_io.h"

static const char *TAG = "io_svc";

#define FAST_MS     100
#define SLOW_MS     500
#define TEMP_MS     2000
#define DI0_BIT     0x01        /* EXIO0 */
#define DI1_BIT     0x20        /* EXIO5 */

static SemaphoreHandle_t s_lock;
static io_svc_status_t   s_st;
static volatile bool     s_fast;
static volatile bool     s_scan_req;
static temperature_sensor_handle_t s_temp;

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

/* On-board devices: CH422G answers on 0x20-0x27 / 0x30-0x3F (a probe there is
 * a command to it), GT911 touch on 0x5D (or 0x14), PCF85063 RTC on 0x51. */
static bool on_board(uint8_t a)
{
    return (a >= 0x20 && a <= 0x27) || (a >= 0x30 && a <= 0x3F) || a == 0x5D || a == 0x14 || a == 0x51;
}

static void i2c_scan(void)
{
    char buf[sizeof(s_st.i2c_result)];
    int len = 0, found = 0;
    i2c_master_bus_handle_t bus = board_i2c_bus();
    if (bus == NULL) {
        snprintf(buf, sizeof(buf), "I2C bus not initialised");
    } else {
        for (uint8_t a = 0x08; a <= 0x77; a++) {
            if (on_board(a)) {
                continue;
            }
            if (i2c_master_probe(bus, a, 20) == ESP_OK) {
                found++;
                if (len < (int)sizeof(buf) - 8) {
                    len += snprintf(buf + len, sizeof(buf) - len, "%s0x%02X", found > 1 ? " " : "", a);
                }
            }
        }
        if (found == 0) {
            snprintf(buf, sizeof(buf), "no external devices (on-board CH422G, GT911 and RTC skipped)");
        }
    }
    LOCK();
    snprintf(s_st.i2c_result, sizeof(s_st.i2c_result), "%s", buf);
    s_st.i2c_scanning = false;
    s_st.i2c_scan_count++;
    UNLOCK();
    ESP_LOGI(TAG, "I2C scan: %s", buf);
}

static void read_di(void)
{
    uint8_t lv;
    bool ok = ch422g_read_io(&lv) == ESP_OK;
    bool now[2] = { !(lv & DI0_BIT), !(lv & DI1_BIT) };     /* low = optocoupler conducts */
    LOCK();
    if (ok) {
        for (int i = 0; i < 2; i++) {
            if (s_st.di_ok && now[i] != s_st.di[i]) {
                s_st.di_changes[i]++;
                s_st.di_last_change_s[i] = uptime_s();
            }
            s_st.di[i] = now[i];
        }
    }
    s_st.di_ok = ok;
    UNLOCK();
}

static void io_task(void *arg)
{
    uint32_t next_temp = 0;
    while (1) {
        if (s_scan_req) {
            s_scan_req = false;
            i2c_scan();
        }
        if (s_fast) {
            read_di();          /* only while the IO page is open, see io_svc.h */
        }
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (s_temp != NULL && (int32_t)(now - next_temp) >= 0) {
            next_temp = now + TEMP_MS;
            float t = 0;
            bool ok = temperature_sensor_get_celsius(s_temp, &t) == ESP_OK;
            LOCK();
            s_st.temp_ok = ok;
            s_st.chip_temp_c = t;
            UNLOCK();
        }
        vTaskDelay(pdMS_TO_TICKS(s_fast ? FAST_MS : SLOW_MS));
    }
}

void io_svc_set_fast(bool fast)
{
    s_fast = fast;
}

esp_err_t io_svc_set_do(int n, bool on)
{
    if (n < 0 || n > 1 || s_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    LOCK();
    s_st.do_on[n] = on;
    /* OC low = output on; OC2 / OC3 unused, kept high */
    uint8_t bits = 0x0C | (s_st.do_on[0] ? 0 : 0x01) | (s_st.do_on[1] ? 0 : 0x02);
    UNLOCK();
    esp_err_t err = ch422g_write_oc(bits);
    ESP_LOGI(TAG, "DO%d %s%s", n, on ? "on" : "off", err == ESP_OK ? "" : " (write failed)");
    return err;
}

void io_svc_reset(void)
{
    if (s_lock == NULL) {
        return;
    }
    LOCK();
    memset(s_st.di_changes, 0, sizeof(s_st.di_changes));
    memset(s_st.di_last_change_s, 0, sizeof(s_st.di_last_change_s));
    UNLOCK();
}

esp_err_t io_svc_i2c_scan(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    LOCK();
    bool busy = s_st.i2c_scanning;
    s_st.i2c_scanning = true;
    UNLOCK();
    if (!busy) {
        s_scan_req = true;
    }
    return busy ? ESP_ERR_INVALID_STATE : ESP_OK;
}

void io_svc_get_status(io_svc_status_t *out)
{
    if (s_lock == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    LOCK();
    *out = s_st;
    UNLOCK();
}

esp_err_t io_svc_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    snprintf(s_st.i2c_result, sizeof(s_st.i2c_result), "not scanned yet");
    const temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tcfg, &s_temp) != ESP_OK || temperature_sensor_enable(s_temp) != ESP_OK) {
        ESP_LOGW(TAG, "chip temperature sensor not available");
        s_temp = NULL;
    }
    /* Stack in PSRAM (internal RAM is needed by Wi-Fi / BLE); the task never
     * writes flash. It does take the SD card's SPI bus for a moment (DI read). */
    if (xTaskCreatePinnedToCoreWithCaps(io_task, "io_svc", 3072, NULL, 3, NULL, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
