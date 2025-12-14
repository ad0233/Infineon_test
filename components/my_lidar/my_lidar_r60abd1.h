#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "my_lidar.h"  // 包含完整的类型定义

// ============================================================================
// R60ABD1 毫米波雷达协议定义（旧雷达）
// ============================================================================

#define RADAR_FRAME_HEADER_1    0x53
#define RADAR_FRAME_HEADER_2    0x59
#define RADAR_FRAME_TAIL_1      0x54
#define RADAR_FRAME_TAIL_2      0x43
#define RADAR_MAX_DATA_LENGTH   64  // 最大数据长度

// 控制字定义
#define RADAR_CTRL_SYSTEM       0x01  // 系统功能
#define RADAR_CTRL_PRODUCT      0x02  // 产品信息
#define RADAR_CTRL_OTA          0x03  // OTA升级
#define RADAR_CTRL_WORK_STATUS  0x05  // 工作状态
#define RADAR_CTRL_RANGE        0x07  // 雷达检测范围
#define RADAR_CTRL_HUMAN        0x80  // 人体存在
#define RADAR_CTRL_RESPIRATORY  0x81  // 呼吸监测
#define RADAR_CTRL_SLEEP        0x84  // 睡眠监测
#define RADAR_CTRL_HEART_RATE   0x85  // 心率监测

// 系统功能命令字
#define RADAR_CMD_HEARTBEAT_REPORT  0x01  // 心跳包上报
#define RADAR_CMD_HEARTBEAT_QUERY   0x80  // 心跳包查询
#define RADAR_CMD_RESET             0x02  // 模组复位

// 产品信息命令字
#define RADAR_CMD_PRODUCT_MODEL     0x01  // 产品型号
#define RADAR_CMD_PRODUCT_ID        0x02  // 产品ID
#define RADAR_CMD_HARDWARE_MODEL    0x03  // 硬件型号
#define RADAR_CMD_FIRMWARE_VERSION  0x04  // 固件版本

// 工作状态命令字
#define RADAR_CMD_INIT_COMPLETE     0x01  // 初始化完成
#define RADAR_CMD_INIT_QUERY        0x81  // 初始化查询

// 人体存在命令字 (主动上报)
#define RADAR_CMD_HUMAN_SWITCH      0x00  // 人体存在开关
#define RADAR_CMD_HUMAN_PRESENCE    0x01  // 存在信息主动上报
#define RADAR_CMD_HUMAN_MOTION      0x02  // 运动信息主动上报
#define RADAR_CMD_HUMAN_MOVEMENT    0x03  // 体动参数主动上报
#define RADAR_CMD_HUMAN_DISTANCE    0x04  // 人体距离主动上报
#define RADAR_CMD_HUMAN_POSITION    0x05  // 人体方位主动上报

// 人体存在查询命令字 (查询回复)
#define RADAR_CMD_HUMAN_QUERY_PRESENCE  0x81  // 查询存在信息回复

// 呼吸监测命令字 (控制字固定为0x81)
#define RADAR_CMD_RESPIRATORY_SWITCH    0x00  // 呼吸监测开关
#define RADAR_CMD_RESPIRATORY_INFO      0x01  // 呼吸信息
#define RADAR_CMD_RESPIRATORY_VALUE     0x02  // 呼吸数值
#define RADAR_CMD_RESPIRATORY_WAVEFORM  0x05  // 呼吸波形
#define RADAR_CMD_RESPIRATORY_SLOW      0x0B  // 低缓呼吸判读
#define RADAR_CMD_RESPIRATORY_UPLOAD    0x0C  // 呼吸波形上报开关

// 心率监测命令字
#define RADAR_CMD_HEART_RATE_SWITCH     0x00  // 心率监测开关
#define RADAR_CMD_HEART_RATE_VALUE      0x02  // 心率数值
#define RADAR_CMD_HEART_RATE_WAVEFORM   0x05  // 心率波形

// 睡眠监测命令字 (控制字固定为0x84)
// 主动上报和设置
#define RADAR_CMD_SLEEP_SWITCH          0x00  // 睡眠监测开关
#define RADAR_CMD_SLEEP_BED_STATUS      0x01  // 入床/离床状态主动上报
#define RADAR_CMD_SLEEP_STATUS          0x02  // 睡眠状态主动上报
#define RADAR_CMD_SLEEP_AWAKE_DURATION  0x03  // 清醒时长主动上报
#define RADAR_CMD_SLEEP_LIGHT_DURATION  0x04  // 浅睡时长主动上报
#define RADAR_CMD_SLEEP_DEEP_DURATION   0x05  // 深睡时长主动上报
#define RADAR_CMD_SLEEP_QUALITY_SCORE   0x06  // 睡眠质量评分主动上报
#define RADAR_CMD_SLEEP_COMPREHENSIVE   0x0C  // 睡眠综合状态主动上报
#define RADAR_CMD_SLEEP_QUALITY_ANALYSIS 0x0D // 睡眠质量分析主动上报
#define RADAR_CMD_SLEEP_ABNORMAL        0x0E  // 睡眠异常主动上报
#define RADAR_CMD_SLEEP_QUALITY_RATING  0x10  // 睡眠质量评级主动上报
#define RADAR_CMD_SLEEP_STRUGGLE_STATUS 0x11  // 异常挣扎状态主动上报
#define RADAR_CMD_SLEEP_NO_PERSON_STATUS 0x12 // 无人计时状态主动上报
#define RADAR_CMD_SLEEP_STRUGGLE_SWITCH 0x13  // 异常挣扎状态开关设置
#define RADAR_CMD_SLEEP_NO_PERSON_SWITCH 0x14 // 无人计时功能开关设置
#define RADAR_CMD_SLEEP_NO_PERSON_TIME  0x15  // 无人计时时长设置
#define RADAR_CMD_SLEEP_CUTOFF_TIME     0x16  // 睡眠截止时长设置
#define RADAR_CMD_SLEEP_STRUGGLE_SENSITIVITY 0x1A // 挣扎状态判读设置

