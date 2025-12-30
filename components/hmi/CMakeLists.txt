idf_component_register(SRCS "./src/ui/images.c" 
                            "./src/ui/screens.c" 
                            "./src/ui/styles.c" 
                            "./src/ui/ui.c"
                            "./src/ui/eez-flow.cpp"
                    INCLUDE_DIRS "./src/ui"
                    REQUIRES "lvgl"
                    )