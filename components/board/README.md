# board

Hardware drivers for the Waveshare ESP32-S3-Touch-LCD-5.

## What it does

Low-level board bring-up — everything that touches the specific peripherals of
this panel. The rest of the firmware depends on this but not on any chip
details.

| File | Peripheral |
|---|---|
| `waveshare_rgb_lcd_port.c` | 800×480 RGB LCD panel + GT911 touch, LVGL display/touch hookup |
| `lvgl_port.c` | LVGL tick, task, flush callbacks; UI init hooks |
| `i2c_io.c` | I2C master + CH422G IO expander (SD CS, LCD reset, backlight, DI/DO) |
| `sd_card.c` | SD card on SPI, FAT mount at `/sdcard` (CS via the CH422G) |
| `rtc_pcf85063.c` | PCF85063A real-time clock (I2C 0x51, backup cell) |

## Dependencies

`esp_lcd`, `esp_driver_i2c`, `esp_driver_gpio`, `esp_driver_sdspi`,
`esp_lcd_touch_gt911`, `lvgl`, `fatfs`.

## Integration (boot order)

From `main.c`:

```c
i2c_io_init();                         /* CH422G first: everything else hangs off it */
if (rtc_pcf85063_init() == ESP_OK)
    rtc_pcf85063_to_system();          /* set system clock from the RTC */

lvgl_port_set_ui_hooks(hmi_init, hmi_tick);
waveshare_esp32_s3_rgb_lcd_init();     /* starts the LVGL task → hmi_init() */

waveshare_sd_card_init();              /* mount /sdcard */
```

## Key APIs

SD card (no reliable card-detect — (re)mount on demand):

```c
waveshare_sd_card_init();              /* SPI bus once + mount */
waveshare_sd_card_remount();           /* after a card swap; no open files allowed */
bool m = waveshare_sd_card_mounted();
```

RTC (holds UTC; bridges the gap before Wi-Fi/SNTP):

```c
rtc_pcf85063_to_system();              /* RTC → system clock, true if valid */
rtc_pcf85063_from_system();            /* after SNTP: store fresh time */
```

IO expander:

```c
ch422g_write_pin(pin, level);          /* SD CS, LCD reset, backlight, DO0/DO1 */
```

## Gotchas

- **The SD card's CS is a CH422G pin held low.** Reading the CH422G inputs
  (`io_svc` DI0/DI1) momentarily releases all its pins including CS, so the
  reader holds the SPI bus between SD commands. This coupling is why
  `io_svc` only fast-polls the inputs while the IO page is open — see
  [sd_card.h](sd_card.h) and [../io_svc](../io_svc).
- `waveshare_sd_card_remount()` requires that nobody has files open on the card.
- The panel is 800×480 (the 1024×600 variant is behind
  `ESP_PANEL_USE_1024_600_LCD` in [lvgl_port.h](lvgl_port.h), off here).
- Most of this is adapted from Waveshare's sample code; keep changes localized
  so the board support can be compared against upstream.
