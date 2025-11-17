#pragma once

#include <stdint.h>
#include <stdbool.h>

// 功能菜单位置枚举（必须与UI中roller的顺序一致）
// UI顺序：Wi-Fi\nWake Mode\nAlarm\nUnwind\nVolume\nScreen Brightness\nSet Time
typedef enum {
    MENU_WIFI = 0,         // Wi-Fi
    MENU_WAKE_MODE,        // 唤醒模式 (Wake Mode)
    MENU_ALARM,            // 闹钟 (Alarm)
    MENU_UNWIND,     // 睡眠声音 (Unwind)
    MENU_VOLUME,           // 音量 (Volume)
    MENU_BODY_DATA,        // 身体数据 (Screen Brightness)
    MENU_SET_TIME,         // 设置时间 (Set Time)
    MENU_MAX               // 菜单项总数
} menu_item_t;

// 页面枚举 - 对应behavior文件中的函数
typedef enum {
    PAGE_START = 0,        // 启动页面
    PAGE_NETWORK_GUIDE,    // 网络引导
    PAGE_NETWORK_CONNECTING, // 网络连接中
    PAGE_NETWORK_CONN_SUC, // 网络连接成功
    PAGE_NETWORK_CONN_FAIL, // 网络连接失败
    PAGE_NETWORK_OFFLINE,  // 网络离线
    PAGE_CLOCK,            // 时钟页面
    PAGE_FUNCTION,         // 功能页面
    PAGE_ALARM,            // 闹钟页面
    PAGE_VOLUME,           // 音量页面
    PAGE_TIME_SET,         // 时间设置页面
    PAGE_UNWIND,           // 放松页面
    PAGE_UNWIND_ON,        // Unwind动物选择页面
    PAGE_UNWIND_SELECT,    // Unwind确认页面
    PAGE_NOISE_TIME,       // 噪音时间设置页面
    PAGE_GOOD_MORNING,     // 早安页面
    PAGE_SLEEP_DATA,       // 睡眠数据页面
    PAGE_DETECTION,        // Detection页面
    PAGE_DETECTION_N,      // DetectionN页面
    PAGE_DETECTION_Y,      // DetectionY页面
    PAGE_DETECTION_N1,     // DetectionN1页面
    PAGE_MIAN_YES_PERSON,  // MianYesPerson页面
    PAGE_OTA,              // OTA页面
    PAGE_BODY_DATA,        // 身体数据页面
    PAGE_WAKE_MODE,        // 智能唤醒模式页面
    PAGE_LIGHT,            // 亮度调节页面
    PAGE_RADAR_DISPLAY,    // 雷达显示页面
    PAGE_MAX               // 页面总数
} page_t;

// 唤醒模式枚举
typedef enum {
    WAKE_MODE_SMART = 0,   // 智能唤醒模式
    WAKE_MODE_CLASSIC,     // 经典唤醒模式
    WAKE_MODE_MAX          // 模式总数
} wake_mode_t;

// 睡眠数据结构体
typedef struct {
    uint8_t sleep_hour;        // 入睡时间（小时）
    uint8_t sleep_minute;      // 入睡时间（分钟）
    uint8_t wake_start_hour;   // 唤醒窗口开始时间（小时）
    uint8_t wake_start_minute; // 唤醒窗口开始时间（分钟）
    uint8_t wake_end_hour;     // 唤醒窗口结束时间（小时）
    uint8_t wake_end_minute;   // 唤醒窗口结束时间（分钟）
    uint8_t duration_hour;     // 睡眠时长（小时）
    uint8_t duration_minute;   // 睡眠时长（分钟）
    uint8_t deep_sleep_percent; // 深睡百分比
    uint8_t deep_sleep_minute; // 深睡时长（分钟）
    float respiratory_rate;    // 呼吸频率
    uint8_t heart_rate;        // 平均心率
    uint8_t time_to_sleep;     // 入睡用时（分钟）
} sleep_data_t;

// Unwind动物类型枚举
typedef enum {
    UNWIND_ANIMAL_CAT = 0,
    UNWIND_ANIMAL_FOX,
    UNWIND_ANIMAL_HUMMINGBIRD,
    UNWIND_ANIMAL_KOI,
    UNWIND_ANIMAL_SWAN,
    UNWIND_ANIMAL_MAX
} unwind_animal_t;

