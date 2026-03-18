#pragma once

#include <stdint.h>

struct my_bmp580_impl;
typedef struct my_bmp580_impl *my_bmp580_handle_t;

#ifdef __cplusplus
extern "C" {
#endif

/** BMP580 I2C 7-bit 地址 (SDO 接 GND=0x46, 接 VDDIO=0x47) */
#define MY_BMP580_I2C_ADDR_7BIT   (0x46)

/**
 * @brief 初始化 BMP580 气压传感器
 * @param self_out 输出句柄
 * @param i2c_bus  已初始化的 I2C master 总线句柄
 * @return 0 成功，非 0 失败
 */
int my_bmp580_init(my_bmp580_handle_t *self_out, void *i2c_bus);

/**
 * @brief 反初始化并释放句柄
 */
int my_bmp580_deinit(my_bmp580_handle_t self);

/**
 * @brief 触发一次测量并读取气压与温度（阻塞）
 * @param self       句柄
 * @param pressure_pa 输出气压 Pa，可为 NULL
 * @param temp_c     输出温度 ℃，可为 NULL
 * @param timeout_ms 超时 ms
 * @return 0 成功，非 0 失败
 */
int my_bmp580_read_block(my_bmp580_handle_t self, float *pressure_pa, float *temp_c, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
