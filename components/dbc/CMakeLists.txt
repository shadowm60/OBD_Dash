# DBC (CAN database) parser + frame decoder. Pure C, also tested on the PC
# (tools/test/test_dbc.sh).
idf_component_register(SRCS "dbc.c"
                    INCLUDE_DIRS ".")
