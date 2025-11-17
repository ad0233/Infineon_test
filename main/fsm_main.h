#pragma once

#include <stdint.h>

#include "fsm_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

int fsm_main_init(void);

enum fm_wifi_conn_state {
    FM_W_N_CFG,
    FM_W_CONN,
    FM_W_SUC,
    FM_W_FAI,
};

enum fm_human_find_conn_state {
    FM_H_F_SUC,
    FM_H_F_FAI,
    FM_H_F_TOUT,
};
uint8_t fm_has_h_fd_state(void); //雷达找人检测
uint8_t fm_has_w_c_state(void);//wifi检测
uint8_t fsm_clock_need_cfg(void);
//开机
void fsm_main_uninit_playing(void *arg);

//找人
void fsm_main_lidar_find_playing(void *arg);
void fsm_main_find_someone(void *arg);
void fsm_main_no_find_someone(void *arg);

void fsm_main_lidar_clock_update(void *arg);
void fsm_main_lidar_find(void *arg);
void fsm_main_lidar_find_suc(void *arg);
void fsm_main_lidar_find_fail(void *arg);

void fsm_main_wifi_guide(void *arg);
void fsm_main_wifi_connecting(void *arg);
void fsm_main_wifi_conn_suc(void *arg);
void fsm_main_wifi_conn_fail(void *arg);
void fsm_main_wifi_reconn(void *arg);
void fsm_main_wifi_forget(void *arg);

void fsm_main_in_offline(void *arg);

void fsm_main_to_clock(void *arg);
void fsm_main_menu_turn(void *arg);

enum fsm_main_event_enum {
    F_MAIN_E_INIT,              // 初始化事件
    F_MAIN_E_ANIM_PLAY_SUC,     //播放结束
    F_MAIN_E_LIDAR_FIND,        // 雷达找到人事件
    F_MAIN_E_LIDAR_UPDATE,      // 雷达数据更新事件
    F_MAIN_E_LIDAR_NOT_FOUND,   // 雷达未找到人事件
    F_MAIN_E_DEV_MOVE,          // 设备移动事件

    F_MAIN_E_WIFI_CMD_TRIG,     // wifi 连接指令触发
    F_MAIN_E_WIFI_C_CONN,      // wifi  连接中
    F_MAIN_E_WIFI_C_SUC,        // wifi 连接成功
    F_MAIN_E_WIFI_C_FAIL,       // wifi 连接失败
    F_MAIN_E_WIFI_C_UNCFG,       // wifi 未配置
    

    F_MAIN_E_RTC_EXIST,        // RTC有（检测到RTC模块存在且正常）
    F_MAIN_E_RTC_NOT_EXIST,     // RTC无（未检测到RTC模块，或模块故障无法识别）

    F_MAIN_E_BTN_CLICKED,       // 点击事件
    F_MAIN_E_BTN_L_CLICKED,     // 长按事件
    F_MAIN_E_KNOB_CW,           // 旋钮事件
    F_MAIN_E_TIME,              // 等待事件（必须在 TIMEOUT 之前，保证值 < 255）
    F_MAIN_E_TIMEOUT = 255,     // 超时事件 特殊事件，不能改值，库内部要求，fsm_timeout_trig函数触发该事件
};
void fsm_main_event_trig(enum fsm_main_event_enum event, void *arg);
uint8_t fsm_main_get_current_state(void);
const char* fsm_main_get_current_state_str(void);

#ifdef __cplusplus
}
#endif