# Board support: Waveshare ESP32-S3-Touch-LCD-5 (LCD, touch, IO expander, SD card).
# Must not depend on any application component.
idf_component_register(SRCS "i2c_io.c" "lvgl_port.c" "waveshare_rgb_lcd_port.c" "sd_card.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_lcd esp_driver_i2c esp_driver_gpio esp_driver_sdspi esp_lcd_touch_gt911 lvgl fatfs)
