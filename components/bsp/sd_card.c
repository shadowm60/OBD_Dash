#include "sd_card.h"
#include "i2c_io.h"

static const char *TAG = "sdcard";

sdmmc_card_t *card = NULL;
const char mount_point[] = MOUNT_POINT;
static int index_val = 0;

// By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
// For setting a specific frequency, use host.max_freq_khz (range 400kHz - 20MHz for SDSPI)
// Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
sdmmc_host_t host = SDSPI_HOST_DEFAULT();


static void sdcard_get_info(void) {
    if (card != NULL) {
        // Card has been initialized, print its properties
        sdmmc_card_print_info(stdout, card);

        const char *file_index = MOUNT_POINT "/index.dat";

        //check if exists
        struct stat st;
        if (stat(file_index, &st) == 0)
        {
            // File exists, extract info
            FILE *f = fopen(file_index, "r");
            if (f == NULL)
            {
                // Failed to open file for reading
                ESP_LOGW(TAG, "Failed to open index file for reading");
                index_val = 255;
                // Delete it 
                unlink(file_index);
            } else {
                char line[20];
                fgets(line, sizeof(line), f);
                sscanf(line,"%d",&index_val);
                index_val++;
                fclose(f);
                unlink(file_index);
            }
            ESP_LOGI(TAG, "Using index '%d'", index_val);

            f=fopen(file_index,"w");
            if (f == NULL) {
                ESP_LOGW(TAG, "Failed to create index file!");                
            } else {
                fprintf(f,"%d",index_val);
                fclose(f);
            }
        } else {
            //file not preset
            index_val = 1;
            FILE *f=fopen(file_index,"w");
            if (f == NULL) {
                ESP_LOGW(TAG, "Failed to create index file!");                
            } else {
                fprintf(f,"%d",index_val);
                fclose(f);
            }
        }
    }
}

esp_err_t waveshare_sd_card_init()
{
    esp_err_t ret;

    sdcard_cs_low();

    // Options for mounting the filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_SDCARD_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true, // If mount fails, format the card
#else
        .format_if_mount_failed = false, // If mount fails, do not format card
#endif
        .max_files = 5,                   // Maximum number of files
        .allocation_unit_size = 16 * 1024 // Set allocation unit size
    };

    // Initializing SD card
    ESP_LOGI(TAG, "Initializing SD card");

    // Configure SPI bus for SD card configuration
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI, // Set MOSI pin
        .miso_io_num = PIN_NUM_MISO, // Set MISO pin
        .sclk_io_num = PIN_NUM_CLK,  // Set SCLK pin
        .quadwp_io_num = -1,         // Not used
        .quadhd_io_num = -1,         // Not used
        .max_transfer_sz = 4000,     // Maximum transfer size
    };
    // Initialize SPI bus
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK)
    {
        // Failed to initialize bus
        ESP_LOGW(TAG, "Failed to initialize bus.");
        return ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "SPI init OK");
    }

    // Configure SD card slot
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS; // Set CS pin
    slot_config.host_id = host.slot;  // Set host ID

    // Mounting filesystem
    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            // Failed to mount filesystem
            ESP_LOGW(TAG, "Failed to mount filesystem. "
                          "If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        }
        else
        {
            // Failed to initialize the card
            ESP_LOGW(TAG, "Failed to initialize the card (%s). "
                          "Make sure SD card lines have pull-up resistors in place.",
                     esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    // Filesystem mounted
    ESP_LOGI(TAG, "Filesystem mounted");
    sdcard_get_info();
    return ESP_OK;
}

