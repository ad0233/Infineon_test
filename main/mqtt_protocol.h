#pragma once

#include "my_lidar.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 协议类型枚举
typedef enum {
    PROTOCOL_TYPE_MQTT = 0,
    PROTOCOL_TYPE_BLE = 1
} protocol_type_t;

// 发布雷达数据到 MQTT 或蓝牙
// @param protocol 协议类型（MQTT 或 BLE）
// @param data 雷达数据（不可变引用）
// @param topic 主题（两种协议都需要）
// 自动获取时间戳、转换为 JSON、发布并释放内存
// 蓝牙协议会将 JSON 包装为 {"topic":"...","data":{...}} 格式
// 返回 0 成功，-1 失败
int protocol_publish_radar_data(protocol_type_t protocol, const radar_latest_data_t *data, const char *topic);

#ifdef __cplusplus
}
#endif

