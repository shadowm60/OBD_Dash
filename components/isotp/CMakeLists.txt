# ISO 15765-2 transport (third-party isotp-c + glue to can_if).
idf_component_register(SRCS "isotp.c"
                            "isotp_user.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES can_if)
