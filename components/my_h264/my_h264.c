#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "my_h264.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include "esp_h264_dec.h"
#include "esp_h264_dec_sw.h"
#include "esp_h264_dec_param.h"

#include "esp_lvgl_port.h"

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
extern const uint8_t _binary_cat_h264_start[];
extern const uint8_t _binary_cat_h264_end[];

static QueueHandle_t h264_queue = NULL;
static QueueHandle_t s_rgb_ready_queue = NULL;
static QueueHandle_t s_rgb_free_queue = NULL;
static EventGroupHandle_t s_playback_event_group = NULL;
static my_h264_callback_t s_callback = NULL;
static void *s_context = NULL;
static my_h264_done_callback_t s_done_callback = NULL;
static void *s_done_context = NULL;

typedef struct {
    uint8_t *buf;
    size_t len;
    bool is_end;
} rgb565_frame_t;

#define RGB565_BUFFER_COUNT (2)
#define CLIP_TABLE_SIZE     (2048)
#define CLIP_TABLE_OFFSET   (512)
#define PLAYBACK_DONE_BIT   (1 << 0)

static void ensure_lut_ready(void);
static void ConvertYUV420SPToRGB565_LUT(const unsigned char *src,
                                        unsigned char *dst,
                                        int src_width,
                                        int src_height,
                                        int dst_width,
                                        int dst_height);
static void i420_decode_thread(void *arg);
static void playback_thread(void *arg);
static TickType_t fps_to_ticks(uint32_t fps);
static TickType_t wait_timeout_to_ticks(uint32_t timeout_ms);

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
static uint32_t s_target_fps = 15;
static TickType_t s_frame_interval_ticks = 0;

#define H264_ANIM_COUNT (7)
// 单个共享 PSRAM 缓冲区：存储当前播放的动画数据（大小为最大动画的大小）
static uint8_t *s_h264_shared_buf = NULL;
static size_t s_h264_shared_buf_size = 0;
static bool s_psram_buffer_allocated = false;

static inline uint16_t align16(uint16_t value)
{
    return (value + 15) & ~0x0F;
}

static TickType_t fps_to_ticks(uint32_t fps)
{
    if (fps == 0) {
        return 0;
    }
    uint32_t ticks_per_second = configTICK_RATE_HZ;
    TickType_t interval = (TickType_t)((ticks_per_second + fps - 1) / fps);
    if (interval == 0) {
        interval = 1;
    }
    return interval;
}

static TickType_t wait_timeout_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == MY_H264_WAIT_FOREVER) {
        return portMAX_DELAY;
    }
    return pdMS_TO_TICKS(timeout_ms);
}

const struct {
    const uint8_t *start;
    const uint8_t *end;
} anim_data[H264_ANIM_COUNT] = {
    {_binary_brand_motion2_h264_start, _binary_brand_motion2_h264_end},
    {_binary_fail2_h264_start, _binary_fail2_h264_end},
    {_binary_go_up_h264_start, _binary_go_up_h264_end},
    {_binary_human_recognized_h264_start, _binary_human_recognized_h264_end},
    {_binary_processing_h264_start, _binary_processing_h264_end},
    {_binary_success2_h264_start, _binary_success2_h264_end},
    {_binary_cat_h264_start, _binary_cat_h264_end},
};

