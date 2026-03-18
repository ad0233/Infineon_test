#pragma once

#include <stdint.h>

struct my_light_impl;
typedef struct my_light_impl *my_light_handle_t;

#ifdef __cplusplus
extern "C" {
#endif

/** BH1750 I2C 7-bit 地址 (ADDR 接 GND=0x23, 接 VCC=0x5C)；8-bit 写=0x46 读=0x47，驱动用 7-bit */
#define MY_LIGHT_I2C_ADDR_7BIT   (0x23)

/**
 * @brief 初始化 BH1750 环境光传感器
 * @param self_out 输出句柄
 * @param i2c_bus  已初始化的 I2C master 总线句柄 (如 bsp_i2c_get_bus_handle())
 * @return 0 成功，非 0 失败
 */
int my_light_init(my_light_handle_t *self_out, void *i2c_bus);

/**
 * @brief 反初始化并释放句柄
 */
int my_light_deinit(my_light_handle_t self);

/**
 * @brief 读取环境光照度（兼容接口，内部调用单次读）
 */
int my_light_read_block(my_light_handle_t self, float *lux_out, uint32_t timeout_ms);

/**
 * @brief 单次读：每次触发一次测量（约 180ms），读当前照度
 * @param self      句柄
 * @param lux_out   输出照度 lx，可为 NULL
 * @param timeout_ms 超时 ms
 * @return 0 成功，非 0 失败
 */
int my_light_read_oneshot(my_light_handle_t self, float *lux_out, uint32_t timeout_ms);

/**
 * @brief 启动连续测量模式（H 分辨率，约 120ms 后可用），之后用 my_light_read_continuous 读
 */
int my_light_start_continuous(my_light_handle_t self);

/**
 * @brief 连续读：直接读当前照度（需先调用 my_light_start_continuous）
 * @param self      句柄
 * @param lux_out   输出照度 lx，可为 NULL
 * @param timeout_ms 超时 ms
 * @return 0 成功，非 0 失败
 */
int my_light_read_continuous(my_light_handle_t self, float *lux_out, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
