# Isolated digital inputs / outputs on the terminal block (CH422G), I2C scan,
# chip temperature (IO page). Parts from Esp32_monitor's io_svc.
idf_component_register(SRCS "io_svc.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES board esp_driver_tsens esp_driver_i2c esp_timer)
