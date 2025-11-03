# 自动扫描 images 文件夹并生成图片声明的 CMake 脚本

# 获取所有图片源文件
file(GLOB IMAGE_FILES "${IMAGES_DIR}/ui_img_*.c")

# 生成声明
set(DECLARATIONS "// 自动生成的图片声明 - 请勿手动编辑\n// Auto-generated image declarations - DO NOT EDIT\n\n")

foreach(IMG_FILE ${IMAGE_FILES})
    # 获取文件名（不含扩展名）
    get_filename_component(IMG_NAME ${IMG_FILE} NAME_WE)
    set(DECLARATIONS "${DECLARATIONS}LV_IMG_DECLARE(${IMG_NAME});\n")
endforeach()

# 写入头文件
file(WRITE "${OUTPUT_FILE}" "${DECLARATIONS}")

message(STATUS "✅ 已自动生成 ${OUTPUT_FILE}")


