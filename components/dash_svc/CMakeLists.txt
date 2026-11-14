# Dash page: decodes a CAN broadcast (rusEFI) with a DBC from the SD card.
idf_component_register(SRCS "dash_svc.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES can_if dbc vfs)
