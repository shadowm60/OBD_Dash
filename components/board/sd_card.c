#include "sd_card.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "i2c_io.h"

static const char *TAG = "sdcard";

static sdmmc_card_t *s_card;
static sdmmc_host_t  s_host = SDSPI_HOST_DEFAULT();
static bool          s_bus_ok;

static esp_err_t bus_init(void)
{
    if (s_bus_ok) {
        return ESP_OK;
    }
    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t err = spi_bus_initialize(s_host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_bus_ok = true;
    return ESP_OK;
}

esp_err_t waveshare_sd_card_init(void)
{
    if (s_card != NULL) {
        return ESP_OK;
    }
    sdcard_cs_low();
    esp_err_t err = bus_init();
    if (err != ESP_OK) {
        return err;
    }

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_SDCARD_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = s_host.slot;

    err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &s_host, &slot_config, &mount_config, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        if (err == ESP_FAIL) {
            ESP_LOGW(TAG, "card found but no FAT file system (exFAT / NTFS? format it as FAT32)");
        } else {
            ESP_LOGW(TAG, "no card or card not answering: %s", esp_err_to_name(err));
        }
        return err;
    }
    ESP_LOGI(TAG, "mounted at %s", MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t waveshare_sd_card_remount(void)
{
    if (s_card != NULL) {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
        s_card = NULL;
        ESP_LOGI(TAG, "unmounted");
    }
    return waveshare_sd_card_init();
}

bool waveshare_sd_card_mounted(void)
{
    return s_card != NULL;
}
