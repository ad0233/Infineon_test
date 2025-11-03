idf_component_register(
    SRCS
        ./my_wifi.c
    INCLUDE_DIRS
        .
    REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        my_nvs
)


