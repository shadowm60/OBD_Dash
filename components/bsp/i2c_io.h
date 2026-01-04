#ifndef _I2C_H_
#define _I2C_H_

#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

#define I2C_MASTER_SCL_IO           CONFIG_I2C_MASTER_SCL //9       /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           CONFIG_I2C_MASTER_SDA //8       /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              0       /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ          400000                     /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

#define CH422G_MAX_PINS 8

typedef enum {
    CH422G_IN = 0,
    CH422G_OUT = 1
} ch422g_pin_dir_t;

typedef struct {
    i2c_port_t i2c_port;
    uint8_t i2c_addr;
    ch422g_pin_dir_t pin_dir[CH422G_MAX_PINS];
} ch422g_config_t;

esp_err_t ch422g_init(const ch422g_config_t *cfg);
esp_err_t ch422g_write_pin(uint8_t pin, uint8_t level);
esp_err_t ch422g_read_pin(uint8_t pin, uint8_t *level);


void i2c_io_init(void);
void waveshare_esp32_s3_touch_reset(void);
esp_err_t wavesahre_rgb_lcd_bl_on(void);
esp_err_t wavesahre_rgb_lcd_bl_off(void);
void sdcard_cs_low(void);
void sdcard_cs_high(void);

#endif