idf_component_register(SRCS "my_board.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_driver_i2c esp_codec_dev) # 显式指定 esp_driver_i2c