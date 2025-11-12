#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "my_h264.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"

#include "esp_h264_dec.h"
#include "esp_h264_dec_sw.h"
#include "esp_h264_dec_param.h"

extern const uint8_t brand_motion2_h264_start[] asm("_binary_brand_motion2_h264_start");
extern const uint8_t brand_motion2_h264_end[]   asm("_binary_brand_motion2_h264_end");

static QueueHandle_t h264_queue = NULL;
static my_h264_callback_t s_callback = NULL;
static void *s_context = NULL;

static void ConvertYUV420SPToRGB565(unsigned char* src,unsigned char* Dst,int ImageWidth,int ImageHeight);
static void i420_decode_thread(void *arg);

void my_h264_init(my_h264_callback_t callback, void *context)
{
    s_callback = callback;
    s_context = context;
    h264_queue = xQueueCreate(1, sizeof(esp_h264_dec_in_frame_t));
    xTaskCreate(i420_decode_thread, "i420_decode_thread", 1024 * 10, NULL, 5, NULL);
}

int my_h264_start(uint32_t timeout_ms)
{
    esp_h264_dec_in_frame_t in_frame;
    in_frame.raw_data.buffer = brand_motion2_h264_start;
    in_frame.raw_data.len = brand_motion2_h264_end - brand_motion2_h264_start;
    if (xQueueSend(h264_queue, &in_frame, pdMS_TO_TICKS(timeout_ms)) != pdPASS) {
        return -1;
    }
    return 0;
}

static void i420_decode_thread(void *arg) {
    esp_h264_err_t ret = ESP_H264_ERR_FAIL;
    esp_h264_dec_handle_t dec = NULL;
    esp_h264_dec_cfg_sw_t cfg;
    cfg.pic_type = ESP_H264_RAW_FMT_I420;
    
    ret = esp_h264_dec_sw_new(&cfg, &dec); 
    if (ret != ESP_H264_ERR_OK) {
        ESP_LOGE("h264", "new failed. line %d \n", __LINE__);
        vTaskDelete(NULL);
        return;
    }

    ret = esp_h264_dec_open(dec);
    if (ret != ESP_H264_ERR_OK) {
        ESP_LOGE("h264", "open failed .line %d \n", __LINE__);
        vTaskDelete(NULL);
        return;
    }
    uint8_t *rgb565_360_buf = malloc(360 * 360 * 2);
    uint8_t *rgb565_368_buf = malloc(368 * 368 * 2);
    esp_h264_dec_out_frame_t out_frame;
    esp_h264_dec_in_frame_t in_frame;
    while (1) {
        if (xQueueReceive(h264_queue, &in_frame, portMAX_DELAY) == pdPASS) {
            while (1) {
                if (in_frame.raw_data.len <= 0) {
                    break;
                }
                ret = esp_h264_dec_process(dec, &in_frame, &out_frame);
                in_frame.raw_data.buffer += in_frame.consume;
                in_frame.raw_data.len -= in_frame.consume;
                if (ret == ESP_H264_ERR_OK) {
                    ConvertYUV420SPToRGB565(out_frame.outbuf, rgb565_368_buf, 368, 368);
                    // 368 转 360
                    uint16_t *src_line = (uint16_t *)rgb565_368_buf;
                    uint16_t *dst_line = (uint16_t *)rgb565_360_buf;
                    for (int row = 0; row < 360; ++row) {
                        memcpy(dst_line + row * 360, src_line + row * 368, 360 * sizeof(uint16_t));
                    }

                    s_callback(rgb565_360_buf, 360 * 360 * 2, s_context);
                } else {
                    ESP_LOGE("h264", "decode failed. line %d \n", __LINE__);
                    break;
                }
            }
        }
    }
}

static void ConvertYUV420SPToRGB565(unsigned char* src,unsigned char* Dst,int ImageWidth,int ImageHeight)
{
    if (ImageWidth < 1 || ImageHeight < 1 || src == NULL || Dst == NULL)
        return;
    const long len = ImageWidth * ImageHeight;
    unsigned char* yData = src;
    unsigned char* uData = &yData[len];
    unsigned char* vData = &uData[len >> 2];
    int yIdx,uIdx,vIdx;
    for (int i = 0; i < ImageHeight; i++){
        for (int j = 0; j < ImageWidth; j++){
            yIdx = i * ImageWidth + j;
            vIdx = (i/2) * (ImageWidth/2) + (j/2);
            uIdx = vIdx;

            int y = yData[yIdx] - 16;
            int u = uData[uIdx] - 128;
            int v = vData[vIdx] - 128;

            if (y < 0) {
                y = 0;
            }

            int r = (298 * y + 409 * v + 128) >> 8;
            int g = (298 * y - 100 * u - 208 * v + 128) >> 8;
            int b = (298 * y + 516 * u + 128) >> 8;

            if (r < 0) {
                r = 0;
            } else if (r > 255) {
                r = 255;
            }
            if (g < 0) {
                g = 0;
            } else if (g > 255) {
                g = 255;
            }
            if (b < 0) {
                b = 0;
            } else if (b > 255) {
                b = 255;
            }

            uint16_t color = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            size_t byte_pos = (size_t)yIdx << 1;
            Dst[byte_pos] = (uint8_t)(color >> 8);
            Dst[byte_pos + 1] = (uint8_t)(color & 0xFF);
        }
    }
}
