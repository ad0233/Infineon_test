#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 挂载 SPIFFS 分区到 /spiffs
 * @return ESP_OK 成功，否则失败
 */
esp_err_t bsp_spiffs_mount(void);

/**
 * @brief 播放 SPIFFS 下 V001 系列音频
 * @param name 文件名后半段，如 "breath" 表示播放 /spiffs/V001-breath.wav
 * @return ESP_OK 成功，否则失败
 */
esp_err_t bsp_spiffs_play_v001(const char *name);


esp_err_t bsp_audio_stream_play(const char *path);

#ifdef __cplusplus
}
#endif
