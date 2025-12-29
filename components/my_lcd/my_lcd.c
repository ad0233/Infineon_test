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
#include <stdbool.h>
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"

#include "ui.h"

#include "freertos/semphr.h"
#include "freertos/task.h"  // 添加这个头文件

#include "my_ui_canvas.h"
#include "board_pins_config.h"

#define TAG __FILE__

#define LCD_HOST            (SPI2_HOST)

static board_lcd_pin_t lcd_pins;

#define EXAMPLE_LCD_H_RES           (360)
#define EXAMPLE_LCD_V_RES           (360)
#define EXAMPLE_LCD_BIT_PER_PIXEL   (16)
#define LCD_BUFFER_SIZE (EXAMPLE_LCD_H_RES * 20)

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

void bsp_lcd_init(void);
void bsp_lcd_bl_init(void);

int my_lcd_init() {
    int ram_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    int ram_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    bsp_lcd_init();
    bsp_lcd_bl_init();
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_affinity = 1;
    lvgl_port_init(&lvgl_cfg);

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_BUFFER_SIZE,
        .double_buffer = 0,
        .hres = EXAMPLE_LCD_H_RES,
        .vres = EXAMPLE_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation = {
            .swap_xy = false,   // 交换X和Y轴
            .mirror_x = false,
            .mirror_y = false,  // 镜像Y轴实现逆时针90°旋转
        },
        .flags = {
            .swap_bytes = true,
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = true,
        }
    };
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    
    // 设置 flush_wait_cb，避免忙等待阻塞 IDLE 任务
    lv_display_set_rotation(lvgl_disp, LV_DISPLAY_ROTATION_90);

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
    // 获取LCD引脚配置
    ESP_ERROR_CHECK(get_lcd_pins(&lcd_pins));
    ESP_LOGI(TAG, "Initialize QSPI bus");
    const spi_bus_config_t bus_config = ST77916_PANEL_BUS_QSPI_CONFIG(lcd_pins.sck,
                                                                                 lcd_pins.da0,
                                                                                 lcd_pins.da1,
                                                                                 lcd_pins.da2,
                                                                                 lcd_pins.da3,
                                                                                 LCD_BUFFER_SIZE * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    
    esp_lcd_panel_io_spi_config_t io_config = ST77916_PANEL_IO_QSPI_CONFIG(lcd_pins.cs, NULL, NULL);
    io_config.trans_queue_depth = 2;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install ST77916 panel driver");
    
    st77916_vendor_config_t vendor_config = {
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = (lcd_pins.reset == -1) ? GPIO_NUM_NC : lcd_pins.reset,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,     // Implemented by LCD command `36h`
        .bits_per_pixel = EXAMPLE_LCD_BIT_PER_PIXEL,    // Implemented by LCD command `3Ah` (16/18)
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(io_handle, &panel_config, &panel_handle));
    
    // 复位LCD（如果reset_gpio_num为-1，驱动会自动使用软件复位）
    if (lcd_pins.reset == -1) {
        ESP_LOGI(TAG, "Using software reset (reset_gpio_num = -1)");
    }
    esp_lcd_panel_reset(panel_handle);  // 驱动会自动判断使用硬件或软件复位
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
        .gpio_num = lcd_pins.bl_pwm,
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