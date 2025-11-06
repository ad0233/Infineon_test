#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ble_data_recv_callback_t)(const uint8_t *data, uint16_t len, void *context);

void my_ble_init(void);
void my_ble_register_recv_callback(ble_data_recv_callback_t callback, void *context);
int my_ble_send_data(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

/**
 * @brief 获取蓝牙 MAC 地址（单例模式，返回静态字符串）
 * @param with_colon true=带冒号格式 "AA:BB:CC:DD:EE:FF", false=无冒号格式 "AABBCCDDEEFF"
 * @return MAC 地址字符串指针（静态缓冲区），失败返回 NULL
 * @note 返回的字符串指针有效期直到下次调用此函数，线程不安全
 */
const char *my_ble_get_mac(bool with_colon);

#ifdef __cplusplus
}
#endif