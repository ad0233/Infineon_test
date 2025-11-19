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
enum fm_rtc_conn_state {
    FM_RTC_EXIST,
    FM_RTC_NO_EXIST,
};

enum fm_human_find_conn_state {
    FM_H_F_SUC,
    FM_H_F_FAI,
    FM_H_F_TOUT,
};

enum fsm_clock_need_cfg {
    FM_MEMU_WIFI_SC,
    FM_MEMU_WIFI_FA,
    FM_MEMU_WAKE_MOD,
    FM_MEMU_ALARM_SC,
    FM_MEMU_ALARM_FA,
    FM_MEMU_UNWIND,
    FM_MEMU_VOL,
    FM_MEMU_SC_BR,
    FM_MEMU_SETTIME,
};
uint8_t fm_has_h_fd_state(void); //雷达找人检测
uint8_t fm_has_w_c_state(void);//wifi检测
uint8_t fm_has_rtc_state(void); //RTC检测
uint8_t fm_has_memu_state(void); //菜单状态检测
uint8_t fsm_clock_need_cfg(void);
//开机
void fsm_main_uninit_playing(void *arg, uint8_t last_state, uint8_t next_state);

//找人
void fsm_main_lidar_find_boot(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_lidar_find_playing(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_find_someone(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_no_find_someone(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_enter_findperson(void *arg, uint8_t last_state, uint8_t next_state);

void fsm_main_lidar_clock_update(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_lidar_find(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_lidar_find_suc(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_lidar_find_fail(void *arg, uint8_t last_state, uint8_t next_state);

void fsm_main_wifi_guide(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_wifi_connecting(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_wifi_conn_suc(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_wifi_conn_fail(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_wifi_reconn(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_wifi_forget(void *arg, uint8_t last_state, uint8_t next_state);

void fsm_main_in_offline(void *arg, uint8_t last_state, uint8_t next_state);

void fsm_main_to_clock(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_set_time(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_rtc_adjust_time(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_rtc_save_and_exit(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_wake_mode_next_item(void *arg, uint8_t last_state, uint8_t next_state);

void fsm_main_in_memu(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_menu_next_item(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_in_wifi_sc(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_in_wifi_fa(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_in_wake_mode(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_in_alarm(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_set_alarm_item(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_in_no_alarm(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_in_unwind(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_unwind_next_item(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_in_volume(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_volume_next_item(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_in_light(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_light_next_item(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_in_set_time(void *arg, uint8_t last_state, uint8_t next_state);

void fsm_main_in_boya_data(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_in_radarinfo(void *arg, uint8_t last_state, uint8_t next_state);
void fsm_main_in_GoodMorning_demo(void *arg, uint8_t last_state, uint8_t next_state); 
void fsm_main_in_reminder_tomorrow(void *arg, uint8_t last_state, uint8_t next_state);

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
    F_MAIN_E_TIME,              // 等待事件
    F_MAIN_E_TIMEOUT = 255,     // 超时事件 特殊事件，不能改值，库内部要求，fsm_timeout_trig函数触发该事件
};

enum fsm_main_state_enum {
    F_MAIN_S_UNINIT,              // 未初始化
    F_MAIN_S_UNINIT_PLAYING,       //开机动画
    F_MAIN_S_CLOCK,              // 时钟页面 
    F_MAIN_S_CLOCK_AWAY,        // 时钟页面人走
    F_MAIN_S_CLOCK_BACK,        //时种页面人回来过度（大蝴蝶）
    // 找人
    F_MAIN_S_FINDPERSONC_ANIM,  // 找人动画
    F_MAIN_S_MEMU_FINDPERSONC_ANIM,//后续的找人动画
    F_MAIN_S_FINDPERSON,        // 雷达找人检测
    F_MAIN_S_FINDSUC,           // 找人成功
    F_MAIN_S_FINDFAIL,          // 找人失败
    F_MAIN_S_FINDSUC_ANIM,      // 找人成功动画
    F_MAIN_S_FINDFAIL_ANIM,     // 找人失败动画
    // Wifi连接
    F_MAIN_S_WIFI_DETECT,       //wifi检测
    F_MAIN_S_WIFI_GUIDE,        // 二维码指引连接wifi
    F_MAIN_S_WIFI_CONN,         // wifi连接中
    F_MAIN_S_WIFI_CONN_SUC,     // wifi连接成功
    F_MAIN_S_WIFI_CONN_FAIL,    //  wifi连接失败
    F_MAIN_S_WIFI_OFFLINE,      //wifi离线状态（wifi云）
    F_MAIN_S_MEMU_WIFI_SUC,     //菜单 wifi连接成功
    F_MAIN_S_MEMU_WIFI_FAILE,   // 菜单 wifi连接失败
    // RTC配置
    F_MAIN_S_RTC_DETECT,         // RTC配置检测
    F_MAIN_S_RTC_DETECT_CLK,     // RTC时间
    //菜单
    F_MAIN_S_MENU,               //菜单

    F_MAIN_S_MENU_SLEEP_MODE,   //睡眠模式页面
    F_MAIN_S_MENU_ALARM,        //有闹钟页面
    F_MAIN_S_NO_MENU_ALARM,        //无闹钟页面
    F_MAIN_S_MENU_UNWIND,        //选择歌曲页面
    F_MAIN_S_MENU_VOLUME,        //声音页面
    F_MAIN_S_MENU_BRIGHTNESS,      //亮度页面
    F_MAIN_S_MENU_SET_TIME,        //设置时间页面

    //数据页面
    F_MAIN_S_BOYA_DATA,         //睡眠数据
    F_MAIN_S_RadarInfo,         //雷达数据
    F_MAIN_S_GoodMorning_DEMO,  //早报demo
    F_MAIN_S_REMINDER,          //明天提醒
};

void fsm_main_event_trig(enum fsm_main_event_enum event, void *arg);
uint8_t fsm_main_get_current_state(void);
const char* fsm_main_get_current_state_str(void);

// 超时刷新函数（由主线程定时调用）
void fsm_main_timeout_flush(void);

#ifdef __cplusplus
}
#endif