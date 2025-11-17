#pragma once

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// 雷达协议选择
// ============================================================================
// 选择使用的雷达类型：
// - 注释下面的宏使用R60ABD1雷达（旧雷达）
// - 启用下面的宏使用艾睿雷达（新雷达，BhrDetInfo自动上报）
// #define USE_AIRTOUCH_RADAR

// ============================================================================
// 通用雷达数据结构（统一接口）
// ============================================================================

// 人体检测数据结构
typedef struct {
    bool presence;               // 人体存在 (true=有人, false=无人)
    uint8_t motion_status;       // 运动状态 (0=无, 1=静止, 2=活跃)
    uint8_t movement_param;      // 体动参数 (0-100)
    uint16_t distance;           // 人体距离 (cm)
    int16_t position_x;          // X坐标 (cm)
    int16_t position_y;          // Y坐标 (cm)
    int16_t position_z;          // Z坐标 (cm)
} radar_human_data_t;

// 呼吸监测数据结构
typedef struct {
    bool respiratory_switch;     // 呼吸监测开关 (true=开, false=关)
    uint8_t respiratory_info;    // 呼吸信息 (1=正常, 2=呼吸过高, 3=呼吸过低, 4=无)
    uint8_t respiratory_value;   // 呼吸数值 (0-35)
    uint8_t respiratory_waveform[5]; // 呼吸波形数据 (5字节, 0-255)
    uint8_t slow_respiratory;    // 低缓呼吸判读 (10-20 次/min)
    bool waveform_upload_switch; // 呼吸波形上报开关 (true=开, false=关)
} radar_respiratory_data_t;

// 心率监测数据结构
typedef struct {
    bool heart_rate_switch;      // 心率监测开关 (true=开, false=关)
    uint8_t heart_rate_value;    // 心率数值 (60-120, 3秒上报一次)
    uint8_t heart_rate_waveform[5]; // 心率波形数据 (5字节, 0-255, 1秒上报一次)
} radar_heart_rate_data_t;

// 产品信息结构
typedef struct {
    char product_model[32];      // 产品型号
    char product_id[32];         // 产品ID
    char hardware_model[32];     // 硬件型号
    char firmware_version[32];   // 固件版本
} radar_product_info_t;

// 最近监测数据汇总结构
typedef struct {
    uint8_t movement_param;      // 体动参数 (0-100)
    uint32_t movement_timestamp; // 体动数据更新时间戳 (ms)
    uint8_t respiratory_value;   // 呼吸数值 (0-35, 次/分)
    uint32_t respiratory_timestamp; // 呼吸数据更新时间戳 (ms)
    uint8_t heart_rate_value;    // 心率数值 (60-120, 次/分)
    uint32_t heart_rate_timestamp; // 心率数据更新时间戳 (ms)
    bool valid;                  // 数据是否有效
} radar_latest_data_t;

// ============================================================================
// 回调函数类型定义
// ============================================================================

typedef void (*radar_human_callback_t)(const radar_human_data_t *data);
typedef void (*radar_respiratory_callback_t)(const radar_respiratory_data_t *data);
typedef void (*radar_heart_rate_callback_t)(const radar_heart_rate_data_t *data);

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// 通用雷达接口（统一API）
// ============================================================================

// 初始化雷达
void my_radar_init(void);

// 启动雷达
void my_radar_start(void);

// 停止雷达
void my_radar_stop(void);

// 设置回调函数
void my_radar_set_human_presence_callback(radar_human_callback_t callback);
void my_radar_set_human_movement_callback(radar_human_callback_t callback);
void my_radar_set_respiratory_callback(radar_respiratory_callback_t callback);
void my_radar_set_heart_rate_callback(radar_heart_rate_callback_t callback);

// 查询连接状态
bool my_radar_is_connected(void);

// 数据获取函数
bool my_radar_get_human_data(radar_human_data_t *data);
bool my_radar_get_respiratory_data(radar_respiratory_data_t *data);
bool my_radar_get_heart_rate_data(radar_heart_rate_data_t *data);
bool my_radar_get_product_info(radar_product_info_t *info);

// 获取最近的体动、呼吸、心率数据
bool my_radar_get_latest_data(radar_latest_data_t *data);

// 将雷达数据转换为 JSON 字符串
// 返回 JSON 字符串长度，失败返回 -1
// 注意：调用者需要释放返回的字符串（使用 free）
int my_radar_data_to_json(const radar_latest_data_t *data, char **json_str);

// ============================================================================
// 特定雷达功能（仅当使用对应雷达时可用）
// ============================================================================

#ifndef USE_AIRTOUCH_RADAR
// R60ABD1特有的命令接口
bool my_radar_send_command(uint8_t control, uint8_t command, const uint8_t *data, uint16_t length);
bool my_radar_query_product_info(void);
bool my_radar_query_human_presence(void);
bool my_radar_set_human_switch(bool enable);
bool my_radar_set_respiratory_switch(bool enable);
bool my_radar_set_heart_rate_switch(bool enable);
bool my_radar_query_human_switch_status(void);
#endif

#ifdef __cplusplus
}
#endif
