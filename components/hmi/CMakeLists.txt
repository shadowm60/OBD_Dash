# UI: EEZ Studio generated code (src/ui, do not edit by hand) + bridge (hand written).
# The bridge is the only code that knows EEZ variable indices and LVGL objects.
idf_component_register(SRCS "./src/ui/images.c"
                            "./src/ui/screens.c"
                            "./src/ui/styles.c"
                            "./src/ui/ui.c"
                            "./src/ui/eez-flow.cpp"
                            "./bridge/actions.c"
                            "./bridge/eez_vars.cpp"
                            "./bridge/ui_bridge.c"
                            "./bridge/settings_view.c"
                            "./bridge/diag_view.c"
                            "./bridge/pid_select_view.c"
                            "./bridge/live_view.c"
                    INCLUDE_DIRS "./bridge"
                    PRIV_INCLUDE_DIRS "./src/ui"
                    REQUIRES lvgl
                    PRIV_REQUIRES comsm obd vehicle_data settings datalog esp_timer)
