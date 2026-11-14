/*
 * SD card on SPI (MOSI 11, SCK 12, MISO 13), CS = CH422G EXIO4 held low.
 * The board has no usable card detect, so (re)mounting is on demand.
 */
#ifndef _SD_CARD_
#define _SD_CARD_

#include <stdbool.h>
#include "esp_err.h"

#define MOUNT_POINT "/sdcard"

#define PIN_NUM_MISO CONFIG_SDCARD_PIN_MISO
#define PIN_NUM_MOSI CONFIG_SDCARD_PIN_MOSI
#define PIN_NUM_CLK  CONFIG_SDCARD_PIN_CLK
#define PIN_NUM_CS   CONFIG_SDCARD_PIN_CS     /* -1: CS is on the IO expander */

/* Initialises the SPI bus (once) and mounts the FAT file system at MOUNT_POINT. */
esp_err_t waveshare_sd_card_init(void);

/* Unmounts (if mounted) and mounts again, e.g. after the card was inserted or
 * swapped. Nobody may have files open on the card while this runs. */
esp_err_t waveshare_sd_card_remount(void);

bool waveshare_sd_card_mounted(void);

#endif
