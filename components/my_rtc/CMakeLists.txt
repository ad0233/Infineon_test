idf_component_register(
    SRCS
        my_rtc.c pcf8563.c
    INCLUDE_DIRS
        .
    REQUIRES
        esp_peripherals
)