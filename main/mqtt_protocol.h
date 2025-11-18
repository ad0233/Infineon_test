#pragma once

#include "my_lidar.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 发布雷达数据到 MQTT
// 自动获取时间戳、转换为 JSON、发布并释放内存
// 返回 0 成功，-1 失败
int mqtt_publish_radar_data(const radar_latest_data_t *data, const char *topic);

#ifdef __cplusplus
}
#endif

