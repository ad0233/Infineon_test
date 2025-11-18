#include "fsm_main.h"

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

void fsm_main_wifi_detect(void *arg) {
    ESP_LOGI(TAG, "wifi detecting...");
    // 检测 wifi 状态并触发相应事件
    uint8_t wifi_state = fm_has_w_c_state();
    enum fsm_main_event_enum event;
    
    switch (wifi_state) {
        case FM_W_N_CFG:
            event = F_MAIN_E_WIFI_C_UNCFG;
            ESP_LOGI(TAG, "wifi state: not configured, trigger WIFI_C_UNCFG");
            break;
        case FM_W_CONN:
            event = F_MAIN_E_WIFI_C_CONN;
            ESP_LOGI(TAG, "wifi state: connecting, trigger WIFI_C_CONN");
            break;
        case FM_W_SUC:
            event = F_MAIN_E_WIFI_C_SUC;
            ESP_LOGI(TAG, "wifi state: connected, trigger WIFI_C_SUC");
            break;
        case FM_W_FAI:
        default:
            event = F_MAIN_E_WIFI_C_FAIL;
            ESP_LOGI(TAG, "wifi state: failed, trigger WIFI_C_FAIL");
            break;
    }
    
    // 延迟触发事件，确保状态机已切换到 WIFI_DETECT 状态
    static enum fsm_main_event_enum s_pending_event = F_MAIN_E_INIT;
    s_pending_event = event;
    
    xTaskCreate([](void *arg) {
        vTaskDelay(pdMS_TO_TICKS(100));
        enum fsm_main_event_enum *evt_ptr = (enum fsm_main_event_enum *)arg;
        enum fsm_main_event_enum evt = *evt_ptr;
        ESP_LOGI(TAG, "triggering wifi event: %d", evt);
        fsm_main_event_trig(evt, nullptr);
        vTaskDelete(nullptr);
    }, "wifi_detect_task", 2048, &s_pending_event, 5, nullptr);
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

void fsm_main_to_clock(void *arg) {
    ESP_LOGI(TAG, "to clock...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_MianNoPerson);
    lvgl_port_unlock();
}

void fsm_main_set_time(void *arg) {
    ESP_LOGI(TAG, "to set time...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_SetTime);
    lvgl_port_unlock();
}

void fsm_main_menu_turn(void *arg) {
    auto diff = *(int32_t*)arg; // *(bool*)arg 一样
    ESP_LOGI(TAG, "to clock...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_MianNoPerson);
    lvgl_port_unlock();
}