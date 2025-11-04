#include "fsm_main.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "ui.h"

#include "my_ui_behavior.h"
#include "my_nvs.h"

#define TAG "fsm_main"

// ⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠
// ⚠不能在这里的函数使用fsm_event_handle ⚠
// ⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠⚠

uint8_t fm_has_w_c_state(void) {
    // 有连接，就返回1
    if (false) {
        return 1;
    }

    // 正在连接
    if(false) {
        return 2;
    }

    // 已知连接失败
    if(false) {
        return 3;
    }
    return 0;
}

void fsm_main_lidar_clock_update(void *arg) {
    // ESP_LOGI(TAG, "lidar update - clock");
    
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
}

void fsm_main_wifi_forget(void *arg) {
    ESP_LOGI(TAG, "wifi forget and guide...");
    lvgl_port_lock(0);
    lv_disp_load_scr(ui_NetworkBoot);
    lvgl_port_unlock();
}