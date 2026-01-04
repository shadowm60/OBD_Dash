idf_component_register(SRCS "can.c" "i2c_io.c" "lvgl_port.c" "waveshare_rgb_lcd_port.c" "sd_card.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_lcd driver esp_lcd_touch_gt911 lvgl hmi fatfs isotp)