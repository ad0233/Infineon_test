#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ble_data_recv_callback_t)(const uint8_t *data, uint16_t len);

void my_ble_init(void);
void my_ble_register_recv_callback(ble_data_recv_callback_t callback);
int my_ble_send_data(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

/**
 * @brief 获取蓝牙 MAC 地址
 * @param mac_str 输出 MAC 地址字符串，格式如 "AA:BB:CC:DD:EE:FF"，需要至少18字节
 * @return 0 成功, -1 失败
 */
int my_ble_get_mac(char *mac_str);

#ifdef __cplusplus
}
#endif