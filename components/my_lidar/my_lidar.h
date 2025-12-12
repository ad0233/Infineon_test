#pragma once

// 高复用要求，主要用于协议代码，方便多端使用（单片机，电脑，后端服务）
// 只添加 std 库，什么环境下都不会错误
#include <stdint.h>
#include <stdbool.h>

// 低复用要求, 函数入参一定要用到环境相关的声明（主要用在单一环境）
#include <esp_err.h>

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
    uint32_t movement_timestamp; // 体动数据更新时间戳 (s)
    uint8_t respiratory_value;   // 呼吸数值 (0-35, 次/分)
    uint32_t respiratory_timestamp; // 呼吸数据更新时间戳 (s)
    uint8_t heart_rate_value;    // 心率数值 (60-120, 次/分)
    uint32_t heart_rate_timestamp; // 心率数据更新时间戳 (s)
    uint32_t heart_rate_system_timestamp;   // 心率系统时间戳 (s)
    uint32_t respiratory_system_timestamp;   // 呼吸系统时间戳 (s)
    uint32_t movement_system_timestamp;   // 体动系统时间戳 (s)
} radar_latest_data_t;

// 句柄声明 struct my_lidar_impl* 是在源文件内部实现，这里隐式声明，避免使用空指针
typedef struct my_lidar_impl* my_lidar_handle_t;

// 旧的回调类型定义（用于底层实现兼容）
typedef void (*radar_human_callback_t)(const radar_human_data_t *data);
typedef void (*radar_respiratory_callback_t)(const radar_respiratory_data_t *data);
typedef void (*radar_heart_rate_callback_t)(const radar_heart_rate_data_t *data);

// 事件回调声明，context 是用于传递上下文
typedef void (*my_lidar_human_presence_callback_t)(const radar_human_data_t *data, void *context, my_lidar_handle_t self);
typedef void (*my_lidar_human_movement_callback_t)(const radar_human_data_t *data, void *context, my_lidar_handle_t self);
typedef void (*my_lidar_respiratory_callback_t)(const radar_respiratory_data_t *data, void *context, my_lidar_handle_t self);
typedef void (*my_lidar_heart_rate_callback_t)(const radar_heart_rate_data_t *data, void *context, my_lidar_handle_t self);

// CPP 文件兼容声明，很多时候还是需要用到 cpp 一些特性来简化代码
#ifdef __cplusplus
extern "C" {
#endif

/// 初始化，入参是句柄指针的指针,用于返回句柄指针
// 返回最好是 int，表示运行结果，可以参考下常规 Linux 对错误的定义，esp_err_t 本质也是 int
int my_lidar_init(my_lidar_handle_t *self_out);

//******** 功能函数 ********

// 启动雷达
int my_lidar_start(my_lidar_handle_t self);

// 停止雷达
int my_lidar_stop(my_lidar_handle_t self);

// 注册回调函数，建议只允许被注册一次，不然会有很多异步冲突的问题导致异常
int my_lidar_reg_cb_human_presence(my_lidar_handle_t self, my_lidar_human_presence_callback_t func, void *context);
int my_lidar_reg_cb_human_movement(my_lidar_handle_t self, my_lidar_human_movement_callback_t func, void *context);
int my_lidar_reg_cb_respiratory(my_lidar_handle_t self, my_lidar_respiratory_callback_t func, void *context);
int my_lidar_reg_cb_heart_rate(my_lidar_handle_t self, my_lidar_heart_rate_callback_t func, void *context);

// 查询连接状态
bool my_lidar_is_connected(my_lidar_handle_t self);
// 查询是否有人体存在
bool my_lidar_have_human(my_lidar_handle_t self);

// 数据获取函数
int my_lidar_get_human_data(my_lidar_handle_t self, radar_human_data_t *data);
int my_lidar_get_respiratory_data(my_lidar_handle_t self, radar_respiratory_data_t *data);
int my_lidar_get_heart_rate_data(my_lidar_handle_t self, radar_heart_rate_data_t *data);
int my_lidar_get_product_info(my_lidar_handle_t self, radar_product_info_t *info);

// 获取最近的体动、呼吸、心率数据
int my_lidar_get_latest_data(my_lidar_handle_t self, radar_latest_data_t *data);

// 用于节省内存，定时刷新函数，可以让一个线程运行多个组件的 flush，减少线程数量
int my_lidar_flush(my_lidar_handle_t self, uint32_t interval_ms);

// 设置灵敏度参数（movement_threshold: 体动参数阈值0-100，默认20；heart_rate_timeout_ms: 心率数据超时时间毫秒，默认5000）
int my_lidar_set_sensitivity(my_lidar_handle_t self, uint8_t movement_threshold, uint32_t heart_rate_timeout_ms);

// ============================================================================
// 特定雷达功能（仅当使用对应雷达时可用）
// ============================================================================

#ifndef USE_AIRTOUCH_RADAR
// R60ABD1特有的命令接口
int my_lidar_send_command(my_lidar_handle_t self, uint8_t control, uint8_t command, const uint8_t *data, uint16_t length);
int my_lidar_query_product_info(my_lidar_handle_t self);
int my_lidar_query_human_presence(my_lidar_handle_t self);
int my_lidar_set_human_switch(my_lidar_handle_t self, bool enable);
int my_lidar_set_respiratory_switch(my_lidar_handle_t self, bool enable);
int my_lidar_set_heart_rate_switch(my_lidar_handle_t self, bool enable);
int my_lidar_query_human_switch_status(my_lidar_handle_t self);
#endif

#ifdef __cplusplus
}
#endif
