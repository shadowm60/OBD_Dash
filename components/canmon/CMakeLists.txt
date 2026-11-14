# CAN bus monitor for the CAN page: ID table, frame rates, bus load (from Esp32_monitor).
idf_component_register(SRCS "canmon.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES can_if esp_timer)
