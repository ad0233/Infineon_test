#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

int my_lcd_init();
void bsp_lcd_bl_set(int brightness_percent);


#ifdef __cplusplus
}
#endif