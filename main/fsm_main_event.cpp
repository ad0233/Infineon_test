#include "fsm_main.h"
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_lvgl_port.h"
#include "ui.h"

#include "my_ui_behavior.h"
#include "my_nvs.h"
#include <my_wifi.h>
#include "my_rtc.h"
#include "my_h264.h"
#include "my_rtc.h"
#include "my_lidar.h"

#define TAG "fsm_main"

// ⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠
// ⚠不能在这里的函数使用fsm_event_handle ⚠
// ⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠
/*------------------------------------------------------------------------------找人------------------------------------------------------------------------------------------*/
// 静态变量：失败开始时间戳、保存的检测结果
static uint32_t s_fail_start_time = 0;  // 失败状态开始的时间戳（ms）
#define FIND_TIMEOUT_MS 15000  // 15秒超时

// 静态变量：闹钟开关状态
static bool s_alarm_enabled = true;  // true=有闹钟, false=无闹钟
static bool s_alarm_state_initialized = false;  // 状态是否已初始化

//找人
uint8_t fm_has_h_fd_state(void) {
    radar_latest_data_t radar_data;
    if(my_radar_get_latest_data(&radar_data)) {
        ESP_LOGI(TAG, "movement_param: %d", radar_data.movement_param);
        // 挥挥手就识别成功了
        if(radar_data.movement_param > 15) {
            return FM_H_F_SUC;
        }
        // TODO: 心率检测更合理些,因为如果没人,就不会有心率更新,但是甲方要求体动判断先
        // if(esp_log_timestamp() - radar_data.heart_rate_system_timestamp < 3) {
        //     return FM_H_F_SUC;
        // }
        return FM_H_F_FAI;
    }

    if(esp_log_timestamp() - s_fail_start_time > FIND_TIMEOUT_MS) {
        return FM_H_F_TOUT;
    }
    
    // 返回保存的结果
    return FM_H_F_FAI;
}
/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
//网络
uint8_t fm_has_w_c_state(void) {
    // 没有配置
    const struct device_config *cfg = my_nvs_get_config();
    if(cfg->wifi_enable == false) {
        ESP_LOGI(TAG, "wifi not configured");
        return FM_W_N_CFG;
    }
    switch (my_wifi_get_state())
    {
    case WIFI_STATE_CONNECTED:
        ESP_LOGI(TAG, "wifi is connected");
        return FM_W_SUC;
        break;
    case WIFI_STATE_CONNECTING:
        ESP_LOGI(TAG, "wifi is connecting");
        return FM_W_CONN;
        break;
    case WIFI_STATE_FAILED:
        ESP_LOGI(TAG, "wifi connection failed");
        return FM_W_FAI;
        break;
    default:
        ESP_LOGI(TAG, "wifi connection failed");
        return FM_W_FAI;
        break;
    }
    return FM_W_FAI;
}

uint8_t fsm_has_wifi_config(void) {
    const struct device_config *cfg = my_nvs_get_config();
    return cfg->wifi_enable;
}


/*----------------------------------------------------------------------rtc-----------------------------------------------------------------------------------------------*/
uint8_t fm_has_rtc_state(void) {
    // my_rtc_is_time_valid() 返回 bool 类型：true(1) 表示有效，false(0) 表示无效
    bool rtc_valid = my_rtc_is_time_valid();
    
    if (rtc_valid) {
        ESP_LOGI(TAG, "RTC connected");
        return FM_RTC_EXIST;
    } else {
        ESP_LOGI(TAG, "RTC NO connecting");
        return FM_RTC_NO_EXIST;
    }
}
/*------------------------------------------------------------------菜单---------------------------------------------------------------------------------------------------*/
//获取当前菜单索引
static int getCurrentMenuIndex(void) {
    return my_ui_function_get_index();
}

