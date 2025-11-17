#include "fsm_main.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "ui.h"

#include "my_ui_behavior.h"
#include "my_nvs.h"
#include <my_wifi.h>
#include "my_rtc.h"
#include "my_h264.h"
#include "random"


#define TAG "fsm_main"

// ⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠
// ⚠不能在这里的函数使用fsm_event_handle ⚠
// ⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠
// 静态变量：当前状态、定时器句柄（仅创建一次）
static enum fm_human_find_conn_state s_cur_state = FM_H_F_FAI;
static TimerHandle_t s_timeout_timer = NULL;


// 定时器回调函数：15秒超时后触发，切换为超时状态
static void timeout_cb(TimerHandle_t xTimer) {
    s_cur_state = FM_H_F_TOUT;
}


// 初始化定时器（程序启动时调用一次即可）
static void init_timer() {
    if (s_timeout_timer == NULL) {
        // 创建15秒单次触发定时器（超时后只回调一次）
        s_timeout_timer = xTimerCreate(
            "find_timeout",       // 定时器名称（调试用）
            pdMS_TO_TICKS(15000), // 15秒（转换为FreeRTOS节拍数）
            pdFALSE,              // 单次触发（不是周期性）
            NULL,                 // 无参数
            timeout_cb            // 超时回调函数
        );
    }
}


// 获取当前找人状态（含超时逻辑）
enum fm_human_find_conn_state my_human_find_get_state(void) {
    init_timer(); // 确保定时器已初始化

    // 1. 生成随机结果（>10为成功，否则失败）
    int rand_val = rand() % 20;
    uint8_t is_success = (rand_val > 10) ? 1 : 0;

    // 2. 处理状态和定时器
    if (is_success) {
        // 成功：更新状态，停止定时器（避免超时误触发）
        s_cur_state = FM_H_F_SUC;
        xTimerStop(s_timeout_timer, 0); // 0表示不等待立即停止
    } else {
        // 失败：如果当前不是失败/超时状态，启动定时器
        if (s_cur_state != FM_H_F_FAI && s_cur_state != FM_H_F_TOUT) {
            s_cur_state = FM_H_F_FAI;
            xTimerStart(s_timeout_timer, 0); // 启动15秒定时器
        }
        // 若已在失败状态：定时器继续运行，超时后自动切为FM_H_F_TOUT
    }

    return s_cur_state;
}


//找人
uint8_t fm_has_h_fd_state(void) {
    // 没有配置 （这里应该是查看雷达是否 enable ）
    // const struct device_config *cfg = my_nvs_get_config();
    // if(cfg->wifi_enable == false) {
    //     ESP_LOGI(TAG, "wifi not configured");
    //     return FM_W_N_CFG;
    // }
    switch (my_human_find_get_state())
    {
    case FM_H_F_SUC:
        ESP_LOGI(TAG, "human_find is successs");
        return FM_H_F_SUC;
        break;
    case FM_H_F_FAI:
        ESP_LOGI(TAG, "human_find_failed");
        return FM_H_F_FAI;
        break;
    case FM_H_F_TOUT:
        ESP_LOGI(TAG, "human_find_Timeout");
        return FM_H_F_TOUT;
        break;
    default:
        ESP_LOGI(TAG, "wifi  connection Timeout");
        return FM_H_F_TOUT;
        break;
    }
}

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
    my_h264_start(MY_H264_ANIM_GO_UP,100);
    
}
//找人动画
void fsm_main_lidar_find_playing(void *arg){
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

void fsm_main_to_clock(void *arg) {
    ESP_LOGI(TAG, "to clock...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_MianNoPerson);
    lvgl_port_unlock();
}

void fsm_main_menu_turn(void *arg) {
    auto diff = *(int32_t*)arg; // *(bool*)arg 一样
    ESP_LOGI(TAG, "to clock...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_MianNoPerson);
    lvgl_port_unlock();
}