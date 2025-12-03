#include "my_ui_canvas.h"
#include "lvgl.h"
#include <string.h>

#include "esp_lvgl_port.h"
#include "esp_heap_caps.h"

#include "esp_log.h"

#define TAG __FILE__

#define CANVAS_WIDTH  360
#define CANVAS_HEIGHT 360

static lv_obj_t * ui_CanvasScreen = NULL;
static lv_obj_t * ui_Canvas = NULL;
static uint8_t * canvas_buffer = NULL;

void my_ui_canvas_init(void)
{
    // 创建父容器作为屏幕
    ui_CanvasScreen = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_CanvasScreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_CanvasScreen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_CanvasScreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 在父容器中创建 canvas
    ui_Canvas = lv_canvas_create(ui_CanvasScreen);
    lv_obj_remove_flag(ui_Canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(ui_Canvas, CANVAS_WIDTH);
    lv_obj_set_height(ui_Canvas, CANVAS_HEIGHT);
    lv_obj_set_align(ui_Canvas, LV_ALIGN_CENTER);
    
    canvas_buffer = (uint8_t *)heap_caps_malloc(CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
    if (canvas_buffer != NULL) {
        lv_canvas_set_buffer(ui_Canvas, canvas_buffer, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_RGB565);
    } else {
        ESP_LOGE("my_ui_canvas", "canvas_buffer == NULL");
    }
}

void my_ui_canvas_update(const uint8_t *data, uint32_t length) {
    if (ui_Canvas == NULL || canvas_buffer == NULL || data == NULL) {
        ESP_LOGE("my_ui_canvas", "ui_Canvas == NULL || canvas_buffer == NULL || data == NULL");
        return;
    }
    
    lvgl_port_lock(0);
    
    // 从 h264 的 buffer 复制数据到 canvas 自己的 buffer
    size_t copy_len = length < (CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t)) ? length : (CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t));
    memcpy(canvas_buffer, data, copy_len);
    // lv_canvas_set_buffer(ui_Canvas, canvas_buffer, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_RGB565);
    lv_obj_invalidate(ui_Canvas);
    
    lvgl_port_unlock();
}

void my_ui_canvas_enter(void)
{
    if (ui_CanvasScreen == NULL || ui_Canvas == NULL) {
        ESP_LOGE("my_ui_canvas", "ui_CanvasScreen == NULL || ui_Canvas == NULL");
        return;
    }
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_CanvasScreen);
    lv_canvas_fill_bg(ui_Canvas, lv_color_hex(0x000000), 255);
    lvgl_port_unlock();
}