uint8_t fm_has_memu_state(void){
    // 第一次调用时，从NVS读取闹钟状态
    if (!s_alarm_state_initialized) {
        const struct device_config *cfg = my_nvs_get_config();
        if (cfg != nullptr) {
            s_alarm_enabled = (cfg->alarm_enable != 0);
            ESP_LOGI(TAG, "Initialized alarm state from NVS: %s", s_alarm_enabled ? "enabled" : "disabled");
        } else {
            s_alarm_enabled = true;  // 默认启用
            ESP_LOGW(TAG, "Failed to load alarm state from NVS, using default enabled");
        }
        s_alarm_state_initialized = true;
    }
    
    int current_index = getCurrentMenuIndex();
    ESP_LOGI(TAG, "fm_has_memu_state, current_index: %d", current_index);
    // 根据当前索引返回对应的菜单状态
    uint8_t ret = 0;
    switch(current_index) {
        case 0: // Wi-Fi
            if(my_wifi_get_state() == WIFI_STATE_CONNECTED){    //这里需要放入有无wifi的判断
                ret = FM_MEMU_WIFI_SC;
            }
            else{
                ret = FM_MEMU_WIFI_FA;
            }
            break;
        case 1: // Wake Mode
            ret = FM_MEMU_WAKE_MOD;
            break;
        case 2: // Alarm
            if(s_alarm_enabled){    // 根据闹钟开关状态判断
                ret = FM_MEMU_ALARM_SC;
            }
            else{
                ret = FM_MEMU_ALARM_FA;
            }
            break;
        case 3: // Unwind
            ret = FM_MEMU_UNWIND;
            break;
        case 4: // Volume
            ret = FM_MEMU_VOL;
            break;
        case 5: // Screen Brightness
            ret = FM_MEMU_SC_BR;
            break;
        case 6: // Set Time
            ret = FM_MEMU_SETTIME;
            break;
        default:
            ESP_LOGW(TAG, "Unknown menu index: %d", current_index);
            ret = 0;  // 默认返回第一个
            break;
    }
    ESP_LOGI(TAG, "fm_has_memu_state, returning: %d (FM_MEMU_WIFI_SC=%d, FM_MEMU_ALARM_SC=%d)", 
             ret, FM_MEMU_WIFI_SC, FM_MEMU_ALARM_SC);
    return ret;
}

/*------------------------------------------------------------------------------rtc------------------------------------------------------------------------------------------*/

// 静态变量：保存当前编辑的时间
static uint8_t s_editing_hour = 0;
static uint8_t s_editing_minute = 0;
static bool s_editing_hour_mode = false;  // true=编辑小时, false=编辑分钟
///设置时间，settime 和  rtc 一个页面  如果rtc设置过则用rtc的时间 没有则  7：30
void fsm_main_set_time(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "to set time...");

    struct tm t;
    bool valid = false;

    // 检查 RTC 是否有效
    if (my_rtc_get_time(&t, &valid) == 0 && valid) {
        // RTC 已设置，使用 RTC 时间
        s_editing_hour   = t.tm_hour;
        s_editing_minute = t.tm_min;
    } else {
        // RTC 未设置，使用默认时间
        s_editing_hour   = 7;
        s_editing_minute = 30;
    }

    s_editing_hour_mode = false;  // 默认先编辑分钟

    // 更新 UI
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_SetTime);

    char hour_str[8];
    char min_str[8];
    snprintf(hour_str, sizeof(hour_str), "%02d", s_editing_hour);
    snprintf(min_str, sizeof(min_str), "%02d", s_editing_minute);
    lv_label_set_text(ui_SetTimeHour, hour_str);
    lv_label_set_text(ui_SetTimeMinute, min_str);

    lvgl_port_unlock();
}


