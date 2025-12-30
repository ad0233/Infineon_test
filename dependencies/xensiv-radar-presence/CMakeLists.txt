idf_component_register(INCLUDE_DIRS ".")
add_prebuilt_library(libprebuilt "xensiv-radar-presence.a"
                     REQUIRES "espressif__esp-dsp" "sensor-dsp")
target_link_libraries(${COMPONENT_LIB} INTERFACE libprebuilt)