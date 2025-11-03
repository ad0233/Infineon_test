#pragma once

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// 艾睿（Airtouch）雷达主动上报协议定义
// ============================================================================

// 主动上报协议帧头
#define AIRTOUCH_HEADER             0x5A

// 消息类型定义
#define AIRTOUCH_MSG_TYPE_BHR_DET   0x04  // 呼吸心率检测信息

// 呼吸心率检测信息结构体（TYPE=4载荷，8字节）
typedef struct {
    uint8_t det_result;   // 检测状态: 0x01/0x02/0x04=运动, 0x08=微动, 0x10=存在
    uint8_t br_val;       // 呼吸频率值
    uint8_t hr_val;       // 心率值
    uint8_t angle_val;    // 保留字段
    uint16_t range_val;   // 检测距离值，单位: mm（小端序）
    uint16_t padding;     // 保留字段（小端序）
} airtouch_bhr_det_info_t;

// 检测状态位定义
#define AIRTOUCH_DET_MOTION_BIT0    0x01  // 运动
#define AIRTOUCH_DET_MOTION_BIT1    0x02  // 运动
#define AIRTOUCH_DET_MOTION_BIT2    0x04  // 运动
#define AIRTOUCH_DET_MICRO_MOTION   0x08  // 微动
#define AIRTOUCH_DET_PRESENCE       0x10  // 存在

// 主动上报帧结构
// 格式: HEAD(0x5A) + LEN(1B) + PAYLOAD(LEN bytes) + CHECK(1B)
// 其中 PAYLOAD = MSG_TYPE(1B) + DATA(N bytes)
// 对于TYPE=4: PAYLOAD = 0x04 + BhrDetInfo(8B) = 9字节
#define AIRTOUCH_BHR_PAYLOAD_SIZE   9     // TYPE(1) + BhrDetInfo(8)
#define AIRTOUCH_BHR_TOTAL_SIZE     11    // HEAD(1) + LEN(1) + PAYLOAD(9) + CHECK(1)

#ifdef __cplusplus
extern "C" {
#endif

// 艾睿雷达初始化
void airtouch_init(void);

// 启动艾睿雷达接收任务（自动上报模式，无需发送命令）
void airtouch_start(void);

// 停止艾睿雷达
void airtouch_stop(void);

// 查询状态
bool airtouch_is_connected(void);

// 注意：回调函数和数据获取接口定义在 my_lidar.h 的统一接口中

#ifdef __cplusplus
}
#endif

