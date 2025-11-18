set(COMPONENT_SRCS
    "main.cpp"
    "fsm_main.cpp"
    "fsm_main_event.cpp"
    "cmd_parse.cpp"
    "cmd_handlers.cpp"
    "mqtt_protocol.cpp"
)
set(COMPONENT_ADD_INCLUDEDIRS .)
register_component()

# spiffs_create_partition_image(spiffs_data ../spiffs FLASH_IN_PROJECT)
