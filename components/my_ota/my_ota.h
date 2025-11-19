#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA 进度回调函数类型
 * @param bytes_read 已读取的字节数
 * @param total_bytes 总字节数（如果可用，否则为 0）
 * @param user_ctx 用户上下文指针
 */
typedef void (*ota_progress_callback_t)(int bytes_read, int total_bytes, void *user_ctx);

/**
 * @brief 注册 OTA 进度回调函数
 * @param progress_cb 进度回调函数（可为 NULL）
 * @param user_ctx 用户上下文指针（传递给回调函数）
 */
void my_ota_register_progress_callback(ota_progress_callback_t progress_cb, void *user_ctx);

/**
 * @brief 启动 OTA 更新
 * @param url_ota_file OTA 文件 URL
 * @param ota_size OTA 文件总大小（字节），用于计算百分比，0 表示未知
 * @return 0 成功，-1 失败
 */
int my_ota_start(const char *url_ota_file, int ota_size);

#ifdef __cplusplus
}
#endif
