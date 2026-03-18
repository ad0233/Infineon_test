#pragma once

#include <stdint.h>
#include <stdbool.h>

struct my_aht20_impl;
typedef struct my_aht20_impl *my_aht20_handle_t;

#ifdef __cplusplus
extern "C" {
#endif

/** I2C 7-bit 地址 (手册 0x70 为 8-bit 写地址) */
#define MY_AHT20_I2C_ADDR_7BIT   (0x38)

/**
 * @brief 初始化 AHT20
 * @param self_out 输出句柄
 * @param i2c_bus  已初始化的 I2C master 总线句柄 (如 bsp_i2c_get_bus_handle())
 * @return 0 成功，非 0 失败 (esp_err_t)
 */
int my_aht20_init(my_aht20_handle_t *self_out, void *i2c_bus);

/**
 * @brief 反初始化并释放句柄
 */
int my_aht20_deinit(my_aht20_handle_t self);

/**
 * @brief 触发一次测量并读取温湿度（阻塞，约 80ms）
 * @param self     句柄
 * @param temp_c   输出温度 ℃，可为 NULL
 * @param rh_pct   输出相对湿度 %，可为 NULL
 * @param timeout_ms 超时 ms
 * @return 0 成功，非 0 失败
 */
int my_aht20_read_block(my_aht20_handle_t self, float *temp_c, float *rh_pct, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
