idf_component_register(
    SRCS
        ./my_mqtt.c
    INCLUDE_DIRS
        .
    REQUIRES
        mqtt my_ble
    EMBED_FILES
        client_cert.crt
        client_key.crt
        root_ca.crt
)