# CAN bus interface (esp_twai). No knowledge of ISO-TP / OBD / UI.
idf_component_register(SRCS "can_if.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_twai
                    PRIV_REQUIRES esp_timer)
