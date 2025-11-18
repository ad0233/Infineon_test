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

#define TAG "fsm_main"

// ⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠
// ⚠不能在这里的函数使用fsm_event_handle ⚠
// ⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠

// 静态变量：失败开始时间戳、保存的检测结果
static uint32_t s_fail_start_time = 0;  // 失败状态开始的时间戳（ms）
static uint8_t s_saved_state = FM_H_F_FAI;  // 保存的检测结果
static bool s_state_valid = false;  // 检测结果是否有效
#define FIND_TIMEOUT_MS 15000  // 15秒超时

// 执行一次检测并保存结果
static void do_detection(void) {
    // 使用ESP32硬件随机数生成器
    int rand_val = esp_random() % 20;
    ESP_LOGI(TAG, "I=%d", rand_val);
    
    uint32_t current_time = esp_log_timestamp();
    
    if (rand_val > 10) {
        // 成功：清除失败时间戳
        s_fail_start_time = 0;
        s_saved_state = FM_H_F_SUC;
        ESP_LOGI(TAG, "human_find is success");
    } else {
        // 失败：检查是否超时
        if (s_fail_start_time == 0) {
            // 第一次失败，记录开始时间
            s_fail_start_time = current_time;
            s_saved_state = FM_H_F_FAI;
            ESP_LOGI(TAG, "human_find_failed, start timer");
        } else {
            // 检查是否超时
            uint32_t elapsed = current_time - s_fail_start_time;
            if (elapsed >= FIND_TIMEOUT_MS) {
                // 超时
                s_saved_state = FM_H_F_TOUT;
                ESP_LOGI(TAG, "human_find_timeout after %" PRIu32 " ms", elapsed);
            } else {
                // 未超时，继续失败
                s_saved_state = FM_H_F_FAI;
                ESP_LOGI(TAG, "human_find_failed, elapsed: %" PRIu32 " ms", elapsed);
            }
        }
    }
    s_state_valid = true;
}

//找人
uint8_t fm_has_h_fd_state(void) {
    // 如果结果无效，执行一次检测
    if (!s_state_valid) {
        do_detection();
    }
    // 返回保存的结果
    return s_saved_state;
}

//网络
uint8_t fm_has_w_c_state(void) {
    // 硬编码用于测试页面流程
    // FM_W_N_CFG -> WIFI_GUIDE (二维码页面)
    // FM_W_CONN -> WIFI_CONN (连接中页面)
    // FM_W_SUC -> CLOCK (时钟页面)
    // FM_W_FAI -> WIFI_GUIDE (二维码页面)
    
    uint16_t i = WIFI_STATE_IDLE;  // 改为 IDLE，会走到 default 返回 FM_W_FAI，跳转到二维码页面
    switch (i)
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
        ESP_LOGI(TAG, "wifi not configured (for testing)");
        return FM_W_N_CFG;  // 返回未配置，跳转到二维码页面
        break;
    }
    return FM_W_N_CFG;
}



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

//获取当前菜单索引
static int getCurrentMenuIndex(void) {
    return my_ui_function_get_index();
}

uint8_t fm_has_memu_state(void){
    int current_index = getCurrentMenuIndex();
    ESP_LOGI(TAG, "fm_has_memu_state, current_index: %d", current_index);
    // 根据当前索引返回对应的菜单状态
    switch(current_index) {
        case 0: // Wi-Fi
            if(1){    //这里需要放入有无wifi的判断
                return FM_MEMU_WIFI_SC;
            }
            else{
                return FM_MEMU_WIFI_FA;
            }
        case 1: // Wake Mode
            return FM_MEMU_WAKE_MOD;
        case 2: // Alarm
            return FM_MEMU_ALARM;
        case 3: // Unwind
            return FM_MEMU_UNWIND;
        case 4: // Volume
            return FM_MEMU_VOL;
        case 5: // Screen Brightness
            return FM_MEMU_SC_BR;
        case 6: // Set Time
            return FM_MEMU_SETTIME;
        default:
            ESP_LOGW(TAG, "Unknown menu index: %d", current_index);
            return 0;  // 默认返回第一个
    }
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

// 静态变量：保存当前编辑的时间
static uint8_t s_editing_hour = 0;
static uint8_t s_editing_minute = 0;
static bool s_editing_hour_mode = false;  // true=编辑小时, false=编辑分钟

void fsm_main_set_time(void *arg) {
    ESP_LOGI(TAG, "to set time...");
    
    // RTC无效时才进入此函数，使用默认值
    s_editing_hour = 7;
    s_editing_minute = 30;
    s_editing_hour_mode = false;  // 默认先编辑分钟
    
    // 更新UI显示
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_SetTime);
    
    // 显示当前时间
    char hour_str[8];
    char min_str[8];
    snprintf(hour_str, sizeof(hour_str), "%02d", s_editing_hour);
    snprintf(min_str, sizeof(min_str), "%02d", s_editing_minute);
    lv_label_set_text(ui_SetTimeHour, hour_str);
    lv_label_set_text(ui_SetTimeMinute, min_str);
    
    lvgl_port_unlock();
}