// 睡眠监测查询命令字 (查询回复)
#define RADAR_CMD_SLEEP_QUERY_SWITCH    0x80  // 查询睡眠监测开关
#define RADAR_CMD_SLEEP_QUERY_BED_STATUS 0x81 // 入床/离床状态查询
#define RADAR_CMD_SLEEP_QUERY_STATUS    0x82  // 睡眠状态查询
#define RADAR_CMD_SLEEP_QUERY_AWAKE_DURATION 0x83 // 清醒时长查询
#define RADAR_CMD_SLEEP_QUERY_LIGHT_DURATION 0x84 // 浅睡时长查询
#define RADAR_CMD_SLEEP_QUERY_DEEP_DURATION  0x85 // 深睡时长查询
#define RADAR_CMD_SLEEP_QUERY_QUALITY_SCORE  0x86 // 睡眠质量评分查询
#define RADAR_CMD_SLEEP_QUERY_COMPREHENSIVE  0x8D // 睡眠综合状态查询
#define RADAR_CMD_SLEEP_QUERY_ABNORMAL       0x8E // 睡眠异常查询
#define RADAR_CMD_SLEEP_QUERY_STATISTICS     0x8F // 睡眠统计查询
#define RADAR_CMD_SLEEP_QUERY_QUALITY_RATING 0x90 // 睡眠质量评级查询
#define RADAR_CMD_SLEEP_QUERY_STRUGGLE_STATUS 0x91 // 异常挣扎状态查询
#define RADAR_CMD_SLEEP_QUERY_NO_PERSON_STATUS 0x92 // 无人计时状态查询
#define RADAR_CMD_SLEEP_QUERY_STRUGGLE_SWITCH 0x93 // 异常挣扎状态开关查询
#define RADAR_CMD_SLEEP_QUERY_NO_PERSON_SWITCH 0x94 // 无人计时功能开关查询
#define RADAR_CMD_SLEEP_QUERY_NO_PERSON_TIME  0x95 // 无人计时时长查询
#define RADAR_CMD_SLEEP_QUERY_CUTOFF_TIME     0x96 // 睡眠截止时间查询
#define RADAR_CMD_SLEEP_QUERY_STRUGGLE_SENSITIVITY 0x9A // 挣扎状态判读查询

// 雷达数据包结构
typedef struct {
    uint8_t header[2];           // 帧头 0x53 0x59
    uint8_t control;             // 控制字
    uint8_t command;             // 命令字
    uint16_t length;             // 数据长度
    uint8_t data[RADAR_MAX_DATA_LENGTH]; // 数据内容
    uint8_t checksum;            // 校验和
    uint8_t tail[2];             // 帧尾 0x54 0x43
} r60abd1_packet_t;

#ifdef __cplusplus
extern "C" {
#endif

// R60ABD1雷达初始化
void r60abd1_init(void);

// 启动R60ABD1雷达接收任务
void r60abd1_start(void);

// 停止R60ABD1雷达
void r60abd1_stop(void);

// 刷新函数（由主线程定时调用）
void r60abd1_flush(void);

// 发送命令
bool r60abd1_send_command(uint8_t control, uint8_t command, const uint8_t *data, uint16_t length);

// 查询命令
bool r60abd1_query_product_info(void);
bool r60abd1_query_human_presence(void);
bool r60abd1_query_human_motion(void);
bool r60abd1_set_human_switch(bool enable);
bool r60abd1_set_respiratory_switch(bool enable);
bool r60abd1_set_heart_rate_switch(bool enable);

// 回调函数设置
void r60abd1_set_human_presence_callback(radar_human_callback_t callback);
void r60abd1_set_human_movement_callback(radar_human_callback_t callback);
void r60abd1_set_respiratory_callback(radar_respiratory_callback_t callback);
void r60abd1_set_heart_rate_callback(radar_heart_rate_callback_t callback);

// 状态查询
bool r60abd1_is_connected(void);

// 数据获取
bool r60abd1_get_human_data(radar_human_data_t *data);
bool r60abd1_get_respiratory_data(radar_respiratory_data_t *data);
bool r60abd1_get_heart_rate_data(radar_heart_rate_data_t *data);
bool r60abd1_get_product_info(radar_product_info_t *info);
bool r60abd1_get_latest_data(radar_latest_data_t *data);

#ifdef __cplusplus
}
#endif