void fsm_main_rtc_adjust_time(void *arg, uint8_t last_state, uint8_t next_state)
{
    if (!arg) return;

    int32_t diff = *(int32_t*)arg;

    lvgl_port_lock(0);

    if (s_editing_hour_mode) {
        // 小时：0~23，简单环绕
        s_editing_hour = (s_editing_hour + diff + 24) % 24;

        char hour_str[4];
        snprintf(hour_str, sizeof(hour_str), "%02d", s_editing_hour);
        lv_label_set_text(ui_SetTimeHour, hour_str);

        ESP_LOGI(TAG, "Hour updated: %d (diff=%ld)", s_editing_hour, diff);

    } else {
        // 分钟：0~59
        int new_min = s_editing_minute + diff;

        // 处理分钟变动可能引起的小时改变：
        if (new_min >= 60) {
            s_editing_minute = new_min % 60;
            s_editing_hour = (s_editing_hour + (new_min / 60)) % 24;
        }
        else if (new_min < 0) {
            // 处理负数分钟，例如 -5 → 55，同时小时-1
            int borrow = (abs(new_min) + 59) / 60; // 需要借多少小时
            s_editing_minute = (new_min % 60 + 60) % 60;
            s_editing_hour = (s_editing_hour - borrow + 24) % 24;
        }
        else {
            s_editing_minute = new_min;
        }

        // 更新 UI
        char hour_str[4], min_str[4];
        snprintf(hour_str, sizeof(hour_str), "%02d", s_editing_hour);
        snprintf(min_str,  sizeof(min_str), "%02d", s_editing_minute);
        lv_label_set_text(ui_SetTimeHour, hour_str);
        lv_label_set_text(ui_SetTimeMinute, min_str);

        ESP_LOGI(TAG, "Minute updated: %d, Hour updated: %d (diff=%ld)", 
                 s_editing_minute, s_editing_hour, diff);
    }

    lvgl_port_unlock();
}


void fsm_main_rtc_save_and_exit(void *arg, uint8_t last_state, uint8_t next_state)
{
    ESP_LOGI(TAG, "Saving RTC time...");

    struct tm t;
    memset(&t, 0, sizeof(t));  // 确保所有字段初始化为 0

    bool valid = false;

    //---- 1. 尝试从 RTC 获取当前日期 ----
    if (my_rtc_get_time(&t, &valid) != 0) {
        ESP_LOGW(TAG, "RTC read failed, using default date.");

        t.tm_year = 124;  // 2024 = 1900 + 124
        t.tm_mon  = 0;    // 1月
        t.tm_mday = 1;    // 1号
        t.tm_wday = 1;    // 周一
    }

    //---- 2. 覆盖用户设置的时间 ----
    t.tm_hour = s_editing_hour;
    t.tm_min  = s_editing_minute;
    t.tm_sec  = 0;

    //---- 3. 写入 RTC ----
    int ret = my_rtc_set_time(&t);

    if (ret == 0) {
        ESP_LOGI(TAG, "RTC saved successfully: %02d:%02d",
                 s_editing_hour, s_editing_minute);
    } else {
        ESP_LOGE(TAG, "RTC save failed (err=%d)!", ret);
    }

    //---- 4. 返回主界面（它会再次读取 RTC）----
    fsm_main_to_clock(nullptr, last_state, next_state);
}



void fsm_main_to_clock(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "to clock...");
    
    // RTC已配置，从RTC读取时间
    struct tm time;
    bool valid = false;
    uint8_t hour = 7;
    uint8_t minute = 30;
    
    if (my_rtc_get_time(&time, &valid) == 0) {
        hour = time.tm_hour;
        minute = time.tm_min;
    }
    
    // 加载主页面
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_MianNoPerson);
    
    // 使用 lv_label_set_text 直接更新主页面时间显示
    char hour_str[8];
    char min_str[8];
    snprintf(hour_str, sizeof(hour_str), "%02d", hour);
    snprintf(min_str, sizeof(min_str), "%02d", minute);
    lv_label_set_text(ui_MainHour, hour_str);
    lv_label_set_text(ui_MainMinute, min_str);
    
    lvgl_port_unlock();
    
    ESP_LOGI(TAG, "Clock time updated: %02d:%02d", hour, minute);
}

/*----------------------------------------------------------------------------菜单的切换----------------------------------------------------------------------------------------*/


