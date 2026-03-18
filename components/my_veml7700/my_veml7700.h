#pragma once

#include <stdint.h>

struct my_veml7700_impl;
typedef struct my_veml7700_impl *my_veml7700_handle_t;

#ifdef __cplusplus
extern "C" {
#endif

/** VEML7700 I2C 7-bit 地址 (固定 0x10) */
#define MY_VEML7700_I2C_ADDR_7BIT   (0x10)

/**
 * @brief 初始化 VEML7700 环境光传感器
 * @param self_out 输出句柄
 * @param i2c_bus  已初始化的 I2C master 总线句柄
 * @return 0 成功，非 0 失败
 */
int my_veml7700_init(my_veml7700_handle_t *self_out, void *i2c_bus);

/**
 * @brief 反初始化并释放句柄
 */
int my_veml7700_deinit(my_veml7700_handle_t self);

/**
 * @brief 读取环境光照度（阻塞，等待一个积分周期后读）
 * @param self      句柄
 * @param lux_out   输出照度 lx，可为 NULL
 * @param timeout_ms 超时 ms
 * @return 0 成功，非 0 失败
 */
int my_veml7700_read_block(my_veml7700_handle_t self, float *lux_out, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
