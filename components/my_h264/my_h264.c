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

#include "esp_lvgl_port.h"
#include "my_ui_canvas.h"
#include "my_utils.h"

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
extern const uint8_t _binary_cat_hum_h264_start[];
extern const uint8_t _binary_cat_hum_h264_end[];
extern const uint8_t _binary_swan_hum_h264_start[];
extern const uint8_t _binary_swan_hum_h264_end[];
extern const uint8_t _binary_bird_hum_h264_start[];
extern const uint8_t _binary_bird_hum_h264_end[];
extern const uint8_t _binary_fish_hum_h264_start[];
extern const uint8_t _binary_fish_hum_h264_end[];
extern const uint8_t _binary_fox_hum_h264_start[];
extern const uint8_t _binary_fox_hum_h264_end[];
extern const uint8_t _binary_cat_static_h264_start[];
extern const uint8_t _binary_cat_static_h264_end[];
extern const uint8_t _binary_fox__static_h264_start[];
extern const uint8_t _binary_fox__static_h264_end[];
extern const uint8_t _binary_bird__static_h264_start[];
extern const uint8_t _binary_bird__static_h264_end[];
extern const uint8_t _binary_fish__static_h264_start[];
extern const uint8_t _binary_fish__static_h264_end[];
extern const uint8_t _binary_swan__static_h264_start[];
extern const uint8_t _binary_swan__static_h264_end[];

static QueueHandle_t h264_queue = NULL;
static QueueHandle_t s_rgb_free_queue = NULL;
static my_h264_callback_t s_callback = NULL;
static void *s_context = NULL;
static my_h264_done_callback_t s_done_callback = NULL;
static void *s_done_context = NULL;
static volatile bool s_abort = false;
static TaskHandle_t s_decode_task_handle = NULL;


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
static void h264_decode_playback_thread(void *arg);
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

#define H264_ANIM_COUNT (16)
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
    {_binary_brand_motion2_h264_start, _binary_brand_motion2_h264_end},      // 0: MY_H264_ANIM_BRAND_MOTION2
    {_binary_fail2_h264_start, _binary_fail2_h264_end},                    // 1: MY_H264_ANIM_FAIL2
    {_binary_go_up_h264_start, _binary_go_up_h264_end},                    // 2: MY_H264_ANIM_GO_UP
    {_binary_human_recognized_h264_start, _binary_human_recognized_h264_end}, // 3: MY_H264_ANIM_HUMAN_RECOGNIZED
    {_binary_processing_h264_start, _binary_processing_h264_end},           // 4: MY_H264_ANIM_PROCESSING
    {_binary_success2_h264_start, _binary_success2_h264_end},               // 5: MY_H264_ANIM_SUCCESS2
    {_binary_cat_hum_h264_start, _binary_cat_hum_h264_end},              // 6: MY_H264_ANIM_CAT
    {_binary_swan_hum_h264_start, _binary_swan_hum_h264_end},              // 7: MY_H264_ANIM_SWAN
    {_binary_bird_hum_h264_start, _binary_bird_hum_h264_end},              // 8: MY_H264_ANIM_BIRD
    {_binary_fish_hum_h264_start, _binary_fish_hum_h264_end},              // 9: MY_H264_ANIM_FISH
    {_binary_fox_hum_h264_start, _binary_fox_hum_h264_end},                // 10: MY_H264_ANIM_FOX
    {_binary_cat_static_h264_start, _binary_cat_static_h264_end},          // 11: MY_H264_ANIM_CAT_STATIC
    {_binary_fox__static_h264_start, _binary_fox__static_h264_end},        // 12: MY_H264_ANIM_FOX_STATIC
    {_binary_bird__static_h264_start, _binary_bird__static_h264_end},      // 13: MY_H264_ANIM_BIRD_STATIC
    {_binary_fish__static_h264_start, _binary_fish__static_h264_end},      // 14: MY_H264_ANIM_FISH_STATIC
    {_binary_swan__static_h264_start, _binary_swan__static_h264_end},       // 15: MY_H264_ANIM_SWAN_STATIC
};

