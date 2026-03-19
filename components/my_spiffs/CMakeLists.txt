idf_component_register(SRCS "my_spiffs.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_peripherals audio_processor esp_codec_dev my_audoi_board)
