#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "my_h264.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "esp_h264_dec.h"
#include "esp_h264_dec_sw.h"
#include "esp_h264_dec_param.h"

extern const uint8_t _binary_brand_motion2_h264_start[];
extern const uint8_t _binary_brand_motion2_h264_end[];
extern const uint8_t _binary_fail2_h264_start[];
extern const uint8_t _binary_fail2_h264_end[];
extern const uint8_t _binary_go_up_h264_start[];
extern const uint8_t _binary_go_up_h264_end[];
extern const uint8_t _binary_human_recognized_h264_start[];
extern const uint8_t _binary_human_recognized_h264_end[];
extern const uint8_t _binary_processing_h264_start[];
extern const uint8_t _binary_processing_h264_end[];
extern const uint8_t _binary_success2_h264_start[];
extern const uint8_t _binary_success2_h264_end[];

static QueueHandle_t h264_queue = NULL;
static QueueHandle_t s_rgb_ready_queue = NULL;
static QueueHandle_t s_rgb_free_queue = NULL;
static my_h264_callback_t s_callback = NULL;
static void *s_context = NULL;

typedef struct {
    uint8_t *buf;
    size_t len;
} rgb565_frame_t;

#define RGB565_BUFFER_COUNT (2)
#define CLIP_TABLE_SIZE     (2048)
#define CLIP_TABLE_OFFSET   (512)

static void ensure_lut_ready(void);
static void ConvertYUV420SPToRGB565_LUT(const unsigned char *src,
                                        unsigned char *dst,
                                        int src_width,
                                        int src_height,
                                        int dst_width,
                                        int dst_height);
static void i420_decode_thread(void *arg);
static void playback_thread(void *arg);

static bool s_lut_ready = false;
static int32_t s_y_table[256];
static int32_t s_u_b_table[256];
static int32_t s_u_g_table[256];
static int32_t s_v_r_table[256];
static int32_t s_v_g_table[256];
static uint8_t s_clip_table[CLIP_TABLE_SIZE];

static uint16_t s_src_width = 0;
static uint16_t s_src_height = 0;
static uint16_t s_dst_width = 0;
static uint16_t s_dst_height = 0;
static size_t s_rgb_frame_bytes = 0;
static bool s_buffers_initialized = false;

static inline uint16_t align16(uint16_t value)
{
    return (value + 15) & ~0x0F;
}

void my_h264_init(my_h264_callback_t callback, void *context)
{
    s_callback = callback;
    s_context = context;
    h264_queue = xQueueCreate(1, sizeof(esp_h264_dec_in_frame_t));
    s_rgb_ready_queue = xQueueCreate(RGB565_BUFFER_COUNT, sizeof(rgb565_frame_t));
    s_rgb_free_queue = xQueueCreate(RGB565_BUFFER_COUNT, sizeof(uint8_t *));
    if (h264_queue == NULL || s_rgb_ready_queue == NULL || s_rgb_free_queue == NULL) {
        ESP_LOGE("h264", "Failed to create queues");
        return;
    }
    if (xTaskCreatePinnedToCore(playback_thread, "h264_play_thread", 1024 * 6, NULL, 5, NULL, 1) != pdPASS) {
        ESP_LOGE("h264", "Failed to create playback task");
        return;
    }
    if (xTaskCreatePinnedToCore(i420_decode_thread, "i420_decode_thread", 1024 * 10, NULL, 5, NULL, 0) != pdPASS) {
        ESP_LOGE("h264", "Failed to create decode task");
    }
}

