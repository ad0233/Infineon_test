idf_component_register(SRCS "my_bdm.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES driver esp_driver_i2s esp_hw_support)