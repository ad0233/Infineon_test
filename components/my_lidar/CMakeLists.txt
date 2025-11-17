idf_component_register(
    SRCS
        ./my_lidar.c
        ./my_lidar_airtouch.c
        ./my_lidar_r60abd1.c
    INCLUDE_DIRS
        .
    REQUIRES
        driver
        my_lcd         # 需要 UI 行为函数
)