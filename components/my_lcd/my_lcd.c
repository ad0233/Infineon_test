#include "my_lcd.h"

#include "lvgl.h"
#include "lv_examples.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_lcd_st77916.h"
#include "esp_lvgl_port.h"

#include <driver/gpio.h>
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"

#include "ui.h"

#include "freertos/semphr.h"
#include "freertos/task.h"  // 添加这个头文件

#include "my_ui_canvas.h"

#define TAG __FILE__

#define LCD_BL_PWM GPIO_NUM_8
#define LCD_RESET GPIO_NUM_15

#define LCD_HOST            (SPI2_HOST)
#define LCD_CS GPIO_NUM_16
#define LCD_SCK GPIO_NUM_4
#define LCD_DA0 GPIO_NUM_6
#define LCD_DA1 GPIO_NUM_14
#define LCD_DA2 GPIO_NUM_9
#define LCD_DA3 GPIO_NUM_2

#define EXAMPLE_LCD_H_RES           (360)
#define EXAMPLE_LCD_V_RES           (360)
#define EXAMPLE_LCD_BIT_PER_PIXEL   (16)

// 亮度映射表（10%, 20%, 30%, 40%, 50%, 60%, 70%, 80%, 90%, 100%）
static const uint16_t bl_map[10] = {
    204,  // 10%
    210,  // 20%
    217,  // 30%
    223,  // 40%
    230,  // 50%
    237,  // 60%
    243,  // 70%
    250,  // 80%
    256,  // 90%
    272   // 100%
};

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

static lv_disp_t *lvgl_disp = NULL;

// 添加 flush_wait_cb 回调，避免忙等待阻塞 IDLE 任务
static void lcd_flush_wait_cb(lv_display_t *disp)
{
    // 使用 vTaskDelay 让出 CPU，避免忙等待
    // 等待传输完成（通常 SPI 传输很快，但需要给硬件时间）
    vTaskDelay(pdMS_TO_TICKS(1));
}

void bsp_lcd_init(void);
void bsp_lcd_bl_init(void);

int my_lcd_init() {
    int ram_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    int ram_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    bsp_lcd_init();
    bsp_lcd_bl_init();
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES / 20,
        .double_buffer = 1,
        .hres = EXAMPLE_LCD_H_RES,
        .vres = EXAMPLE_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation = {
            .swap_xy = true,   // 交换X和Y轴
            .mirror_x = false,
            .mirror_y = true,  // 镜像Y轴实现逆时针90°旋转
        },
        .flags = {
            .swap_bytes = true,
            .buff_dma = true,
            .buff_spiram = true,
        }
    };
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    
    // 设置 flush_wait_cb，避免忙等待阻塞 IDLE 任务
    lv_display_set_flush_wait_cb(lvgl_disp, lcd_flush_wait_cb);

    lvgl_port_lock(0);
    ui_init();
    lv_disp_load_scr(ui_Boot);
    my_ui_canvas_init();
    lvgl_port_unlock();
    bsp_lcd_bl_set(100);

    ESP_LOGW("MEM", "my_lcd_init consume ram: %d bytes, dma: %d bytes",
        ram_internal - heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        ram_dma - heap_caps_get_free_size(MALLOC_CAP_DMA));
    return -1;
}

void bsp_lcd_init(void)
{
    ESP_LOGI(TAG, "Initialize QSPI bus");
    const spi_bus_config_t bus_config = ST77916_PANEL_BUS_QSPI_CONFIG(LCD_SCK,
                                                                                 LCD_DA0,
                                                                                 LCD_DA1,
                                                                                 LCD_DA2,
                                                                                 LCD_DA3,
                                                                                 EXAMPLE_LCD_H_RES * 80 * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    
    const esp_lcd_panel_io_spi_config_t io_config = ST77916_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install ST77916 panel driver");
    
    st77916_vendor_config_t vendor_config = {
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,     // Implemented by LCD command `36h`
        .bits_per_pixel = EXAMPLE_LCD_BIT_PER_PIXEL,    // Implemented by LCD command `3Ah` (16/18)
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(io_handle, &panel_config, &panel_handle));

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    
    esp_lcd_panel_disp_on_off(panel_handle, true);
}

void bsp_lcd_bl_set(int percent)
{
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;

    int index = (percent - 10) / 10;
    uint16_t duty = bl_map[index];

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void bsp_lcd_bl_off(void)
{
    bsp_lcd_bl_set(0);
}

void bsp_lcd_bl_on(void)
{
    bsp_lcd_bl_set(100);
}

void bsp_lcd_bl_init(void)
{
    const ledc_channel_config_t LCD_backlight_channel = {
        .gpio_num = LCD_BL_PWM,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };

    const ledc_timer_config_t LCD_backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };

    ledc_timer_config(&LCD_backlight_timer);
    ledc_channel_config(&LCD_backlight_channel);
}

#include "esp_heap_caps.h"
#include "string.h"


// LVGL核心内存管理函数 - 直接使用ESP-IDF内存管理
void* lv_malloc_core(size_t size)
{
    // 优先使用PSRAM，如果不够再用内部RAM
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (ptr == NULL) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    }
    return ptr;
}

void lv_free_core(void* ptr)
{
    if (ptr != NULL) {
        heap_caps_free(ptr);
    }
}

void* lv_realloc_core(void* ptr, size_t new_size)
{
    if (new_size == 0) {
        lv_free_core(ptr);
        return NULL;
    }
    
    if (ptr == NULL) {
        return lv_malloc_core(new_size);
    }
    
    // 尝试原地扩展
    void* new_ptr = heap_caps_realloc(ptr, new_size, MALLOC_CAP_8BIT);
    if (new_ptr != NULL) {
        return new_ptr;
    }
    
    // 如果原地扩展失败，分配新内存并复制数据
    void* new_mem = lv_malloc_core(new_size);
    if (new_mem != NULL) {
        // 由于无法获取原内存大小，使用新大小作为复制大小
        // 这可能会导致数据截断，但在大多数情况下是安全的
        memcpy(new_mem, ptr, new_size);
        lv_free_core(ptr);
    }
    
    return new_mem;
}

void lv_mem_init(void)
{
    ESP_LOGI("LVGL_MEM", "LVGL内存初始化");
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    ESP_LOGI("LVGL_MEM", "内存状态: 内部RAM=%d bytes, PSRAM=%d bytes", free_internal, free_psram);
}

void lv_mem_deinit(void)
{
    ESP_LOGI("LVGL_MEM", "LVGL内存反初始化");
}

// 修正函数签名 - 需要参数
void lv_mem_monitor_core(lv_mem_monitor_t * mon_p)
{
    if (mon_p == NULL) return;
    
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    size_t min_free_internal = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    
    // 填充监控结构
    mon_p->total_size = free_internal + free_psram + (free_internal - min_free_internal);
    mon_p->free_size = free_internal + free_psram;
    mon_p->free_biggest_size = (free_internal > free_psram) ? free_internal : free_psram;
    mon_p->used_cnt = 0;
    mon_p->max_used = free_internal - min_free_internal;
    mon_p->used_pct = (uint8_t)((mon_p->max_used * 100) / mon_p->total_size);
    mon_p->frag_pct = 0;
    
    ESP_LOGI("LVGL_MEM", "内存监控: 内部RAM=%d bytes, PSRAM=%d bytes, 已用=%d bytes", 
             free_internal, free_psram, mon_p->max_used);
}