void my_h264_init(my_h264_callback_t callback, void *context, my_h264_done_callback_t done_callback, void *done_context)
{
    s_callback = callback;
    s_context = context;
    s_done_callback = done_callback;
    s_done_context = done_context;
    h264_queue = xQueueCreate(1, sizeof(esp_h264_dec_in_frame_t));
    s_rgb_free_queue = xQueueCreate(RGB565_BUFFER_COUNT, sizeof(uint8_t *));
    if (h264_queue == NULL || s_rgb_free_queue == NULL) {
        ESP_LOGE("h264", "Failed to create queues");
        return;
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
    if (my_task_create_pinned_psram(h264_decode_playback_thread, "h264_decode_playback", 1024 * 12, NULL, 5, &s_decode_task_handle, 1) != ESP_OK) {
        ESP_LOGE("h264", "Failed to create decode playback task");
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

    my_h264_stop();

    len = anim_data[animation].end - anim_data[animation].start;
    if (len == 0 || len > s_h264_shared_buf_size) {
        ESP_LOGE("h264", "%s invalid animation size: %zu (max: %zu)", __func__, len, s_h264_shared_buf_size);
        return -1;
    }

    // 每次播放都从 flash 复制到共享缓冲区
    memcpy(s_h264_shared_buf, anim_data[animation].start, len);
    start = s_h264_shared_buf;
    
    memset(&in_frame, 0, sizeof(in_frame));
    in_frame.raw_data.buffer = start;
    in_frame.raw_data.len = len;
    TickType_t wait_ticks = wait_timeout_to_ticks(timeout_ms);
    if (xQueueSend(h264_queue, &in_frame, wait_ticks) != pdPASS) {
        ESP_LOGE("h264", "%s xQueueSend(h264_queue, &in_frame, wait_ticks) != pdPASS", __func__);
        return -1;
    }
    my_ui_canvas_enter();
    return 0;
}

static void h264_decode_playback_thread(void *arg) {
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

    if (s_rgb_free_queue == NULL) {
        ESP_LOGE("h264", "RGB free queue is not ready");
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
    TickType_t last_wake_time = 0;
    bool has_last_wake_time = false;
    bool local_abort = false;
    
    while (1) {
        if (xQueueReceive(h264_queue, &in_frame, portMAX_DELAY) == pdPASS) {
            local_abort = false;
            while (in_frame.raw_data.len > 0 && !s_abort) {
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
                    
                    // Check abort before playback
                    if (s_abort) {
                        local_abort = true;
                        xQueueSend(s_rgb_free_queue, &rgb565_buf, portMAX_DELAY);
                        break;
                    }
                    
                    // Playback the frame immediately
                    if (s_callback != NULL) {
                        s_callback(rgb565_buf, s_rgb_frame_bytes, s_context);
                    }
                    
                    // Return buffer to free queue
                    xQueueSend(s_rgb_free_queue, &rgb565_buf, portMAX_DELAY);
                    
                    // Frame rate control
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
                } else {
                    ESP_LOGE("h264", "decode failed. line %d \n", __LINE__);
                    break;
                }
            }
            
            // End of animation
            has_last_wake_time = false;
            if (s_done_callback != NULL && !local_abort && !s_abort) {
                s_done_callback(s_done_context);
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

    // 优化：使用指针而非索引，减少计算
    for (int i = 0; i < dst_height; ++i) {
        const unsigned char *y_row = yData + i * src_width;
        const int uv_row_idx = (i >> 1) * half_src_width;
        const unsigned char *u_row = uData + uv_row_idx;
        const unsigned char *v_row = vData + uv_row_idx;
        uint16_t *dst_row = (uint16_t *)(dst + i * dst_width * 2);

        // 优化：展开循环，一次处理2个像素（共享UV）
        int j = 0;
        for (; j < dst_width - 1; j += 2) {
            const int uv_idx = j >> 1;
            const int u_val = u_row[uv_idx];
            const int v_val = v_row[uv_idx];

            // 预计算UV查找表值，减少重复访问
            const int32_t u_b = s_u_b_table[u_val];
            const int32_t u_g = s_u_g_table[u_val];
            const int32_t v_r = s_v_r_table[v_val];
            const int32_t v_g = s_v_g_table[v_val];

            // 处理第一个像素
            {
                const int y_idx = j;
                const int32_t y_comp = s_y_table[y_row[y_idx]];
                const int r_val = (y_comp + v_r + 128) >> 8;
                const int g_val = (y_comp + u_g + v_g + 128) >> 8;
                const int b_val = (y_comp + u_b + 128) >> 8;

                const uint8_t r = s_clip_table[r_val + CLIP_TABLE_OFFSET];
                const uint8_t g = s_clip_table[g_val + CLIP_TABLE_OFFSET];
                const uint8_t b = s_clip_table[b_val + CLIP_TABLE_OFFSET];

                dst_row[j] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            }

            // 处理第二个像素（共享UV）
            {
                const int y_idx = j + 1;
                const int32_t y_comp = s_y_table[y_row[y_idx]];
                const int r_val = (y_comp + v_r + 128) >> 8;
                const int g_val = (y_comp + u_g + v_g + 128) >> 8;
                const int b_val = (y_comp + u_b + 128) >> 8;

                const uint8_t r = s_clip_table[r_val + CLIP_TABLE_OFFSET];
                const uint8_t g = s_clip_table[g_val + CLIP_TABLE_OFFSET];
                const uint8_t b = s_clip_table[b_val + CLIP_TABLE_OFFSET];

                dst_row[j + 1] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            }
        }

        // 处理剩余像素
        for (; j < dst_width; ++j) {
            const int uv_idx = j >> 1;
            const int32_t y_comp = s_y_table[y_row[j]];
            const int32_t u_b = s_u_b_table[u_row[uv_idx]];
            const int32_t u_g = s_u_g_table[u_row[uv_idx]];
            const int32_t v_r = s_v_r_table[v_row[uv_idx]];
            const int32_t v_g = s_v_g_table[v_row[uv_idx]];

            const int r_val = (y_comp + v_r + 128) >> 8;
            const int g_val = (y_comp + u_g + v_g + 128) >> 8;
            const int b_val = (y_comp + u_b + 128) >> 8;

            const uint8_t r = s_clip_table[r_val + CLIP_TABLE_OFFSET];
            const uint8_t g = s_clip_table[g_val + CLIP_TABLE_OFFSET];
            const uint8_t b = s_clip_table[b_val + CLIP_TABLE_OFFSET];

            dst_row[j] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
}


void my_h264_stop(void)
{
    if (h264_queue == NULL) {
        return;
    }

    // Check if already done: queue is empty
    if (uxQueueMessagesWaiting(h264_queue) == 0) {
        return;
    }

    s_abort = true;

    // Drain h264_queue to stop decode thread from starting new segments
    esp_h264_dec_in_frame_t in_frame;
    while (xQueueReceive(h264_queue, &in_frame, 0) == pdPASS) {
        // Data is in s_h264_shared_buf, no need to free
    }

    // Wait for decode thread to finish processing (queue empty means it's waiting)
    TickType_t timeout = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
    while (xTaskGetTickCount() < timeout) {
        if (uxQueueMessagesWaiting(h264_queue) == 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    s_abort = false;
}

void my_h264_set_fps(uint32_t fps)
{
    s_target_fps = fps;
    s_frame_interval_ticks = fps_to_ticks(fps);
}

int my_h264_wait_done(uint32_t timeout_ms)
{
    if (h264_queue == NULL) {
        return -1;
    }
    
    TickType_t wait_ticks = wait_timeout_to_ticks(timeout_ms);
    TickType_t start_ticks = xTaskGetTickCount();
    
    while (xTaskGetTickCount() - start_ticks < wait_ticks) {
        if (uxQueueMessagesWaiting(h264_queue) == 0) {
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    return -1;
}
