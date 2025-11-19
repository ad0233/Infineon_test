#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化蓝牙协议解析
 * 
 * 创建接收流缓冲区，注册 BLE 接收回调，初始化解析器
 * 
 * @return 0 成功，-1 失败
 */
int ble_protocol_init();

/**
 * @brief BLE解析flush函数，在主循环中定期调用
 * @details 非阻塞函数，从流缓冲区读取数据并解析命令
 *          需要在主循环中定期调用（建议10-50ms间隔）
 */
void ble_parse_flush(void);

/**
 * @brief 发送 BLE 响应数据
 * 
 * @param json_str JSON 字符串
 * @return 0 成功，-1 失败
 */
int ble_send_response(const char *json_str);

#ifdef __cplusplus
}
#endif

