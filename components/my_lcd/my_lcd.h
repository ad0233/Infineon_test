#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

int my_lcd_init();
void bsp_lcd_bl_set(int brightness_percent);
esp_err_t my_lcd_draw_rgb565(const uint16_t *frame, size_t pixel_count);
esp_err_t my_lvgl_force_refresh(void);

#ifdef __cplusplus
}
#endif