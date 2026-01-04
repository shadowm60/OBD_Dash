#include "i2c_io.h"
#include "waveshare_rgb_lcd_port.h"

static const char *TAG = "CH422G";

static ch422g_config_t g_cfg;
static uint8_t g_output_state = 0;   // cached output register

#define CUSTOM

static esp_err_t ch422g_write_reg(uint8_t value)
{
#ifdef CUSTOM
    uint8_t write_buf = 0x01;
    i2c_master_write_to_device(I2C_MASTER_NUM, 0x24, &write_buf, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

    return i2c_master_write_to_device(I2C_MASTER_NUM, 0x38, &value, 1, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
#else    
    uint8_t data[2] = { 0x40, value }; // 0x40 = output register command
    return i2c_master_write_to_device(
        g_cfg.i2c_port,
        g_cfg.i2c_addr,
        data,
        sizeof(data),
        100 / portTICK_PERIOD_MS
    );
#endif    
}

static esp_err_t ch422g_read_reg(uint8_t *value)
{
    uint8_t cmd = 0x41; // input register command
    esp_err_t ret = i2c_master_write_read_device(
        g_cfg.i2c_port,
        g_cfg.i2c_addr,
        &cmd,
        1,
        value,
        1,
        100 / portTICK_PERIOD_MS
    );
    return ret;
}

esp_err_t ch422g_init(const ch422g_config_t *cfg)
{
    g_cfg = *cfg;

    // Build initial output register based on pin_dir
    g_output_state = 0;
    for (int i = 0; i < CH422G_MAX_PINS; i++) {
        if (g_cfg.pin_dir[i] == CH422G_OUT) {
            // default output = low
            g_output_state &= ~(1 << i);
        }
    }

    ESP_LOGI(TAG, "Initializing CH422G at addr 0x%02X", g_cfg.i2c_addr);

    return ch422g_write_reg(g_output_state);
}

esp_err_t ch422g_write_pin(uint8_t pin, uint8_t level)
{
    if (pin >= CH422G_MAX_PINS) return ESP_ERR_INVALID_ARG;
    if (g_cfg.pin_dir[pin] != CH422G_OUT) return ESP_ERR_INVALID_STATE;

    if (level)
        g_output_state |= (1 << pin);
    else
        g_output_state &= ~(1 << pin);

    return ch422g_write_reg(g_output_state);
}

esp_err_t ch422g_read_pin(uint8_t pin, uint8_t *level)
{
    if (pin >= CH422G_MAX_PINS) return ESP_ERR_INVALID_ARG;
    if (g_cfg.pin_dir[pin] != CH422G_IN) return ESP_ERR_INVALID_STATE;

    uint8_t val;
    esp_err_t ret = ch422g_read_reg(&val);
    if (ret != ESP_OK) return ret;

    *level = (val >> pin) & 0x01;
    return ESP_OK;
}



/**
 * @brief I2C master initialization
 */
static esp_err_t i2c_master_init(void)
{
    int i2c_master_port = I2C_MASTER_NUM;

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    // Configure I2C parameters
    i2c_param_config(i2c_master_port, &i2c_conf);

    // Install I2C driver
    return i2c_driver_install(i2c_master_port, i2c_conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

void i2c_io_init(void) {
    i2c_master_init();

    ch422g_config_t cfg = {
        .i2c_port = I2C_MASTER_NUM,
        .i2c_addr = 0x20,
        .pin_dir = {
            CH422G_OUT, CH422G_OUT, CH422G_OUT, CH422G_OUT,
            CH422G_OUT, CH422G_OUT, CH422G_OUT, CH422G_OUT
        }
    };

    ch422g_init(&cfg);

    //prepare lcd pins
    ch422g_write_pin(0x02, 0x01); // set EXIO2 high - DISP
    ch422g_write_pin(0x03, 0x01); // set EXIO3 high - LCD_RST

}

// Reset the touch screen
void waveshare_esp32_s3_touch_reset(void)
{
    // Reset the touch screen. It is recommended to reset the touch screen before using it.
    ch422g_write_pin(0x01, 0x00); // set EXIO1 low
    
    esp_rom_delay_us(100 * 1000);
    gpio_set_level(GPIO_INPUT_IO_4, 0);
    esp_rom_delay_us(100 * 1000);
    
    ch422g_write_pin(0x01, 0x01); // set EXIO1 high
    esp_rom_delay_us(200 * 1000);
}

/******************************* Turn on the screen backlight **************************************/
esp_err_t wavesahre_rgb_lcd_bl_on()
{
    // Pull the backlight pin high to light the screen backlight
    ch422g_write_pin(0x02, 0x01); // set EXIO2 high - DISP
    return ESP_OK;
}

/******************************* Turn off the screen backlight **************************************/
esp_err_t wavesahre_rgb_lcd_bl_off()
{

    // Turn off the screen backlight by pulling the backlight pin low
    ch422g_write_pin(0x02, 0x00); // set EXIO2 low - DISP
    return ESP_OK;
}

void sdcard_cs_low() {

    // Control CH422G to pull down the CS pin of the SD
    ch422g_write_pin(0x04, 0x00); // set EXIO4 low - SDCS
}

void sdcard_cs_high() {

    // Control CH422G to pull down the CS pin of the SD
    ch422g_write_pin(0x04, 0x01); // set EXIO4 low - SDCS
}