void my_h264_init(my_h264_callback_t callback, void *context, my_h264_done_callback_t done_callback, void *done_context)
{
    s_callback = callback;
    s_context = context;
    s_done_callback = done_callback;
    s_done_context = done_context;
    h264_queue = xQueueCreate(1, sizeof(esp_h264_dec_in_frame_t));
    s_rgb_ready_queue = xQueueCreate(RGB565_BUFFER_COUNT, sizeof(rgb565_frame_t));
    s_rgb_free_queue = xQueueCreate(RGB565_BUFFER_COUNT, sizeof(uint8_t *));
    if (h264_queue == NULL || s_rgb_ready_queue == NULL || s_rgb_free_queue == NULL) {
        ESP_LOGE("h264", "Failed to create queues");
        return;
    }
    if (s_playback_event_group == NULL) {
        s_playback_event_group = xEventGroupCreate();
        if (s_playback_event_group == NULL) {
            ESP_LOGE("h264", "Failed to create event group");
            return;
        }
        xEventGroupSetBits(s_playback_event_group, PLAYBACK_DONE_BIT);
    } else {
        xEventGroupSetBits(s_playback_event_group, PLAYBACK_DONE_BIT);
    }
    
    // 分配单个共享 PSRAM 缓冲区（大小为最大动画的大小）
    if (!s_psram_buffer_allocated) {
        // 找到最大动画大小
        size_t max_size = 0;
        for (int i = 0; i < H264_ANIM_COUNT; i++) {
            size_t len = anim_data[i].end - anim_data[i].start;
            if (len > max_size) {
                max_size = len;
            }
        }
        
        if (max_size > 0) {
            s_h264_shared_buf = (uint8_t *)heap_caps_malloc(max_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (s_h264_shared_buf == NULL) {
                ESP_LOGE("h264", "Failed to allocate shared PSRAM buffer, size: %zu", max_size);
                return;
            }
            s_h264_shared_buf_size = max_size;
            s_psram_buffer_allocated = true;
            ESP_LOGI("h264", "Allocated shared PSRAM buffer, size: %zu", max_size);
        }
    }
    
    s_frame_interval_ticks = fps_to_ticks(s_target_fps);
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
    uint8_t *start = NULL;
    size_t len;

    if (animation < 0 || animation >= H264_ANIM_COUNT) {
        ESP_LOGE("h264", "%s invalid animation: %d", __func__, animation);
        return -1;
    }

    if (!s_psram_buffer_allocated || s_h264_shared_buf == NULL) {
        ESP_LOGE("h264", "%s PSRAM buffer not allocated", __func__);
        return -1;
    }

    len = anim_data[animation].end - anim_data[animation].start;
    if (len == 0 || len > s_h264_shared_buf_size) {
        ESP_LOGE("h264", "%s invalid animation size: %zu (max: %zu)", __func__, len, s_h264_shared_buf_size);
        return -1;
    }

    // 每次播放都从 flash 复制到共享缓冲区
    memcpy(s_h264_shared_buf, anim_data[animation].start, len);
    start = s_h264_shared_buf;

    if (s_playback_event_group == NULL) {
        ESP_LOGE("h264", "%s s_playback_event_group == NULL", __func__);
        return -1;
    }
    EventBits_t previous_bits = xEventGroupClearBits(s_playback_event_group, PLAYBACK_DONE_BIT);
    if ((previous_bits & PLAYBACK_DONE_BIT) == 0) {
        ESP_LOGE("h264", "%s (previous_bits & PLAYBACK_DONE_BIT) == 0", __func__);
        return -1;
    }
    memset(&in_frame, 0, sizeof(in_frame));
    in_frame.raw_data.buffer = start;
    in_frame.raw_data.len = len;
    TickType_t wait_ticks = wait_timeout_to_ticks(timeout_ms);
    if (xQueueSend(h264_queue, &in_frame, wait_ticks) != pdPASS) {
        xEventGroupSetBits(s_playback_event_group, PLAYBACK_DONE_BIT);
        ESP_LOGE("h264", "%s xQueueSend(h264_queue, &in_frame, wait_ticks) != pdPASS", __func__);
        return -1;
    }
    lvgl_port_lock(0);
    lvgl_port_unlock();
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
            bool decode_failed = false;
            while (in_frame.raw_data.len > 0) {
                ret = esp_h264_dec_process(dec, &in_frame, &out_frame);
                in_frame.raw_data.buffer += in_frame.consume;
                in_frame.raw_data.len -= in_frame.consume;
                if (ret == ESP_H264_ERR_OK) {
                    uint8_t *rgb565_buf = NULL;
                    if (xQueueReceive(s_rgb_free_queue, &rgb565_buf, portMAX_DELAY) != pdPASS) {
                        ESP_LOGE("h264", "Failed to get free RGB buffer");
                        decode_failed = true;
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
                        .is_end = false,
                    };
                    if (xQueueSend(s_rgb_ready_queue, &frame, portMAX_DELAY) != pdPASS) {
                        ESP_LOGE("h264", "Failed to enqueue RGB frame");
                        xQueueSend(s_rgb_free_queue, &rgb565_buf, portMAX_DELAY);
                        decode_failed = true;
                        break;
                    }
                } else {
                    ESP_LOGE("h264", "decode failed. line %d \n", __LINE__);
                    decode_failed = true;
                    break;
                }
            }
            rgb565_frame_t end_frame = {
                .buf = NULL,
                .len = 0,
                .is_end = true,
            };
            if (s_rgb_ready_queue != NULL) {
                if (xQueueSend(s_rgb_ready_queue, &end_frame, portMAX_DELAY) != pdPASS) {
                    ESP_LOGE("h264", "Failed to enqueue end frame");
                    if (s_playback_event_group != NULL) {
                        xEventGroupSetBits(s_playback_event_group, PLAYBACK_DONE_BIT);
                    }
                }
            } else if (s_playback_event_group != NULL) {
                xEventGroupSetBits(s_playback_event_group, PLAYBACK_DONE_BIT);
            }
            if (decode_failed && s_playback_event_group != NULL) {
                xEventGroupSetBits(s_playback_event_group, PLAYBACK_DONE_BIT);
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
    TickType_t last_wake_time = 0;
    bool has_last_wake_time = false;
    while (1) {
        if (s_rgb_ready_queue == NULL || s_rgb_free_queue == NULL) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (xQueueReceive(s_rgb_ready_queue, &frame, portMAX_DELAY) == pdPASS) {
            if (frame.is_end) {
                has_last_wake_time = false;
                if (s_playback_event_group != NULL) {
                    xEventGroupSetBits(s_playback_event_group, PLAYBACK_DONE_BIT);
                }
                if (s_done_callback != NULL) {
                    s_done_callback(s_done_context);
                }
                continue;
            }
            if (s_callback != NULL) {
                s_callback(frame.buf, frame.len, s_context);
            }
            xQueueSend(s_rgb_free_queue, &frame.buf, portMAX_DELAY);
            TickType_t interval = s_frame_interval_ticks;
            if (interval > 0) {
                if (!has_last_wake_time) {
                    last_wake_time = xTaskGetTickCount();
                    has_last_wake_time = true;
                }
                vTaskDelayUntil(&last_wake_time, interval);
            } else {
                has_last_wake_time = false;
            }
        }
    }
}

void my_h264_set_fps(uint32_t fps)
{
    s_target_fps = fps;
    s_frame_interval_ticks = fps_to_ticks(fps);
}

int my_h264_wait_done(uint32_t timeout_ms)
{
    if (s_playback_event_group == NULL) {
        return -1;
    }
    TickType_t wait_ticks = wait_timeout_to_ticks(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_playback_event_group,
                                           PLAYBACK_DONE_BIT,
                                           pdFALSE,
                                           pdTRUE,
                                           wait_ticks);
    return (bits & PLAYBACK_DONE_BIT) ? 0 : -1;
}
