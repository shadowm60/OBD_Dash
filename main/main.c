/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"
#include "ui.h"
#include "dpool.h"
#include "sd_card.h"

void app_main()
{
    waveshare_esp32_s3_rgb_lcd_init(); // Initialize the Waveshare ESP32-S3 RGB LCD 
    // wavesahre_rgb_lcd_bl_on();  //Turn on the screen backlight 
    // wavesahre_rgb_lcd_bl_off(); //Turn off the screen backlight 
   
    ESP_LOGI(TAG, "Display init done");

    waveshare_sd_card_init();
    
    //loadScreen(SCREEN_ID_STARTUP);
    ESP_LOGI(TAG, "Screen Loaded");

    init_dpool();

    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(-1)) {


        // Release the mutex
        lvgl_port_unlock();
    }
}
