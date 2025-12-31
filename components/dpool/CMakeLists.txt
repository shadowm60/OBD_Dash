idf_component_register(SRCS "./dpool.c" 
                            "./wrapper.cpp"
                    INCLUDE_DIRS "./"
                    REQUIRES "hmi"
                    )
set_source_files_properties(wrapper.cpp PROPERTIES LANGUAGE CXX)                    