# Communication state machine: owns the OBD client task; UI posts commands.
idf_component_register(SRCS "comsm.c" "comsm_diag.c"
                    INCLUDE_DIRS "."
                    REQUIRES vehicle_data
                    PRIV_REQUIRES can_if obd)
