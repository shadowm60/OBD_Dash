#include "i2c_io.h"
#include "waveshare_rgb_lcd_port.h"
#include "sd_card.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "CH422G";

/*
 * CH422G IO expander. It has no register map: each function is a separate
 * I2C address (datasheet):
 *   0x24  system parameter (0x01 = IO0..7 as outputs)
 *   0x38  write IO0..7 output levels
 *   0x26  read IO0..7 input levels
 * The bus is created with the new i2c_master driver; the GT911 touch
 * controller shares it (see waveshare_rgb_lcd_port.c).
 */
#define CH422G_ADDR_SYSTEM  0x24
#define CH422G_ADDR_OUTPUT  0x38
#define CH422G_ADDR_INPUT   0x26
#define CH422G_ADDR_OC      0x23        /* write OC0..3 (DO0 / DO1 on this board) */
#define CH422G_SYS_IO_OE    0x01        /* IO0..7 all outputs; 0 = all inputs (one bit for all 8) */

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev_system;
static i2c_master_dev_handle_t s_dev_output;
static i2c_master_dev_handle_t s_dev_input;
static i2c_master_dev_handle_t s_dev_oc;

static ch422g_config_t g_cfg;
static uint8_t g_output_state = 0;   // cached output register

/* The output register is read-modify-write: the LVGL task (backlight), the SD
 * card (CS) and later the IO page change single pins from different tasks. */
static SemaphoreHandle_t s_lock;

static esp_err_t add_device(uint8_t addr, i2c_master_dev_handle_t *out)
{
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &dev_cfg, out);
}

static esp_err_t ch422g_write_reg(uint8_t value)
{
    uint8_t sys = CH422G_SYS_IO_OE;
    esp_err_t ret = i2c_master_transmit(s_dev_system, &sys, 1, I2C_MASTER_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    return i2c_master_transmit(s_dev_output, &value, 1, I2C_MASTER_TIMEOUT_MS);
}

static esp_err_t ch422g_read_reg(uint8_t *value)
{
    return i2c_master_receive(s_dev_input, value, 1, I2C_MASTER_TIMEOUT_MS);
}

esp_err_t ch422g_init(const ch422g_config_t *cfg)
{
    g_cfg = *cfg;
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_RETURN_ON_ERROR(add_device(CH422G_ADDR_SYSTEM, &s_dev_system), TAG, "add sys dev");
    ESP_RETURN_ON_ERROR(add_device(CH422G_ADDR_OUTPUT, &s_dev_output), TAG, "add out dev");
    ESP_RETURN_ON_ERROR(add_device(CH422G_ADDR_INPUT, &s_dev_input), TAG, "add in dev");
    ESP_RETURN_ON_ERROR(add_device(CH422G_ADDR_OC, &s_dev_oc), TAG, "add oc dev");
    uint8_t oc = 0x0F;                  /* OC high = DO0 / DO1 off (also the power-on state) */
    i2c_master_transmit(s_dev_oc, &oc, 1, I2C_MASTER_TIMEOUT_MS);

    // All outputs start low
    g_output_state = 0;

    ESP_LOGI(TAG, "Initializing CH422G");

    return ch422g_write_reg(g_output_state);
}

esp_err_t ch422g_write_pin(uint8_t pin, uint8_t level)
{
    if (pin >= CH422G_MAX_PINS) return ESP_ERR_INVALID_ARG;
    if (g_cfg.pin_dir[pin] != CH422G_OUT) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (level)
        g_output_state |= (1 << pin);
    else
        g_output_state &= ~(1 << pin);

    esp_err_t ret = ch422g_write_reg(g_output_state);
    xSemaphoreGive(s_lock);
    return ret;
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

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_bus;
}

/* CH422G datasheet: RD-IO returns the pin levels only while IO_OE = 0 (all IO
 * pins inputs, weak ~5 uA pull-up); with IO_OE = 1 it returns the output
 * register. So: hold the SD card's SPI bus (EXIO4 = SD CS floats up), switch
 * to inputs, read, switch back and restore the outputs. ~0.3 ms; touch reset,
 * LCD reset and backlight enable stay high meanwhile (pull-up + capacitance).
 * EXIO0 / EXIO5 (DI0 / DI1) are driven low while in output mode, which never
 * fights the optocouplers that pull them low. */
esp_err_t ch422g_read_io(uint8_t *levels)
{
    if (s_lock == NULL || s_dev_input == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    waveshare_sd_bus_lock();
    uint8_t in_mode = 0x00;
    esp_err_t err = i2c_master_transmit(s_dev_system, &in_mode, 1, I2C_MASTER_TIMEOUT_MS);
    if (err == ESP_OK) {
        err = i2c_master_receive(s_dev_input, levels, 1, I2C_MASTER_TIMEOUT_MS);
    }
    esp_err_t back = ch422g_write_reg(g_output_state);   /* IO_OE = 1 + output levels */
    waveshare_sd_bus_unlock();
    xSemaphoreGive(s_lock);
    return err != ESP_OK ? err : back;
}

esp_err_t ch422g_write_oc(uint8_t bits)
{
    if (s_lock == NULL || s_dev_oc == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t v = bits & 0x0F;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = i2c_master_transmit(s_dev_oc, &v, 1, I2C_MASTER_TIMEOUT_MS);
    xSemaphoreGive(s_lock);
    return err;
}

/**
 * @brief I2C master bus initialization (new i2c_master driver)
 */
static esp_err_t i2c_master_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_cfg, &s_bus);
}

void i2c_io_init(void) {
    ESP_ERROR_CHECK(i2c_master_init());

    ch422g_config_t cfg = {
        .i2c_port = I2C_MASTER_NUM,
        .i2c_addr = 0x20,
        .pin_dir = {
            CH422G_OUT, CH422G_OUT, CH422G_OUT, CH422G_OUT,
            CH422G_OUT, CH422G_OUT, CH422G_OUT, CH422G_OUT
        }
    };

    ESP_ERROR_CHECK(ch422g_init(&cfg));

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