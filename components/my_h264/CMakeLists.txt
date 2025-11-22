file(GLOB H264_BIN_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "h264/*.h264")

idf_component_register(
    SRCS
        ./my_h264.c
    INCLUDE_DIRS
        .
    REQUIRES
        main
    EMBED_FILES
        ${H264_BIN_FILES}
)