void fsm_main_rtc_adjust_time(void *arg) {
    if (arg == nullptr) {
        return;
    }
    
    // 获取旋钮变化量，正数=右旋(增加)，负数=左旋(减少)
    int32_t diff = *(int32_t*)arg;
    
    if (s_editing_hour_mode) {
        // 编辑小时
        int new_hour = s_editing_hour + diff;
        if (new_hour > 23) {
            s_editing_hour = new_hour - 24;
        } else if (new_hour < 0) {
            s_editing_hour = new_hour + 24;
        } else {
            s_editing_hour = new_hour;
        }
        
        // 更新UI显示
        char hour_str[8];
        snprintf(hour_str, sizeof(hour_str), "%02d", s_editing_hour);
        lvgl_port_lock(0);
        lv_label_set_text(ui_SetTimeHour, hour_str);
        lvgl_port_unlock();
        
        ESP_LOGI(TAG, "Adjust hour: %d (diff: %" PRId32 ")", s_editing_hour, diff);
    } else {
        // 编辑分钟
        int new_minute = s_editing_minute + diff;
        if (new_minute > 59) {
            s_editing_minute = new_minute - 60;
            // 分钟进位，小时也要增加
            s_editing_hour = (s_editing_hour + 1) % 24;
        } else if (new_minute < 0) {
            s_editing_minute = new_minute + 60;
            // 分钟借位，小时也要减少
            if (s_editing_hour == 0) {
                s_editing_hour = 23;
            } else {
                s_editing_hour -= 1;
            }
        } else {
            s_editing_minute = new_minute;
        }
        
        // 更新UI显示
        char min_str[8];
        char hour_str[8];
        snprintf(min_str, sizeof(min_str), "%02d", s_editing_minute);
        snprintf(hour_str, sizeof(hour_str), "%02d", s_editing_hour);
        lvgl_port_lock(0);
        lv_label_set_text(ui_SetTimeMinute, min_str);
        lv_label_set_text(ui_SetTimeHour, hour_str);  // 如果进位/借位，小时也要更新
        lvgl_port_unlock();
        
        ESP_LOGI(TAG, "Adjust minute: %d (diff: %" PRId32 ")", s_editing_minute, diff);
    }
}

void fsm_main_rtc_save_and_exit(void *arg) {
    // 尝试从RTC获取当前日期（即使时间无效，日期可能还有）
    struct tm time;
    bool valid = false;
    if (my_rtc_get_time(&time, &valid) != 0) {
        // 如果获取失败，使用默认日期
        memset(&time, 0, sizeof(time));
        time.tm_year = 124;  // 2024年（从1900年开始）
        time.tm_mon = 0;     // 1月
        time.tm_mday = 1;    // 1日
        time.tm_wday = 1;    // 周一
    }
    // 只更新时间部分
    time.tm_hour = s_editing_hour;
    time.tm_min = s_editing_minute;
    time.tm_sec = 0;     // 秒数清零
    
    // 使用 my_rtc_set_time 保存时间到RTC
    if (my_rtc_set_time(&time) == 0) {
        ESP_LOGI(TAG, "RTC time saved: %02d:%02d", s_editing_hour, s_editing_minute);
    } else {
        ESP_LOGE(TAG, "Failed to save RTC time");
    }

    // 返回主页面，会自动从RTC读取最新时间并更新显示
    fsm_main_to_clock(arg);
}