int my_h264_start(my_h264_animation_t animation, uint32_t timeout_ms)
{
    esp_h264_dec_in_frame_t in_frame;
    const uint8_t *start = NULL;
    const uint8_t *end = NULL;

    switch (animation) {
        case MY_H264_ANIM_BRAND_MOTION2:
            start = _binary_brand_motion2_h264_start;
            end = _binary_brand_motion2_h264_end;
            break;
        case MY_H264_ANIM_FAIL2:
            start = _binary_fail2_h264_start;
            end = _binary_fail2_h264_end;
            break;
        case MY_H264_ANIM_GO_UP:
            start = _binary_go_up_h264_start;
            end = _binary_go_up_h264_end;
            break;
        case MY_H264_ANIM_HUMAN_RECOGNIZED:
            start = _binary_human_recognized_h264_start;
            end = _binary_human_recognized_h264_end;
            break;
        case MY_H264_ANIM_PROCESSING:
            start = _binary_processing_h264_start;
            end = _binary_processing_h264_end;
            break;
        case MY_H264_ANIM_SUCCESS2:
            start = _binary_success2_h264_start;
            end = _binary_success2_h264_end;
            break;
        default:
            return -1;
    }

    if (start == NULL || end == NULL || end <= start) {
        return -1;
    }

    in_frame.raw_data.buffer = start;
    in_frame.raw_data.len = end - start;
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
    ensure_lut_ready();

    esp_h264_dec_param_sw_handle_t param_handle = NULL;
    esp_h264_resolution_t res = {0};
    ret = esp_h264_dec_sw_get_param_hd(dec, &param_handle);
    if (ret == ESP_H264_ERR_OK) {
        ret = esp_h264_dec_get_resolution(param_handle, &res);
    }
    if (ret != ESP_H264_ERR_OK || res.width == 0 || res.height == 0) {
        // ESP_LOGW("h264", "Failed to get resolution, fallback to 360x360");
        res.width = 360;
        res.height = 360;
    }

    s_dst_width = res.width;
    s_dst_height = res.height;
    s_src_width = align16(res.width);
    s_src_height = align16(res.height);
    s_rgb_frame_bytes = (size_t)s_dst_width * s_dst_height * 2;

    if (s_rgb_free_queue == NULL || s_rgb_ready_queue == NULL) {
        ESP_LOGE("h264", "RGB queues are not ready");
        vTaskDelete(NULL);
        return;
    }

    if (!s_buffers_initialized) {
        for (int i = 0; i < RGB565_BUFFER_COUNT; ++i) {
            uint8_t *buf = (uint8_t *)heap_caps_malloc(s_rgb_frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (buf == NULL) {
                ESP_LOGE("h264", "Failed to allocate RGB buffer");
                vTaskDelete(NULL);
                return;
            }
            if (xQueueSend(s_rgb_free_queue, &buf, 0) != pdPASS) {
                ESP_LOGE("h264", "Failed to enqueue RGB buffer");
                heap_caps_free(buf);
                vTaskDelete(NULL);
                return;
            }
        }
        s_buffers_initialized = true;
    }

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
                    uint8_t *rgb565_buf = NULL;
                    if (xQueueReceive(s_rgb_free_queue, &rgb565_buf, portMAX_DELAY) != pdPASS) {
                        ESP_LOGE("h264", "Failed to get free RGB buffer");
                        break;
                    }
                    ConvertYUV420SPToRGB565_LUT(out_frame.outbuf,
                                                rgb565_buf,
                                                s_src_width,
                                                s_src_height,
                                                s_dst_width,
                                                s_dst_height);
                    rgb565_frame_t frame = {
                        .buf = rgb565_buf,
                        .len = s_rgb_frame_bytes,
                    };
                    if (xQueueSend(s_rgb_ready_queue, &frame, portMAX_DELAY) != pdPASS) {
                        ESP_LOGE("h264", "Failed to enqueue RGB frame");
                        xQueueSend(s_rgb_free_queue, &rgb565_buf, portMAX_DELAY);
                        break;
                    }
                } else {
                    ESP_LOGE("h264", "decode failed. line %d \n", __LINE__);
                    break;
                }
            }
        }
    }
}

static void ensure_lut_ready(void)
{
    if (s_lut_ready) {
        return;
    }
    for (int i = 0; i < 256; ++i) {
        int y = i - 16;
        if (y < 0) {
            y = 0;
        }
        s_y_table[i] = 298 * y;
        int u = i - 128;
        s_u_b_table[i] = 516 * u;
        s_u_g_table[i] = -100 * u;
        int v = i - 128;
        s_v_r_table[i] = 409 * v;
        s_v_g_table[i] = -208 * v;
    }
    for (int i = 0; i < CLIP_TABLE_SIZE; ++i) {
        int value = i - CLIP_TABLE_OFFSET;
        if (value < 0) {
            value = 0;
        } else if (value > 255) {
            value = 255;
        }
        s_clip_table[i] = (uint8_t)value;
    }
    s_lut_ready = true;
}

static void ConvertYUV420SPToRGB565_LUT(const unsigned char *src,
                                        unsigned char *dst,
                                        int src_width,
                                        int src_height,
                                        int dst_width,
                                        int dst_height)
{
    if (src == NULL || dst == NULL || src_width <= 0 || src_height <= 0 ||
        dst_width <= 0 || dst_height <= 0 || dst_width > src_width || dst_height > src_height) {
        return;
    }

    ensure_lut_ready();

    const size_t y_plane_size = (size_t)src_width * src_height;
    const unsigned char *yData = src;
    const unsigned char *uData = yData + y_plane_size;
    const unsigned char *vData = uData + (y_plane_size >> 2);

    const int half_src_width = src_width >> 1;

    for (int i = 0; i < dst_height; ++i) {
        const int y_row_offset = i * src_width;
        const int uv_row_offset = (i >> 1) * half_src_width;
        const int dst_row_offset = i * dst_width;
        for (int j = 0; j < dst_width; ++j) {
            const int yIdx = y_row_offset + j;
            const int uvIdx = uv_row_offset + (j >> 1);

            const int y_component = s_y_table[yData[yIdx]];
            const int u_value = uData[uvIdx];
            const int v_value = vData[uvIdx];

            const int r_val = (y_component + s_v_r_table[v_value] + 128) >> 8;
            const int g_val = (y_component + s_u_g_table[u_value] + s_v_g_table[v_value] + 128) >> 8;
            const int b_val = (y_component + s_u_b_table[u_value] + 128) >> 8;

            const uint8_t r = s_clip_table[r_val + CLIP_TABLE_OFFSET];
            const uint8_t g = s_clip_table[g_val + CLIP_TABLE_OFFSET];
            const uint8_t b = s_clip_table[b_val + CLIP_TABLE_OFFSET];

            const uint16_t color = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            const size_t dst_pos = ((size_t)dst_row_offset + j) << 1;
            dst[dst_pos] = (uint8_t)(color >> 8);
            dst[dst_pos + 1] = (uint8_t)(color & 0xFF);
        }
    }
}

static void playback_thread(void *arg)
{
    rgb565_frame_t frame;
    while (1) {
        if (s_rgb_ready_queue == NULL || s_rgb_free_queue == NULL) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (xQueueReceive(s_rgb_ready_queue, &frame, portMAX_DELAY) == pdPASS) {
            if (s_callback != NULL) {
                s_callback(frame.buf, frame.len, s_context);
            }
            xQueueSend(s_rgb_free_queue, &frame.buf, portMAX_DELAY);
        }
    }
}
