#include <stdbool.h>
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

static void i420_to_rgb_360x360(uint8_t *yuv, uint8_t *rgb);
static void i420_decode_thread(void *arg);

void my_h264_init(my_h264_callback_t callback, void *context)
{
    i420_to_rgb_360x360(NULL, NULL);
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
    uint8_t *rgb565_buf = malloc(360 * 360 * 2);
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
                    i420_to_rgb_360x360(out_frame.outbuf, rgb565_buf);
                    s_callback(rgb565_buf, out_frame.out_size, s_context);
                } else {
                    ESP_LOGE("h264", "decode failed. line %d \n", __LINE__);
                    break;
                }
            }
        }
    }
}


static void i420_to_rgb_360x360(uint8_t *yuv, uint8_t *rgb) {
    enum {
        WIDTH = 360,
        HEIGHT = 360,
        PADDED_WIDTH = 368,
        PADDED_HEIGHT = 368,
        Y_STRIDE = PADDED_WIDTH,
        UV_STRIDE = PADDED_WIDTH / 2,
        Y_PLANE_SIZE = PADDED_WIDTH * PADDED_HEIGHT,
        UV_PLANE_HEIGHT = PADDED_HEIGHT / 2,
        UV_PLANE_SIZE = UV_STRIDE * UV_PLANE_HEIGHT
    };

    static bool tables_inited = false;
    static int32_t y_table[256];
    static int32_t r_v_table[256];
    static int32_t g_u_table[256];
    static int32_t g_v_table[256];
    static int32_t b_u_table[256];
    static uint8_t clip_table[1024];
    static uint8_t *const clip = clip_table + 256;

    if (!tables_inited) {
        for (int i = -256; i < 768; ++i) {
            int v = i;
            if (v < 0) {
                v = 0;
            } else if (v > 255) {
                v = 255;
            }
            clip[i] = (uint8_t)v;
        }
        for (int i = 0; i < 256; ++i) {
            y_table[i] = (int32_t)(298 * ((int16_t)i - 16));
            r_v_table[i] = (int32_t)(409 * ((int16_t)i - 128));
            g_u_table[i] = (int32_t)(-100 * ((int16_t)i - 128));
            g_v_table[i] = (int32_t)(-208 * ((int16_t)i - 128));
            b_u_table[i] = (int32_t)(516 * ((int16_t)i - 128));
        }
        tables_inited = true;
    }
    
    if (yuv == NULL || rgb == NULL) {
        return;
    }

    uint8_t *y_plane = yuv;
    uint8_t *u_plane = yuv + Y_PLANE_SIZE;
    uint8_t *v_plane = u_plane + UV_PLANE_SIZE;

    for (int row = 0; row < HEIGHT; row += 2) {
        uint8_t *y_row0 = y_plane + row * Y_STRIDE;
        uint8_t *y_row1 = y_row0 + Y_STRIDE;
        uint8_t *u_row = u_plane + (row / 2) * UV_STRIDE;
        uint8_t *v_row = v_plane + (row / 2) * UV_STRIDE;
        uint16_t *rgb_row0 = (uint16_t *)(rgb + row * WIDTH * 2);
        uint16_t *rgb_row1 = (uint16_t *)(rgb + (row + 1) * WIDTH * 2);

        for (int col = 0; col < WIDTH; col += 2) {
            uint8_t u = u_row[col / 2];
            uint8_t v = v_row[col / 2];
            int32_t rv = r_v_table[v];
            int32_t guv = g_u_table[u] + g_v_table[v];
            int32_t bu = b_u_table[u];

            int32_t y00 = y_table[y_row0[0]];
            int32_t y01 = y_table[y_row0[1]];
            int32_t y10 = y_table[y_row1[0]];
            int32_t y11 = y_table[y_row1[1]];

            int32_t r = (y00 + rv + 128) >> 8;
            int32_t g = (y00 + guv + 128) >> 8;
            int32_t b = (y00 + bu + 128) >> 8;
            uint8_t r8 = clip[r];
            uint8_t g8 = clip[g];
            uint8_t b8 = clip[b];
            rgb_row0[0] = (uint16_t)(((r8 & 0xF8) << 8) | ((g8 & 0xFC) << 3) | (b8 >> 3));

            r = (y01 + rv + 128) >> 8;
            g = (y01 + guv + 128) >> 8;
            b = (y01 + bu + 128) >> 8;
            r8 = clip[r];
            g8 = clip[g];
            b8 = clip[b];
            rgb_row0[1] = (uint16_t)(((r8 & 0xF8) << 8) | ((g8 & 0xFC) << 3) | (b8 >> 3));

            r = (y10 + rv + 128) >> 8;
            g = (y10 + guv + 128) >> 8;
            b = (y10 + bu + 128) >> 8;
            r8 = clip[r];
            g8 = clip[g];
            b8 = clip[b];
            rgb_row1[0] = (uint16_t)(((r8 & 0xF8) << 8) | ((g8 & 0xFC) << 3) | (b8 >> 3));

            r = (y11 + rv + 128) >> 8;
            g = (y11 + guv + 128) >> 8;
            b = (y11 + bu + 128) >> 8;
            r8 = clip[r];
            g8 = clip[g];
            b8 = clip[b];
            rgb_row1[1] = (uint16_t)(((r8 & 0xF8) << 8) | ((g8 & 0xFC) << 3) | (b8 >> 3));

            y_row0 += 2;
            y_row1 += 2;
            rgb_row0 += 2;
            rgb_row1 += 2;
        }
    }
}