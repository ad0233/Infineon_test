#include "fsm_main.h"
#include <string.h>
#include <inttypes.h>
#include <sys/time.h>
#include <time.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_lvgl_port.h"
#include "ui.h"

#include "board.h"
#include "audio_recorder.h"
#include "audio_processor.h"

#include "my_ui_behavior.h"
#include "my_nvs.h"
#include <my_wifi.h>
#include "my_rtc.h"
#include "my_h264.h"
#include "my_rtc.h"
#include "my_lidar.h"
#include "my_lcd.h"
#include "broadcast.h"

#define TAG "fsm_main"

// ⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠
// ⚠不能在这里的函数使用fsm_event_handle ⚠
// ⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠
/*------------------------------------------------------------------------------找人------------------------------------------------------------------------------------------*/
// 静态变量：失败开始时间戳、保存的检测结果
static uint32_t s_fail_start_time = 0;  // 失败状态开始的时间戳（ms）
#define FIND_TIMEOUT_MS 15000  // 15秒超时
static bool s_person_detected = false;  // 体动检测结果（边播放边检测时设置）
static bool s_timeout_detected = false;  // 超时检测结果（边播放边检测时设置）

// 静态变量：闹钟开关状态
static bool s_alarm_enabled = true;  // true=有闹钟, false=无闹钟
static bool s_alarm_state_initialized = false;  // 状态是否已初始化

//找人
uint8_t fm_has_h_fd_state(void) {
    // 如果边播放边检测时已经检测到超时，直接返回超时
    if (s_timeout_detected) {
        return FM_H_F_TOUT;
    }
    
    // 如果边播放边检测时已经检测到体动，直接返回成功
    if (s_person_detected) {
        return FM_H_F_SUC;
    }
    
    my_lidar_handle_t lidar_handle = fsm_main_get_lidar_handle();
    if(my_lidar_have_human(lidar_handle)) {
        ESP_LOGI(TAG, "person found");
        return FM_H_F_SUC;
    } else {
        ESP_LOGI(TAG, "person not found");
    }

    if(esp_log_timestamp() - s_fail_start_time > FIND_TIMEOUT_MS) {
        return FM_H_F_TOUT;
    }
    
    // 返回保存的结果
    return FM_H_F_FAI;
}

// 获取失败开始时间戳（供外部任务使用）
uint32_t fsm_main_get_fail_start_time(void) {
    return s_fail_start_time;
}

// 设置体动检测结果（供encoder_test任务使用）
void fsm_main_set_person_detected(bool detected) {
    s_person_detected = detected;
}