#ifdef __cplusplus
extern "C" {
#endif

void my_ui_set_light(uint8_t val);
void my_ui_in_start();
void my_ui_network_guide();
void my_ui_network_connecting();
void my_ui_network_conn_suc();
void my_ui_network_conn_fail();
void my_ui_network_offline();
void my_ui_in_clock();
void my_ui_clock_set_now_time(uint8_t hour, uint8_t min);
void my_ui_clock_set_tips(const char* tips);
void my_ui_clock_set_emoji(uint8_t emoji);
void my_ui_clock_show_alarm(uint8_t hour, uint8_t min);

// 二维码相关函数
// 生成二维码，格式: {url}?sn={device_id}
// device_id 从 iot_config 的 thing_name 获取，如果没有则使用 MAC 地址
void my_ui_generate_qr_code(const char* url, const char* device_id);
void my_ui_clock_show_no_alarm(void);
void my_ui_in_funtion();
void my_ui_function_menu_up();
void my_ui_function_menu_down();
uint32_t my_ui_function_get_index();
void my_ui_in_alarm();
void my_ui_in_no_alarm();
void my_ui_alarm_set_title(const char *title);
void my_ui_alarm_set_tips(const char *tips);
void my_ui_alarm_set_time(uint8_t hour, uint8_t min);
void my_ui_alarm_get_time(uint8_t *hour, uint8_t *min);
void my_ui_in_volume();
void my_ui_volume_set(int val);
uint8_t my_ui_volume_get();
void my_ui_in_time_set();
void my_ui_time_set(uint8_t hour, uint8_t min);
void my_ui_time_get(uint8_t *hour, uint8_t *min);
void my_ui_in_unwind();
void my_ui_in_unwind_on(void);
void my_ui_in_unwind_select(void);
void my_ui_in_noise_time(void);
void my_ui_unwind_set_animal(unwind_animal_t animal);
unwind_animal_t my_ui_unwind_get_animal(void);
void my_ui_unwind_next_animal(void);
void my_ui_unwind_prev_animal(void);
uint8_t my_ui_noise_time_get_selection(void);
void my_ui_unwind_select_mode(uint8_t mode);
void my_ui_in_good_morning();
void my_ui_good_morning_set_emoji(uint8_t emoji);
void my_ui_good_morning_set_txt(const char* txt);
void my_ui_in_sleep_data();
void my_ui_sleep_data_set(const sleep_data_t *data);
void my_ui_sleep_data_get(sleep_data_t *data);
void my_ui_in_detection(void);
void my_ui_in_detection_n(void);
void my_ui_in_detection_y(void);
void my_ui_in_detection_n1(void);
void my_ui_in_mian_yes_person(void);
void my_ui_in_ota();
void my_ui_ota_set_progress(uint8_t progress);
void my_ui_in_body_data();
void my_ui_in_wake_mode();

// 唤醒模式相关函数
void my_ui_wake_mode_set(wake_mode_t mode);
wake_mode_t my_ui_wake_mode_get(void);
void my_ui_wake_mode_next(void);
void my_ui_wake_mode_prev(void);
void my_ui_wake_mode_save_current(void);

// 亮度调节相关函数
void my_ui_in_light(void);
void my_ui_light_set(uint8_t duty);
uint8_t my_ui_light_get(void);
void my_ui_light_next(void);
void my_ui_light_prev(void);

// 雷达数据更新函数
void my_ui_radar_update_presence(bool presence);
void my_ui_radar_update_movement(uint8_t movement);
void my_ui_radar_update_respiratory(uint8_t respiratory);
void my_ui_radar_update_heart_rate(uint8_t heart_rate);

// 雷达显示页面相关函数
void my_ui_in_radar_display(void);
void my_ui_radar_display_update(float angle, int distance_cm, uint8_t heart_rate, uint8_t breath_rate);

// 主页面图片显示控制
void my_ui_main_image_set_visibility(bool visible);

// 页面状态管理函数
int my_ui_get_current_page(void);

// 页面跳转函数
void navigate_to_page(page_t page);

#ifdef __cplusplus
}
#endif