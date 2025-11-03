idf_component_register(
    SRCS
        ./my_nvs.c
    INCLUDE_DIRS
        .
    REQUIRES
        driver nvs_flash
)