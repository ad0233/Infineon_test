set(COMPONENT_SRCS
    "main.cpp"
    "fsm_main.cpp"
    "fsm_main_event.cpp"
    "cmd_parse.cpp"
    "cmd_handlers.cpp"
    "mqtt_protocol.cpp"
    "ble_protocol.cpp"
    "btn.c"
)
set(COMPONENT_ADD_INCLUDEDIRS .)
register_component()
idf_component_add_link_dependency(FROM main TO my_light)
idf_component_add_link_dependency(FROM main TO my_bmp580)
idf_component_add_link_dependency(FROM main TO my_veml7700)
idf_component_add_link_dependency(FROM main TO my_bdm)

spiffs_create_partition_image(spiffs_data ../spiffs FLASH_IN_PROJECT)
