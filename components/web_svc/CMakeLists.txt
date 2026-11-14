# Log file manager over Wi-Fi (HTTP): download / delete the SD card logs.
idf_component_register(SRCS "web_svc.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES board datalog esp_http_server fatfs)
