# Latest value of every vehicle signal; shared between producers and consumers.
idf_component_register(SRCS "vehicle_data.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_timer)
