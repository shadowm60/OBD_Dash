# SD card logging: raw CAN (candump format) and polled parameters (CSV).
idf_component_register(SRCS "datalog.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES can_if comsm obd vehicle_data vfs)