void fsm_menu_next_item(void *arg, uint8_t last_state, uint8_t next_state) {
    if (arg == nullptr) {
        return;
    }
    
    int32_t diff = *(int32_t*)arg;
    
    ESP_LOGI(TAG, "fsm_menu_next_item, diff: %" PRId32, diff);
    
    // 根据旋钮方向切换菜单项
    // diff > 0: 右旋（向下），diff < 0: 左旋（向上）
    if (diff > 0) {
        // 向下切换（下一个菜单项）
        my_ui_function_menu_down();
    } else if (diff < 0) {
        // 向上切换（上一个菜单项）
        my_ui_function_menu_up();
    }
    
    // 获取切换后的菜单索引和名称
    int current_index = getCurrentMenuIndex();
    const char* menu_names[] = {
        "Wi-Fi",           // 0
        "Wake Mode",       // 1
        "Alarm",           // 2
        "Unwind",          // 3
        "Volume",          // 4
        "Screen Brightness", // 5
        "Set Time"         // 6
    };
    
    const char* menu_name = "Unknown";
    if (current_index >= 0 && current_index < 7) {
        menu_name = menu_names[current_index];
    }
    
    ESP_LOGI(TAG, "Menu switched: index=%d, name=%s", current_index, menu_name);
}


/*-------------------------------------------------------------------------------------唤醒的切换------------------------------------------------------------------------------*/

void fsm_wake_mode_next_item(void *arg, uint8_t last_state, uint8_t next_state) {
    if (arg == nullptr) {
        return;
    }
    
    int32_t diff = *(int32_t*)arg;
    
    ESP_LOGI(TAG, "fsm_menu_next_item, diff: %" PRId32, diff);
    
    // 根据旋钮方向切换菜单项
    // diff > 0: 右旋（向下），diff < 0: 左旋（向上）
    if (diff > 0) {
        // 向下切换（下一个菜单项）
        wakeModeTestDown();
    } else if (diff < 0) {
        // 向上切换（上一个菜单项）
        wakeModeTestUP();
    }
}

/*-----------------------------------------------------------------------------------设置闹钟-------------------------------------------------------------------------------------*/
// 静态变量：保存当前编辑的闹钟时间
static uint8_t s_alarm_editing_hour = 8;
static uint8_t s_alarm_editing_minute = 0;
static bool s_alarm_editing_hour_mode = false;  // true=编辑小时, false=编辑分钟