void fsm_main_to_clock(void *arg) {
    ESP_LOGI(TAG, "to clock...");
    
    // 检查RTC是否已配置（使用 my_rtc_is_time_valid 判断）
    if (!my_rtc_is_time_valid()) {
        // RTC未配置，跳转到配置页面
        ESP_LOGI(TAG, "RTC not configured, go to RTC setup page");
        fsm_main_set_time(arg);
        return;
    }
    
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

/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/


void fsm_menu_next_item(void *arg) {
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
}


/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
uint8_t fsm_clock_need_cfg(void) {
    return my_rtc_is_time_valid();
}

void fsm_main_lidar_clock_update(void *arg) {
    // ESP_LOGI(TAG, "lidar update - clock");
    
}

//开机动画
void fsm_main_uninit_playing(void *arg){
    lvgl_port_stop();
    my_h264_start(MY_H264_ANIM_BRAND_MOTION2,100);

    
}
//找人动画前
void fsm_main_lidar_find_boot(void *arg){
    lvgl_port_stop();
    my_h264_start(MY_H264_ANIM_GO_UP,100);
}


//找人动画
void fsm_main_lidar_find_playing(void *arg){
    // 重置失败时间戳，开始新的检测周期
    s_fail_start_time = 0;
    s_state_valid = false;  // 标记检测结果无效，下次调用时会重新检测
    
    lvgl_port_stop();
    my_h264_start(MY_H264_ANIM_PROCESSING,100);
}
//找到人动画
void fsm_main_find_someone(void *arg){
    lvgl_port_stop();
    my_h264_start(MY_H264_ANIM_HUMAN_RECOGNIZED,100);
}
//没找到人动画
void fsm_main_no_find_someone(void *arg){
    lvgl_port_stop();
    my_h264_start(MY_H264_ANIM_FAIL2,100);
}

void fsm_main_lidar_find(void *arg) {
    ESP_LOGI(TAG, "find person...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Detection);
    lvgl_port_unlock();
}

void fsm_main_lidar_find_suc(void *arg) {
    ESP_LOGI(TAG, "find suc");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_DetectionY);
    lvgl_port_unlock();
}

void fsm_main_lidar_find_fail(void *arg) {
    ESP_LOGI(TAG, "find fail, restart finding");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_DetectionN1);
    lvgl_port_unlock();
}

void fsm_main_wifi_guide(void *arg) {
    ESP_LOGI(TAG, "wifi guide...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_NetworkBoot);
    lvgl_port_unlock();
}

void fsm_main_wifi_connecting(void *arg) {
    ESP_LOGI(TAG, "wifi connecting...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Connecting);
    lvgl_port_unlock();
}

void fsm_main_wifi_conn_suc(void *arg) {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_ConnectingSuccess);
    lvgl_port_unlock();
}

void fsm_main_wifi_conn_fail(void *arg) {
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_ConnectingFailed);
    lvgl_port_unlock();
}

void fsm_main_wifi_reconn(void *arg) {
    ESP_LOGI(TAG, "wifi reconnecting...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Connecting);
    lvgl_port_unlock();
    my_wifi_auto_connect();
}

void fsm_main_wifi_forget(void *arg) {
    ESP_LOGI(TAG, "wifi forget and guide...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_NetworkBoot);
    lvgl_port_unlock();
}

void fsm_main_in_offline(void *arg) {
    ESP_LOGI(TAG, "in offline mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_OfflineMode);
    lvgl_port_unlock();
}

void fsm_main_in_memu(void *arg) {
    ESP_LOGI(TAG, "in menu...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Memu);
    lvgl_port_unlock();
}
//wifi

void fsm_main_in_wifi_sc(void *arg) {
    ESP_LOGI(TAG, "in wifi_sc mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_ConnectingSuccess);
    lvgl_port_unlock();
}
void fsm_main_in_wifi_fa(void *arg) {
    ESP_LOGI(TAG, "in wifi_fa mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_OfflineMode);
    lvgl_port_unlock();
}
//wake mode
void fsm_main_in_wake_mode(void *arg) {
    ESP_LOGI(TAG, "in wake_mode mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_WakeModeTest);
    lvgl_port_unlock();
}

//alarm
void fsm_main_in_alarm(void *arg) {
    ESP_LOGI(TAG, "in alarm mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_AlarmON);
    lvgl_port_unlock();
}


void fsm_main_in_unwind(void *arg) {
    ESP_LOGI(TAG, "in unwind mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_UnwindSelet);
    lvgl_port_unlock();
}


void fsm_main_in_volume(void *arg) {
    ESP_LOGI(TAG, "in_volume mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Volume_);
    lvgl_port_unlock();
}


void fsm_main_in_light(void *arg) {
    ESP_LOGI(TAG, "in light mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_Light);
    lvgl_port_unlock();
}


void fsm_main_in_set_time(void *arg) {
    ESP_LOGI(TAG, "in set_time mode...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_SetTime);
    lvgl_port_unlock();
}



