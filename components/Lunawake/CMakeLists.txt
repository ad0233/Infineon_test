file(GLOB SCREENS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/screens/*.c")
file(GLOB IMAGES_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/images/*.c")
file(GLOB FONTS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/fonts/*.c")
file(GLOB COMPONENTS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/components/*.c")

SET(SOURCES 
    ${SCREENS_SOURCES}
    ${IMAGES_SOURCES}
    ${FONTS_SOURCES}
    ${COMPONENTS_SOURCES}
    ui.c
    ui_helpers.c
)

idf_component_register(
    SRCS
        ${SOURCES}
    INCLUDE_DIRS
        .
        screens
        components
        images
    REQUIRES
        lvgl  # 声明依赖 lvgl
)