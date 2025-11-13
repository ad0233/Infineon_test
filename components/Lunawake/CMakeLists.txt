file(GLOB SCREENS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/screens/*.c")
file(GLOB IMAGES_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/images/*.c")
file(GLOB FONTS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/fonts/*.c")
file(GLOB COMPONENTS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/components/*.c")
file(GLOB UI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
file(GLOB UI_ASSET_BIN_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "drive/assets/*.bin")

SET(SOURCES 
    ${SCREENS_SOURCES}
    ${IMAGES_SOURCES}
    ${FONTS_SOURCES}
    ${COMPONENTS_SOURCES}
    ${UI_SOURCES}
)

idf_component_register(
    SRCS
        ${SOURCES}
    INCLUDE_DIRS
        .
        screens
        components
        images
    EMBED_FILES
        ${UI_ASSET_BIN_FILES}
    REQUIRES
        lvgl  # 声明依赖 lvgl
)