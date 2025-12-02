#include "my_ui_lottie.h"
#include "lvgl.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

// 编译时检查：确保 LV_USE_LOTTIE 已启用
#if !defined(LV_USE_LOTTIE) || LV_USE_LOTTIE == 0
#error "LV_USE_LOTTIE must be enabled. Run 'idf.py menuconfig' -> Component config -> LVGL configuration -> Enable Lottie"
#endif

#if !defined(LV_USE_VECTOR_GRAPHIC) || LV_USE_VECTOR_GRAPHIC == 0
#error "LV_USE_VECTOR_GRAPHIC must be enabled. Run 'idf.py menuconfig' -> Component config -> LVGL configuration -> Enable Vector Graphic"
#endif

#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#define TAG "mu_ui_lottie"

#define LOTTIE_WIDTH  100
#define LOTTIE_HEIGHT 100

/*
 * LOTTIE 内存占用说明：
 * 
 * 1. 帧缓冲区（必需）：360x360x4 = 518,400 字节 ≈ 506 KB
 * 
 * 2. ThorVG 渲染临时内存（大量，动态分配）：
 *    - RLE（Run-Length Encoding）扫描线数据
 *    - 向量路径光栅化缓冲区
 *    - 图层合成临时缓冲区
 *    - 估计：帧缓冲的 5-10 倍（约 2.5-5 MB）
 *    - 注意：这些通过 realloc 动态分配，必须使用 SPIRAM
 * 
 * 3. 总内存需求：约 3-6 MB（取决于动画复杂度）
 * 
 * 关键配置（必须）：
 *   CONFIG_SPIRAM=y
 *   CONFIG_SPIRAM_USE_MALLOC=y  (让所有 malloc/realloc 使用 SPIRAM)
 * 
 * 如果内存不足，建议：
 *   1. 减小动画尺寸（如 240x240 或 180x180）
 *   2. 简化动画内容
 *   3. 在初始化前释放其他内存
 */

// 访问嵌入的 JSON 文件数据
extern const uint8_t lunawake_json_start[] asm("_binary_lunawake_json_start");
extern const uint8_t lunawake_json_end[] asm("_binary_lunawake_json_end");

static lv_obj_t * ui_Lottie = NULL;
static uint8_t * lottie_buffer = NULL;  // ARGB8888 格式的帧缓冲区

