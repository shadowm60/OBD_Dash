# OBD-II client: request engine over ISO-TP + Service 01 PID catalogue.
# Not thread safe except the obd_pids state accessors; driven by comsm.
idf_component_register(SRCS "obd_client.c" "obd_pids.c" "obd_pid_table.c"
                    INCLUDE_DIRS "."
                    REQUIRES vehicle_data
                    PRIV_REQUIRES can_if isotp)