void fsm_set_alarm_item(void *arg, uint8_t last_state, uint8_t next_state) {
    if (arg == nullptr) {
        return;
    }
    
    // 获取旋钮变化量，正数=右旋(增加)，负数=左旋(减少)
    int32_t diff = *(int32_t*)arg;
    
    // 步进改为5
    int32_t step = diff * 5;
    
    // 先获取当前时间（从UI读取）
    uint8_t hour, minute;
    my_ui_alarm_get_time(&hour, &minute);
    s_alarm_editing_hour = hour;
    s_alarm_editing_minute = minute;
    
    if (s_alarm_editing_hour_mode) {
        // 编辑小时
        int new_hour = s_alarm_editing_hour + step;
        if (new_hour > 23) {
            s_alarm_editing_hour = new_hour - 24;
        } else if (new_hour < 0) {
            s_alarm_editing_hour = new_hour + 24;
        } else {
            s_alarm_editing_hour = new_hour;
        }
        
        // 使用 my_ui_alarm_set_time 更新闹钟时间显示
        my_ui_alarm_set_time(s_alarm_editing_hour, s_alarm_editing_minute);
        
        // 保存时间到NVS
        const struct device_config *cfg = my_nvs_get_config();
        if (cfg != nullptr) {
            struct device_config new_cfg = *cfg;
            new_cfg.alarm_hour = s_alarm_editing_hour;
            new_cfg.alarm_minute = s_alarm_editing_minute;
            new_cfg.alarm_enable = 1;
            my_nvs_update_config(&new_cfg);
            ESP_LOGI(TAG, "Saved alarm time to NVS: %02d:%02d", s_alarm_editing_hour, s_alarm_editing_minute);
        }
        
        ESP_LOGI(TAG, "Adjust alarm hour: %d (step: %" PRId32 ")", s_alarm_editing_hour, step);
    } else {
        // 编辑分钟
        int new_minute = s_alarm_editing_minute + step;
        if (new_minute > 59) {
            s_alarm_editing_minute = new_minute - 60;
            // 分钟进位，小时也要增加
            s_alarm_editing_hour = (s_alarm_editing_hour + 1) % 24;
        } else if (new_minute < 0) {
            s_alarm_editing_minute = new_minute + 60;
            // 分钟借位，小时也要减少
            if (s_alarm_editing_hour == 0) {
                s_alarm_editing_hour = 23;
            } else {
                s_alarm_editing_hour -= 1;
            }
        } else {
            s_alarm_editing_minute = new_minute;
        }
        
        // 使用 my_ui_alarm_set_time 更新闹钟时间显示
        my_ui_alarm_set_time(s_alarm_editing_hour, s_alarm_editing_minute);
        
        // 保存时间到NVS
        const struct device_config *cfg = my_nvs_get_config();
        if (cfg != nullptr) {
            struct device_config new_cfg = *cfg;
            new_cfg.alarm_hour = s_alarm_editing_hour;
            new_cfg.alarm_minute = s_alarm_editing_minute;
            new_cfg.alarm_enable = 1;
            my_nvs_update_config(&new_cfg);
            ESP_LOGI(TAG, "Saved alarm time to NVS: %02d:%02d", s_alarm_editing_hour, s_alarm_editing_minute);
        }
        
        ESP_LOGI(TAG, "Adjust alarm minute: %d (step: %" PRId32 ")", s_alarm_editing_minute, step);
    }
}

/*----------------------------------------------------------------------------------unwind-------------------------------------------------------------------------------------*/
void fsm_unwind_next_item(void *arg, uint8_t last_state, uint8_t next_state) {
    if (arg == nullptr) {
        ESP_LOGW(TAG, "fsm_unwind_next_item: arg is nullptr");
        return;
    }
    
    int32_t diff = *(int32_t*)arg;
    
    ESP_LOGI(TAG, "fsm_unwind_next_item, diff: %" PRId32, diff);
    
    // 根据旋钮方向切换菜单项
    // diff > 0: 右旋（向下），diff < 0: 左旋（向上）
    if (diff > 0) {
        // 向下切换（下一个菜单项）
        ESP_LOGI(TAG, "fsm_unwind_next_item: calling my_ui_unwind_next_animal()");
        my_ui_unwind_next_animal();
    } else if (diff < 0) {
        // 向上切换（上一个菜单项）
        ESP_LOGI(TAG, "fsm_unwind_next_item: calling my_ui_unwind_prev_animal()");
        my_ui_unwind_prev_animal();
    } else {
        ESP_LOGI(TAG, "fsm_unwind_next_item: diff is 0, no action");
    }
}

/*----------------------------------------------------------------------------------volume-------------------------------------------------------------------------------------*/
void fsm_volume_next_item(void *arg, uint8_t last_state, uint8_t next_state) {
    if (arg == nullptr) {
        ESP_LOGW(TAG, "fsm_volume_next_item: arg is nullptr");
        return;
    }
    
    // 获取旋钮变化量，正数=右旋(增加)，负数=左旋(减少)
    int32_t diff = *(int32_t*)arg;
    
    ESP_LOGI(TAG, "fsm_volume_next_item, diff: %" PRId32, diff);
    
    // 获取当前音量
    uint8_t current_volume = my_ui_volume_get();
    
    // 计算新音量
    int32_t step = diff ;
    int new_volume = current_volume + step;
    
    // 限制音量范围在0-100
    if (new_volume > 100) {
        new_volume = 100;
    } else if (new_volume < 0) {
        new_volume = 0;
    }
    
    ESP_LOGI(TAG, "fsm_volume_next_item: volume %d -> %d (step: %" PRId32 ")", current_volume, new_volume, step);
    
    // 设置新音量
    my_ui_volume_set(new_volume);
}