void my_ui_lottie_init(void)
{
    if (ui_Lottie != NULL) {
        ESP_LOGW(TAG, "Lottie already initialized");
        return;
    }

    lvgl_port_lock(0);

    // 计算 JSON 数据长度
    size_t json_len = lunawake_json_end - lunawake_json_start;
    if (json_len == 0) {
        ESP_LOGE(TAG, "Embedded JSON data is empty");
        lvgl_port_unlock();
        return;
    }

    ESP_LOGI(TAG, "Creating lottie animation, JSON size: %zu bytes", json_len);

    // 检查可用内存（lottie 需要大量内存用于渲染）
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    size_t required_memory = LOTTIE_WIDTH * LOTTIE_HEIGHT * 4; // ARGB8888 格式需要 4 字节/像素
    
    // 检查 SPIRAM 可用内存
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest_free_spiram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    
    ESP_LOGI(TAG, "Memory status:");
    ESP_LOGI(TAG, "  Free heap (total): %zu bytes", free_heap);
    ESP_LOGI(TAG, "  Min free heap: %zu bytes", min_free_heap);
    ESP_LOGI(TAG, "  Free SPIRAM: %zu bytes", free_spiram);
    ESP_LOGI(TAG, "  Largest free SPIRAM block: %zu bytes", largest_free_spiram);
    ESP_LOGI(TAG, "  Required for frame buffer: %zu bytes (%.2f KB)", 
             required_memory, required_memory / 1024.0f);
    
    // ThorVG 渲染时需要大量临时内存用于 RLE（扫描线编码）
    // 复杂动画可能需要帧缓冲的 5-10 倍
    size_t estimated_render_memory = required_memory * 8;  // 保守估计
    ESP_LOGI(TAG, "  Estimated total memory (frame + render): %zu bytes (%.2f MB)", 
             estimated_render_memory, estimated_render_memory / (1024.0f * 1024.0f));
    
    if (free_spiram == 0) {
        ESP_LOGE(TAG, "CRITICAL: SPIRAM not available!");
        ESP_LOGE(TAG, "LOTTIE requires SPIRAM. Enable CONFIG_SPIRAM=y and CONFIG_SPIRAM_USE_MALLOC=y");
        lvgl_port_unlock();
        return;
    }
    
    // 检查是否有足够的 SPIRAM
    if (free_spiram < estimated_render_memory) {
        ESP_LOGE(TAG, "CRITICAL: Insufficient SPIRAM!");
        ESP_LOGE(TAG, "  Free SPIRAM: %zu bytes (%.2f MB)", free_spiram, free_spiram / (1024.0f * 1024.0f));
        ESP_LOGE(TAG, "  Estimated need: %zu bytes (%.2f MB)", 
                 estimated_render_memory, estimated_render_memory / (1024.0f * 1024.0f));
        ESP_LOGE(TAG, "Solutions:");
        ESP_LOGE(TAG, "  1. Reduce animation size (e.g., 240x240 or 180x180)");
        ESP_LOGE(TAG, "  2. Simplify animation content (fewer layers/paths)");
        ESP_LOGE(TAG, "  3. Free other SPIRAM allocations before init");
        lvgl_port_unlock();
        return;
    }
    
    if (free_heap < estimated_render_memory) {
        ESP_LOGW(TAG, "Warning: Total free heap (%zu) < estimated need (%zu)", 
                 free_heap, estimated_render_memory);
        ESP_LOGW(TAG, "Ensure CONFIG_SPIRAM_USE_MALLOC=y so malloc uses SPIRAM");
    }
    
    // 分配帧缓冲区（使用 SPIRAM）
    if (lottie_buffer == NULL) {
        lottie_buffer = (uint8_t *)heap_caps_malloc(required_memory, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (lottie_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer from SPIRAM, trying default heap");
            lottie_buffer = (uint8_t *)malloc(required_memory);
            if (lottie_buffer == NULL) {
                ESP_LOGE(TAG, "Failed to allocate frame buffer");
                lvgl_port_unlock();
                return;
            }
        }
        ESP_LOGI(TAG, "Frame buffer allocated: %zu bytes", required_memory);
    }

    // 创建 lottie 对象
    ui_Lottie = lv_lottie_create(NULL);
    
    if (ui_Lottie == NULL) {
        ESP_LOGE(TAG, "Failed to create lottie object");
        if (lottie_buffer != NULL) {
            free(lottie_buffer);
            lottie_buffer = NULL;
        }
        lvgl_port_unlock();
        return;
    }

    // 设置缓冲区（ARGB8888 格式）
    lv_lottie_set_buffer(ui_Lottie, LOTTIE_WIDTH, LOTTIE_HEIGHT, lottie_buffer);
    
    // 设置 JSON 数据源
    lv_lottie_set_src_data(ui_Lottie, lunawake_json_start, json_len);

    // 设置样式：黑色背景
    lv_obj_set_style_bg_color(ui_Lottie, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Lottie, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ESP_LOGI(TAG, "Lottie animation created successfully");

    lvgl_port_unlock();
}

void my_ui_lottie_enter(void)
{
    if (ui_Lottie == NULL) {
        ESP_LOGE(TAG, "Lottie not initialized, call my_ui_lottie_init() first");
        return;
    }

    lvgl_port_lock(0);
    
    // 检查内存状态
    size_t free_heap = esp_get_free_heap_size();
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Free heap before loading: %zu bytes, Free SPIRAM: %zu bytes", 
             free_heap, free_spiram);
    
    lv_disp_load_scr(ui_Lottie);
    
    // lv_lottie 会自动开始播放，无需手动设置播放模式
    
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Lottie screen loaded");
}
