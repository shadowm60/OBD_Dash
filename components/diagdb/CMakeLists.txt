# Diagnostic database parser (docs/diag_db.md). Pure C, PC test:
# tools/test/test_diagdb_c.sh
idf_component_register(SRCS "diagdb.c"
                    INCLUDE_DIRS ".")