// 设置超时检测结果（供encoder_test任务使用）
void fsm_main_set_timeout_detected(bool detected) {
    s_timeout_detected = detected;
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
    my_wifi_handle_t wifi_handle = fsm_main_get_wifi_handle();
    if (!wifi_handle) {
        ESP_LOGI(TAG, "wifi not initialized");
        return FM_W_FAI;
    }
    
    switch (my_wifi_get_state(wifi_handle))
    {
    case WIFI_STATE_CONNECTED: {
        // WiFi连接成功，检查NTP是否已同步
        my_rtc_handle_t rtc_handle = fsm_main_get_rtc_handle();
        if (rtc_handle != NULL && my_rtc_is_ntp_synced(rtc_handle)) {
            ESP_LOGI(TAG, "wifi is connected and NTP synced");
            return FM_W_SUC;
        } else {
            ESP_LOGI(TAG, "wifi is connected but NTP not synced yet");
            return FM_W_CONN;
        }
        break;
    }
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
    my_rtc_handle_t rtc_handle = fsm_main_get_rtc_handle();
    bool rtc_valid = (rtc_handle != NULL) ? my_rtc_is_time_valid(rtc_handle) : false;
    
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
        case 0: {// Wi-Fi
            my_wifi_handle_t wifi_handle = fsm_main_get_wifi_handle();
            if(wifi_handle && my_wifi_get_state(wifi_handle) == WIFI_STATE_CONNECTED){    //这里需要放入有无wifi的判断
                ret = FM_MEMU_WIFI_SC;
            }
            else{
                ret = FM_MEMU_WIFI_FA;
            }
            break;
        }
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
        case 5: // Brightness
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

    // 优先从硬件 RTC 获取当前值作为编辑起点
    my_rtc_handle_t rtc_handle = fsm_main_get_rtc_handle();
    if (rtc_handle != NULL && my_rtc_get_time(rtc_handle, &t, &valid) == 0 && valid) {
        s_editing_hour   = t.tm_hour;
        s_editing_minute = t.tm_min;
    } else {
        // 如果 RTC 无效，尝试使用系统时间（可能已由 NTP 设置）
        time_t now;
        time(&now);
        localtime_r(&now, &t);
        if (t.tm_year > 120) {
            s_editing_hour   = t.tm_hour;
            s_editing_minute = t.tm_min;
        } else {
            // 都无效则使用默认时间
            s_editing_hour   = 7;
            s_editing_minute = 30;
        }
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
    my_rtc_handle_t rtc_handle = fsm_main_get_rtc_handle();
    if (rtc_handle == NULL || my_rtc_get_time(rtc_handle, &t, &valid) != 0) {
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
    int ret = my_rtc_set_time(rtc_handle, &t);

    if (ret == 0) {
        ESP_LOGI(TAG, "RTC saved successfully: %02d:%02d",
                 s_editing_hour, s_editing_minute);
        
        // 如果 NTP 还没同步，我们要同步系统时间到这个手动设置的时间
        // 这样 UI 上的时间（由系统时间驱动）才会立即改变
        if (rtc_handle != NULL && !my_rtc_is_ntp_synced(rtc_handle)) {
            ESP_LOGI(TAG, "NTP not synced, updating system time from manual RTC set");
            time_t timestamp = mktime(&t);
            struct timeval tv = { .tv_sec = timestamp, .tv_usec = 0 };
            settimeofday(&tv, NULL);
        }
    } else {
        ESP_LOGE(TAG, "RTC save failed (err=%d)!", ret);
    }

    //---- 4. 返回主界面（它会再次读取 RTC）----
    fsm_main_to_clock(nullptr, last_state, next_state);
}



void fsm_main_to_clock(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "to clock...");
    
    struct tm timeinfo;
    bool time_valid = false;
    my_rtc_handle_t rtc_handle = fsm_main_get_rtc_handle();

    // 1. 优先尝试获取系统时间（如果NTP同步过，或者启动时从RTC同步过，系统时间是准确的）
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // 如果年份 > 2020 (1900 + 120)，说明系统时间已经被设置过
    if (timeinfo.tm_year > 120) {
        time_valid = true;
        ESP_LOGI(TAG, "Using system time: %02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    } 
    // 2. 如果系统时间无效，尝试直接从硬件RTC读取
    else if (rtc_handle != NULL) {
        bool rtc_valid = false;
        if (my_rtc_get_time(rtc_handle, &timeinfo, &rtc_valid) == 0 && rtc_valid) {
            time_valid = true;
            ESP_LOGI(TAG, "Using hardware RTC time: %02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        }
    }

    uint8_t hour = time_valid ? timeinfo.tm_hour : 7;
    uint8_t minute = time_valid ? timeinfo.tm_min : 30;

    if (!time_valid) {
        ESP_LOGW(TAG, "No valid time source found, using default 07:30");
    }

    lvgl_port_lock(0);
    
    // 先更新标签内容，再加载屏幕，防止显示 Studio 默认值 (如06:24) 导致的闪烁
    char hour_buf[8], min_buf[8];
    snprintf(hour_buf, sizeof(hour_buf), "%02d", hour);
    snprintf(min_buf, sizeof(min_buf), "%02d", minute);
    
    if (ui_MainHour) lv_label_set_text(ui_MainHour, hour_buf);
    if (ui_MainMinute) lv_label_set_text(ui_MainMinute, min_buf);
    
    // 从NVS读取保存的闹钟时间并更新主页面显示
    const struct device_config *cfg = my_nvs_get_config();
    if (cfg != nullptr && ui_MainMorningAlarm != NULL) {
        char alarm_time_str[8];
        snprintf(alarm_time_str, sizeof(alarm_time_str), "%02d:%02d", cfg->alarm_hour, cfg->alarm_minute);
        lv_label_set_text(ui_MainMorningAlarm, alarm_time_str);
    }

    lv_disp_load_scr(ui_MianNoPerson);
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
        "Brightness",      // 5
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
// 根据动物类型获取对应的歌曲文件路径
static const char* get_unwind_music_path(unwind_animal_t animal) {
    switch (animal) {
        case UNWIND_ANIMAL_CAT:
            return "/sdcard/V001-Unwind-cat.wav";
        case UNWIND_ANIMAL_FOX:
            return "/sdcard/V001-Unwind-Fox.wav";
        case UNWIND_ANIMAL_HUMMINGBIRD:
            return "/sdcard/V001-Unwind-Bird.wav";
        case UNWIND_ANIMAL_KOI:
            return "/sdcard/V001-Unwind-Fish.wav";
        case UNWIND_ANIMAL_SWAN:
            return "/sdcard/V001-Unwind-Swan.wav";
        default:
            ESP_LOGW(TAG, "Unknown animal type %d, using cat music", animal);
            return "/sdcard/V001-Unwind-cat.wav";
    }
}

static my_h264_animation_t get_unwind_h264_anim(unwind_animal_t animal) {
    switch (animal) {
        case UNWIND_ANIMAL_CAT:
            return MY_H264_ANIM_CAT;
        case UNWIND_ANIMAL_FOX:
            return MY_H264_ANIM_FOX;
        case UNWIND_ANIMAL_HUMMINGBIRD:
            return MY_H264_ANIM_BIRD;
        case UNWIND_ANIMAL_KOI:
            return MY_H264_ANIM_FISH;
        case UNWIND_ANIMAL_SWAN:
            return MY_H264_ANIM_SWAN;
        default:
            return MY_H264_ANIM_CAT;
    }
}


void fsm_unwind_next_item(void *arg, uint8_t last_state, uint8_t next_state) {
    if (arg == nullptr) {
        ESP_LOGW(TAG, "fsm_unwind_next_item: arg is nullptr");
        return;
    }
    
    int32_t diff = *(int32_t*)arg;
    
    ESP_LOGI(TAG, "fsm_unwind_next_item, diff: %" PRId32, diff);
    
    // 获取当前动物
    unwind_animal_t current_animal = my_ui_unwind_get_animal();
    unwind_animal_t next_animal;
    
    // 根据旋钮方向计算下一个动物（只更新变量，不更新UI）
    // diff > 0: 右旋（向下），diff < 0: 左旋（向上）
    if (diff > 0) {
        // 向下切换（下一个）
        next_animal = (unwind_animal_t)((current_animal + 1) % UNWIND_ANIMAL_MAX);
        ESP_LOGI(TAG, "fsm_unwind_next_item: next animal %d", next_animal);
    } else if (diff < 0) {
        // 向上切换（上一个）
        next_animal = (unwind_animal_t)((current_animal + UNWIND_ANIMAL_MAX - 1) % UNWIND_ANIMAL_MAX);
        ESP_LOGI(TAG, "fsm_unwind_next_item: prev animal %d", next_animal);
    } else {
        ESP_LOGI(TAG, "fsm_unwind_next_item: diff is 0, no action");
        return;
    }
    
    // 只更新动物变量（不更新UI）
    my_ui_unwind_set_animal_no_ui(next_animal);
    
    // 获取对应的动画和音频路径
    const char* music_path = get_unwind_music_path(next_animal);
    my_h264_animation_t h264_anim = get_unwind_h264_anim(next_animal);
    
    ESP_LOGI(TAG, "fsm_unwind_next_item: playing animation and music for animal %d: %s", next_animal, music_path);
    // 播放动画和音频
    my_h264_start(h264_anim, 100);
    audio_tone_play(music_path);
}
 
// void fsm_main_in_unwind(void *arg, uint8_t last_state, uint8_t next_state) {
//     ESP_LOGI(TAG, "fsm_main_in_unwind: last_state=%d, next_state=%d", last_state, next_state);
//     ESP_LOGI(TAG, "in unwind mode...");
    
//     if(last_state == F_MAIN_S_MENU_UNWIND_PLAYING) {
//         my_ui_unwind_set_animal(UNWIND_ANIMAL_CAT);
//     }
    
//     // 调用UI函数，内部会处理显示和3秒定时器
//     my_ui_in_unwind();

//     // 获取当前显示的动物，播放对应的歌曲
//     unwind_animal_t current_animal = my_ui_unwind_get_animal();
//     const char* music_path = get_unwind_music_path(current_animal);
//     ESP_LOGI(TAG, "fsm_main_in_unwind: playing music for animal %d: %s", current_animal, music_path);
//     // audio_tone_play 内部会处理停止逻辑，直接调用即可
//     audio_tone_play(music_path);
// }

void fsm_main_in_unwind(void *arg, uint8_t last_state, uint8_t next_state) {

    my_ui_unwind_set_animal(UNWIND_ANIMAL_CAT);
    
    // 获取当前显示的动物，播放对应的动画和歌曲
    unwind_animal_t current_animal = my_ui_unwind_get_animal();
    const char* music_path = get_unwind_music_path(current_animal);
    my_h264_animation_t h264_anim = get_unwind_h264_anim(current_animal);
    
    ESP_LOGI(TAG, "fsm_main_in_unwind: playing animation and music for animal %d: %s", current_animal, music_path);
    // 播放动画和音频
    my_h264_start(h264_anim, 100);
    audio_tone_play(music_path);
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
    //更新音量
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_set_volume(board_handle->audio_hal, new_volume);
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
    my_rtc_handle_t rtc_handle = fsm_main_get_rtc_handle();
    return (rtc_handle != NULL) ? my_rtc_is_time_valid(rtc_handle) : false;
}

void fsm_main_lidar_clock_update(void *arg, uint8_t last_state, uint8_t next_state) {
    // ESP_LOGI(TAG, "lidar update - clock");
    
}

//开机动画
void fsm_main_uninit_playing(void *arg, uint8_t last_state, uint8_t next_state){
    my_h264_start(MY_H264_ANIM_BRAND_MOTION2,100);
}
//找人动画前
void fsm_main_lidar_find_boot(void *arg, uint8_t last_state, uint8_t next_state){
    my_h264_start(MY_H264_ANIM_GO_UP,100);
}


//找人动画
void fsm_main_lidar_find_playing(void *arg, uint8_t last_state, uint8_t next_state){
    // 只在第一次进入F_MAIN_S_FINDPERSONC_ANIM状态时设置时间戳（从其他状态进入时）
    // 如果是从F_MAIN_S_FINDPERSONC_ANIM循环进入（last_state == F_MAIN_S_FINDPERSONC_ANIM），不重置时间戳
    if (last_state != F_MAIN_S_FINDPERSONC_ANIM) {
        s_fail_start_time = esp_log_timestamp();
        ESP_LOGI(TAG, "First time entering find person animation, start timeout timer");
    }
    
    // 重置检测结果标志位
    s_person_detected = false;
    s_timeout_detected = false;
    
    // 启动动画
    my_h264_start(MY_H264_ANIM_PROCESSING,100);
}
//找到人动画
void fsm_main_find_someone(void *arg, uint8_t last_state, uint8_t next_state){
    s_person_detected = false;  // 清除标志位
    s_timeout_detected = false;
    my_h264_start(MY_H264_ANIM_HUMAN_RECOGNIZED,100);
}
//没找到人动画
void fsm_main_no_find_someone(void *arg, uint8_t last_state, uint8_t next_state){
    // 清除标志位，因为已经进入失败流程
    s_person_detected = false;
    s_timeout_detected = false;
    my_h264_start(MY_H264_ANIM_FAIL2,100);
}

//音乐动画
void fsm_main_memu_cat_playing(void *arg, uint8_t last_state, uint8_t next_state){
    ESP_LOGI(TAG, "fsm_main_memu_cat_playing: last_state=%d (%s), next_state=%d (%s)", 
             last_state, fsm_main_get_current_state_str(), 
             next_state, (next_state == F_MAIN_S_MENU_UNWIND_PLAYING) ? "MENU_UNWIND_PLAYING" : "OTHER");
    
    // 进入动画时，禁用雷达检测
    fsm_main_set_radar_detect_enabled(false);
    // 获取当前选中的动物
     unwind_animal_t current_animal = my_ui_unwind_get_animal();
    my_h264_animation_t anim_to_play = MY_H264_ANIM_CAT;  // 默认使用CAT动画
    
    // 根据选中的动物选择对应的动画
    switch (current_animal)
    {
    case UNWIND_ANIMAL_CAT:
        anim_to_play = MY_H264_ANIM_CAT;
        ESP_LOGI(TAG, "Playing CAT animation");
        audio_tone_play("/sdcard/V001-Unwind-cat_hum.wav");
        break;
    
    case UNWIND_ANIMAL_FOX:
        anim_to_play = MY_H264_ANIM_FOX; 
        ESP_LOGI(TAG, "Playing FOX animation (using CAT for now)");
        break;
    
    case UNWIND_ANIMAL_HUMMINGBIRD:
        anim_to_play = MY_H264_ANIM_BIRD;  
        ESP_LOGI(TAG, "Playing HUMMINGBIRD animation (using CAT for now)");
        break;
    
    case UNWIND_ANIMAL_KOI:
        anim_to_play = MY_H264_ANIM_FISH;  
        ESP_LOGI(TAG, "Playing KOI animation (using CAT for now)");
        break;
    
    case UNWIND_ANIMAL_SWAN:
        anim_to_play = MY_H264_ANIM_SWAN;  
        ESP_LOGI(TAG, "Playing SWAN animation (using CAT for now)");
        break;   

    default:
        anim_to_play = MY_H264_ANIM_CAT;
        ESP_LOGW(TAG, "Unknown animal type, using CAT animation");
        break;
    }

    my_h264_start(anim_to_play, 100);

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
    my_wifi_handle_t wifi_handle = fsm_main_get_wifi_handle();
    if (wifi_handle) {
        my_wifi_auto_connect(wifi_handle);
    }
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
    initWakeModeTestTextColors();  // 初始化页面显示状态
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
void fsm_main_in_sleep_mode(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in sleep_mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_sleepMode);
    lvgl_port_unlock();
    // 恢复睡眠模式页面的初始样式（确保是黄色月亮和Good Night）
    my_ui_sleep_mode_reset_to_default();
    audio_tone_play("/sdcard/V001-night-Bed-test.wav");
}

void fsm_main_in_sleep_mode_ready(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in sleep_mode_ready...");
    // 不切换屏幕，直接修改当前睡眠模式页面的样式
    // 显示白圈，文字改回 Good Night
    my_ui_sleep_mode_ready();
    audio_tone_play("/sdcard/V001-night-Breath-test.wav");
}

void fsm_main_in_night_mode(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in night_mode...");
    // 不切换屏幕，直接修改当前睡眠模式页面的样式
    my_ui_sleep_mode_to_night_mode();
    // 启动进度条更新任务
    my_ui_night_mode_start_progress();
}

/**
 * @brief 歌词实时刷新任务 (高频 10ms 轮询)
 */
static void lyric_update_task(void* param) {
    uint8_t target_state = (uint8_t)(uintptr_t)param;
    ESP_LOGI(TAG, "lyric_update_task started (10ms mode)");
    
    while (fsm_main_get_current_state() == target_state) {
        uint32_t played_ms = 0;
        // 获取硬件层真实的音频播放进度
        if (player_pipeline_get_progress(&played_ms) == ESP_OK) {
            lvgl_port_lock(0);
            updateMorningAnimationLyrics((int)played_ms);
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "lyric_update_task finished");
    vTaskDelete(NULL);
}

void fsm_main_in_MorningAnimation(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "Entering MorningAnimation...");
    
    // 先停止之前的播放，确保清除所有残留状态和事件
    audio_tone_stop();
    // 给系统一点时间处理停止操作
    vTaskDelay(pdMS_TO_TICKS(50));
    
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_MorningAnimation);
    // 确保使用正确的文件名：/sdcard/V001-morning_Voice.lrc
    initMorningAnimationLyrics("/sdcard/V001-morning_Voice.lrc");
    lvgl_port_unlock();
    
    // 播放音频
    audio_tone_play("/sdcard/V001-morning.wav");
    
    // 创建歌词更新任务
    xTaskCreate(lyric_update_task, "lyric_update", 4096, (void*)(uintptr_t)next_state, 5, NULL);
}

void fsm_main_in_OTA(void *arg, uint8_t last_state, uint8_t next_state) {
    ESP_LOGI(TAG, "in ota mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_OTA);
    size_t ota_progress = (size_t)arg;
    char progress_str[10];
    snprintf(progress_str, sizeof(progress_str), "%d%%", ota_progress);
    lv_label_set_text(ui_OTALabel2, progress_str);
    lv_slider_set_range(ui_OTASlider, 0, 100);
    lv_slider_set_value(ui_OTASlider, ota_progress, LV_ANIM_OFF);
    lv_disp_t *disp = lv_disp_get_default();
    if (disp != NULL) {
        lv_refr_now(disp);
    }
    lvgl_port_unlock();
}


