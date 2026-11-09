# Communication state machine: owns the OBD client task; UI posts commands.
idf_component_register(SRCS "comsm.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES can_if obd vehicle_data)