/*----------------------------------------------------------------------------------light-------------------------------------------------------------------------------------*/
void fsm_light_next_item(void *arg, uint8_t last_state, uint8_t next_state) {
    if (arg == nullptr) {
        ESP_LOGW(TAG, "fsm_light_next_item: arg is nullptr");
        return;
    }
    
    // 获取旋钮变化量，正数=右旋(增加)，负数=左旋(减少)
    int32_t diff = *(int32_t*)arg;
    
    ESP_LOGI(TAG, "fsm_light_next_item, diff: %" PRId32, diff);
    
    // 根据旋钮方向切换亮度
    // diff > 0: 右旋（增加），diff < 0: 左旋（减少）
    if (diff > 0) {
        // 右旋，增加亮度
        ESP_LOGI(TAG, "fsm_light_next_item: calling my_ui_light_next()");
        my_ui_light_next();
    } else if (diff < 0) {
        // 左旋，减少亮度
        ESP_LOGI(TAG, "fsm_light_next_item: calling my_ui_light_prev()");
        my_ui_light_prev();
    } else {
        ESP_LOGI(TAG, "fsm_light_next_item: diff is 0, no action");
    }
}
/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
uint8_t fsm_clock_need_cfg(void) {
    return my_rtc_is_time_valid();
}

void fsm_main_lidar_clock_update(void *arg, uint8_t last_state, uint8_t next_state) {
    // ESP_LOGI(TAG, "lidar update - clock");
    
}

//开机动画
void fsm_main_uninit_playing(void *arg, uint8_t last_state, uint8_t next_state){
    lvgl_port_stop();
    my_h264_start(MY_H264_ANIM_BRAND_MOTION2,100);

    
}
//找人动画前
void fsm_main_lidar_find_boot(void *arg, uint8_t last_state, uint8_t next_state){
    lvgl_port_stop();
    my_h264_start(MY_H264_ANIM_GO_UP,100);
}


//找人动画
void fsm_main_lidar_find_playing(void *arg, uint8_t last_state, uint8_t next_state){
    // 重置失败时间戳，开始新的检测周期
    
    lvgl_port_stop();
    my_h264_start(MY_H264_ANIM_PROCESSING,100);

    if(last_state == F_MAIN_S_UNINIT_PLAYING || last_state == F_MAIN_S_FINDFAIL) {
        s_fail_start_time = esp_log_timestamp();
    }
}
//找到人动画
void fsm_main_find_someone(void *arg, uint8_t last_state, uint8_t next_state){
    lvgl_port_stop();
    my_h264_start(MY_H264_ANIM_HUMAN_RECOGNIZED,100);
}
//没找到人动画
void fsm_main_no_find_someone(void *arg, uint8_t last_state, uint8_t next_state){
    lvgl_port_stop();
    my_h264_start(MY_H264_ANIM_FAIL2,100);
}

void fsm_main_lidar_find(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "find person...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Detection);
    lvgl_port_unlock();
}

void fsm_main_lidar_find_suc(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "find suc");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_DetectionY);
    lvgl_port_unlock();
}

void fsm_main_lidar_find_fail(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "find fail, restart finding");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_DetectionN1);
    lvgl_port_unlock();
}

void fsm_main_wifi_guide(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "wifi guide...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_NetworkBoot);
    lvgl_port_unlock();
}

void fsm_main_wifi_connecting(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "wifi connecting...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Connecting);
    lvgl_port_unlock();
}

void fsm_main_wifi_conn_suc(void *arg, uint8_t last_state, uint8_t next_state) {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_ConnectingSuccess);
    lvgl_port_unlock();
}

void fsm_main_wifi_conn_fail(void *arg, uint8_t last_state, uint8_t next_state) {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_ConnectingFailed);
    lvgl_port_unlock();
}

