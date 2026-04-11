#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 WS2812 RGB 灯（1 颗，GPIO20，RMT 驱动）
 * @return 0 成功，-1 失败
 */
int my_rgb_init(void);

/**
 * @brief 控制 RGB 灯
 * @param r 红 0-255
 * @param g 绿 0-255
 * @param b 蓝 0-255
 * @param brightness 亮度 0-100（0=关灯）
 */
void my_rgb_set(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);

#ifdef __cplusplus
}
#endif
