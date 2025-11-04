idf_component_register(
    SRCS
        "my_utils.c"
        "pwm_control.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        freertos
        heap
        driver
)