void fsm_main_wifi_reconn(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "wifi reconnecting...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Connecting);
    lvgl_port_unlock();
    my_wifi_auto_connect();
}

void fsm_main_wifi_forget(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "wifi forget and guide...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_NetworkBoot);
    lvgl_port_unlock();
}

void fsm_main_in_offline(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in offline mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_OfflineMode);
    lvgl_port_unlock();
}

void fsm_main_in_memu(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in menu...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Memu);
    lvgl_port_unlock();
}
//wifi

void fsm_main_in_wifi_sc(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in wifi_sc mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_ConnectingSuccess);
    lvgl_port_unlock();
}
void fsm_main_in_wifi_fa(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in wifi_fa mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_OfflineMode);
    lvgl_port_unlock();
}
//wake mode
void fsm_main_in_wake_mode(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in wake_mode mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_WakeModeTest);
    lvgl_port_unlock();
}

//alarm
void fsm_main_in_alarm(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in alarm mode...");
    
    // 设置闹钟状态为开启
    s_alarm_enabled = true;
    
    // 从NVS读取保存的闹钟时间
    const struct device_config *cfg = my_nvs_get_config();
    if (cfg != nullptr && cfg->alarm_enable != 0) {
        s_alarm_editing_hour = cfg->alarm_hour;
        s_alarm_editing_minute = cfg->alarm_minute;
        ESP_LOGI(TAG, "Loaded alarm time from NVS: %02d:%02d", s_alarm_editing_hour, s_alarm_editing_minute);
    } else {
        // 如果没有保存的时间，使用默认值
        s_alarm_editing_hour = 8;
        s_alarm_editing_minute = 0;
        ESP_LOGI(TAG, "No saved alarm time, using default: %02d:%02d", s_alarm_editing_hour, s_alarm_editing_minute);
    }
    
    s_alarm_editing_hour_mode = false;  // 默认先编辑分钟
    
    // 先加载UI页面
    my_ui_in_alarm();
    
    // 显示保存的时间（在 my_ui_in_alarm 之后调用，覆盖默认的8:00）
    my_ui_alarm_set_time(s_alarm_editing_hour, s_alarm_editing_minute);
    
    // 更新NVS中的闹钟使能状态
    if (cfg != nullptr) {
        struct device_config new_cfg = *cfg;
        new_cfg.alarm_enable = 1;
        my_nvs_update_config(&new_cfg);
    }
}
void fsm_main_in_no_alarm(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in no alarm mode...");
    
    // 设置闹钟状态为关闭
    s_alarm_enabled = false;
    
    // 更新NVS中的闹钟使能状态
    const struct device_config *cfg = my_nvs_get_config();
    if (cfg != nullptr) {
        struct device_config new_cfg = *cfg;
        new_cfg.alarm_enable = 0;
        my_nvs_update_config(&new_cfg);
        ESP_LOGI(TAG, "Alarm disabled, saved to NVS");
    }
    
    my_ui_in_no_alarm();
}


void fsm_main_in_unwind(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in unwind mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_UnwindSelet);
    lvgl_port_unlock();
}


void fsm_main_in_volume(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in_volume mode...");
    my_ui_in_volume();
}


void fsm_main_in_light(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in light mode...");
    my_ui_in_light();
}


void fsm_main_in_set_time(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in set_time mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_SetTime);
    lvgl_port_unlock();
}

void fsm_main_in_boya_data(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in boya_data mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_sleepData);
    lvgl_port_unlock();
}
void fsm_main_in_radarinfo(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "inradarinfo mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_RadarInfo);
    lvgl_port_unlock();
}
void fsm_main_in_GoodMorning_demo(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in GoodMorning_deme mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_MorningAnimation);
    lvgl_port_unlock();
}
void fsm_main_in_reminder_tomorrow(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "inreminder_tomorrow mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_ReminderTomorrow);
    lvgl_port_unlock();
}
void fsm_main_in_OTA(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in ota mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_OTA);
    lvgl_port_unlock();
}


