#include "my_spiffs.h"
#include "periph_spiffs.h"
#include "esp_peripherals.h"
#include "esp_log.h"
#include "audio_processor.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_codec_dev.h"
#include "my_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "my_spiffs";

esp_err_t bsp_spiffs_mount(void)
{
    periph_spiffs_cfg_t spiffs_cfg = {
        .root = "/spiffs",
        .partition_label = "spiffs_data",
        .max_files = 5,
        .format_if_mount_failed = false,
    };

    esp_periph_handle_t spiffs_periph = periph_spiffs_init(&spiffs_cfg);
    if (spiffs_periph == NULL) {
        ESP_LOGE(TAG, "periph_spiffs_init fail");
        return ESP_FAIL;
    }

    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    esp_err_t ret = esp_periph_start(set, spiffs_periph);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS periph start fail: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SPIFFS 挂载成功，路径 /spiffs");
    return ESP_OK;
}

/**
 * @brief 基础音频播放函数
 * @param path 文件在SPIFFS中的路径，如 "/spiffs/V001-hello.wav"
 */
esp_err_t bsp_audio_stream_play(const char *path)
{
    esp_codec_dev_handle_t play_handle = bsp_audio_get_play_handle();
    if (play_handle == NULL) {
        ESP_LOGE(TAG, "播放句柄为空，请检查初始化");
        return ESP_FAIL;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "无法打开文件: %s", path);
        return ESP_FAIL;
    }

    fseek(f, 44, SEEK_SET);

    const size_t chunk_size = 4096;
    uint8_t *buffer = (uint8_t *)malloc(chunk_size);
    if (buffer == NULL) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_set_out_vol(play_handle, 70);
    esp_codec_dev_set_out_mute(play_handle, false);
    esp_codec_dev_handle_t play_handle_2 = bsp_audio_get_play_handle_2();
    if (play_handle_2 != NULL) {
        esp_codec_dev_set_out_vol(play_handle_2, 70);
        esp_codec_dev_set_out_mute(play_handle_2, false);
    }

    ESP_LOGI(TAG, "开始播放: %s", path);

    // 6. 核心读取与写入循环
    size_t read_len;
    while ((read_len = fread(buffer, 1, chunk_size, f)) > 0) {
        esp_codec_dev_write(play_handle, buffer, read_len);
    }

    // 等待 I2S 缓冲区播完
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "播放结束");

    // 7. 释放资源
    free(buffer);
    fclose(f);
    return ESP_OK;
}