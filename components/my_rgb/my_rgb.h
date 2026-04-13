#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 WS2812 RGB 灯（1 颗，GPIO20，RMT 驱动）
 */
int my_rgb_init(void);

/**
 * @brief 设置颜色（存储到状态，如果当前开灯则立即应用）
 * @param r 红 0-255
 * @param g 绿 0-255
 * @param b 蓝 0-255
 * @param brightness 亮度 0-100
 */
void my_rgb_set_color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);

/**
 * @brief 开/关灯（使用当前存储的颜色）
 */
void my_rgb_enable(bool on);

#ifdef __cplusplus
}
#endif
