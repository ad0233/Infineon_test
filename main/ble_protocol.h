#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化蓝牙协议解析
 * 
 * 创建接收流缓冲区，注册 BLE 接收回调，启动解析任务和发送任务
 * 
 * @return 0 成功，-1 失败
 */
int ble_protocol_init();

#ifdef __cplusplus
}
#endif

