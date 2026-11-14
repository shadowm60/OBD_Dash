# UI: EEZ Studio generated code (src/ui, never edit by hand: change the EEZ
# project - tools/obd_ui.py for the menu pages - and run tools/eez-build.sh)
# + bridge (hand written, the only code touching EEZ variables / LVGL).
set(var_table "${CMAKE_CURRENT_BINARY_DIR}/eez_var_table.c")

idf_component_register(SRCS "./src/ui/images.c"
                            "./src/ui/screens.c"
                            "./src/ui/styles.c"
                            "./src/ui/ui.c"
                            "./src/ui/eez-flow.cpp"
                            "./bridge/eez_vars.cpp"
                            "./bridge/uivar.c"
                            "./bridge/nav.c"
                            "./bridge/screen.c"
                            "./bridge/hmi.c"
                            "./bridge/actions.c"
                            "./bridge/view_sys.c"
                            "./bridge/view_obd.c"
                            "./bridge/view_wifi.c"
                            "./bridge/view_ble.c"
                            "./bridge/view_can.c"
                            "./bridge/view_io.c"
                            "./bridge/view_elm.c"
                            "./bridge/ui_bridge.c"
                            "./bridge/diag_view.c"
                            "./bridge/pid_select_view.c"
                            "./bridge/live_view.c"
                            "./bridge/live_values.c"
                            "./bridge/brightness.c"
                            "${var_table}"
                    INCLUDE_DIRS "./bridge"
                    PRIV_INCLUDE_DIRS "./src/ui"
                    REQUIRES lvgl
                    PRIV_REQUIRES board comsm obd vehicle_data settings datalog can_if canmon io_svc wifi_svc ble_svc web_svc
                                  esp_timer esp_driver_ledc esp_app_format esp_psram fatfs spi_flash)

# EEZ variable and screen names -> indices, from the generated vars.h / screens.h
idf_build_get_property(python PYTHON)
set(gen_script "${CMAKE_CURRENT_LIST_DIR}/../../tools/gen_eez_var_table.py")
set(vars_h "${CMAKE_CURRENT_LIST_DIR}/src/ui/vars.h")
set(screens_h "${CMAKE_CURRENT_LIST_DIR}/src/ui/screens.h")
add_custom_command(OUTPUT "${var_table}"
                   COMMAND ${python} "${gen_script}" "${vars_h}" "${screens_h}" "${var_table}"
                   DEPENDS "${vars_h}" "${screens_h}" "${gen_script}"
                   COMMENT "Generating EEZ variable / screen tables"
                   VERBATIM)
