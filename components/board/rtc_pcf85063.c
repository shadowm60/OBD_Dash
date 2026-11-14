#include "rtc_pcf85063.h"

#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_check.h"
#include "i2c_io.h"

static const char *TAG = "rtc";

#define RTC_ADDR        0x51
#define REG_CONTROL_1   0x00        /* bit 1 12_24: 0 = 24 h mode */
#define REG_SECONDS     0x04        /* bit 7 OS: oscillator stopped, time invalid */
#define MIN_VALID_YEAR  2024

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

esp_err_t rtc_pcf85063_init(void)
{
    i2c_master_bus_handle_t bus = board_i2c_bus();
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (i2c_master_probe(bus, RTC_ADDR, 50) != ESP_OK) {
        ESP_LOGW(TAG, "no PCF85063 at 0x%02X", RTC_ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RTC_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    return i2c_master_bus_add_device(bus, &cfg, &s_dev);
}

esp_err_t rtc_pcf85063_read(time_t *utc)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t reg = REG_SECONDS, r[7];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, r, sizeof(r), 100), TAG, "read");
    if (r[0] & 0x80) {
        return ESP_ERR_INVALID_STATE;       /* oscillator stopped */
    }
    struct tm tm = {
        .tm_sec  = bcd2bin(r[0] & 0x7F),
        .tm_min  = bcd2bin(r[1] & 0x7F),
        .tm_hour = bcd2bin(r[2] & 0x3F),
        .tm_mday = bcd2bin(r[3] & 0x3F),
        /* r[4] weekday: not needed */
        .tm_mon  = bcd2bin(r[5] & 0x1F) - 1,
        .tm_year = bcd2bin(r[6]) + 100,     /* 2000 + yy */
    };
    if (tm.tm_year + 1900 < MIN_VALID_YEAR) {
        return ESP_ERR_INVALID_STATE;
    }
    /* timegm without a TZ dependency: mktime with TZ=UTC0 temporarily is not
     * thread safe, so compute it directly (days from civil, Howard Hinnant) */
    int y = tm.tm_year + 1900, m = tm.tm_mon + 1, d = tm.tm_mday;
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468;
    *utc = (time_t)days * 86400 + tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    return ESP_OK;
}

esp_err_t rtc_pcf85063_write(time_t utc)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    struct tm tm;
    gmtime_r(&utc, &tm);
    if (tm.tm_year + 1900 < MIN_VALID_YEAR || tm.tm_year + 1900 > 2099) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t w[8] = {
        REG_SECONDS,
        bin2bcd(tm.tm_sec),                 /* writing seconds clears OS */
        bin2bcd(tm.tm_min),
        bin2bcd(tm.tm_hour),
        bin2bcd(tm.tm_mday),
        (uint8_t)tm.tm_wday,
        bin2bcd(tm.tm_mon + 1),
        bin2bcd(tm.tm_year - 100),
    };
    return i2c_master_transmit(s_dev, w, sizeof(w), 100);
}

bool rtc_pcf85063_to_system(void)
{
    time_t t;
    esp_err_t err = rtc_pcf85063_read(&t);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no valid time in the RTC (%s)", esp_err_to_name(err));
        return false;
    }
    struct timeval tv = { .tv_sec = t };
    settimeofday(&tv, NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    ESP_LOGI(TAG, "system clock set from the RTC: %s UTC", buf);
    return true;
}

esp_err_t rtc_pcf85063_from_system(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    esp_err_t err = rtc_pcf85063_write(tv.tv_sec);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RTC updated from the system clock");
    }
    return err;
}
