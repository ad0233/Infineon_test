# For RISC-V architecture, use our own implementation
if(CONFIG_IDF_TARGET_ESP32P4 OR CONFIG_IDF_TARGET_ESP32C6 OR CONFIG_IDF_TARGET_ESP32C3 OR CONFIG_IDF_TARGET_ESP32C2 OR CONFIG_IDF_TARGET_ESP32C5 OR CONFIG_IDF_TARGET_ESP32C61)
    set(PRESENCE_SRCS "xensiv_radar_presence_impl.c")
    set(PRESENCE_REQUIRES "sensor-dsp" "espressif__esp-dsp")
else()
    # Xtensa architecture uses original library
    set(PRESENCE_REQUIRES "xensiv-radar-presence")
endif()

idf_component_register(
    SRCS
        "my_lidar_inf.c"
        "cli_task.c"
        "FreeRTOS_CLI.c"
        ${PRESENCE_SRCS}
    INCLUDE_DIRS
        .
    REQUIRES
        main
        ${PRESENCE_REQUIRES}
)