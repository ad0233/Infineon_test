idf_component_register(INCLUDE_DIRS ".")

# xensiv-radar-presence library only supports Xtensa architecture (ESP32/ESP32-S3)
# Skip library linking for RISC-V architecture (ESP32P4, ESP32C6, etc.)
if(CONFIG_IDF_TARGET_ESP32 OR CONFIG_IDF_TARGET_ESP32S3)
    add_prebuilt_library(libprebuilt "xensiv-radar-presence.a"
                         REQUIRES "espressif__esp-dsp" "sensor-dsp")
    target_link_libraries(${COMPONENT_LIB} INTERFACE libprebuilt)
else()
    # For unsupported architectures, just register the component without library
    # This allows header files to be included but library won't be linked
    message(WARNING "xensiv-radar-presence: Library not available for ${IDF_TARGET}, only headers will be available")
endif()