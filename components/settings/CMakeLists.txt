# Persistent user configuration in NVS (one versioned blob, delayed save).
idf_component_register(SRCS "settings.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES nvs_flash esp_timer)
