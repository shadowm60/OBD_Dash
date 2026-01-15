/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"
#include "ui.h"
#include "dpool.h"
#include "sd_card.h"
#include "can.h"
#include "comSM.h"

static const char *TAG = "app_main";

void app_main()
{
    i2c_io_init();

    waveshare_esp32_s3_rgb_lcd_init(); // Initialize the Waveshare ESP32-S3 RGB LCD 
    // wavesahre_rgb_lcd_bl_on();  //Turn on the screen backlight 
    // wavesahre_rgb_lcd_bl_off(); //Turn off the screen backlight 
   
    ESP_LOGI(TAG, "Display init done");

    waveshare_sd_card_init();

    wavesahre_rgb_lcd_bl_on();
    
    //loadScreen(SCREEN_ID_STARTUP);
    ESP_LOGI(TAG, "Screen Loaded");

    init_dpool();

    init_can();
    init_can_tasks();

    init_comSM();
    init_comSM_task();

    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(-1)) {

        // Release the mutex
        lvgl_port_unlock();
    }
}
