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
uint8_t fm_has_w_c_state(void);
uint8_t fsm_has_wifi_config(void);

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

void fsm_main_to_clock(void *arg);

enum fsm_main_event_enum {
    F_MAIN_E_INIT,
    F_MAIN_E_LIDAR_FIND,
    F_MAIN_E_LIDAR_UPDATE,
    F_MAIN_E_LIDAR_NOT_FOUND,
    F_MAIN_E_DEV_MOVE,
    F_MAIN_E_WIFI_CMD_TRIG,
    F_MAIN_E_WIFI_C_SUC,
    F_MAIN_E_WIFI_C_FAIL,
    F_MAIN_E_BTN_CLICKED,
    F_MAIN_E_BTN_L_CLICKED,
    F_MAIN_E_TIMEOUT = 255, // 特殊事件，不能改值，库内部要求，fsm_timeout_trig函数触发该事件
};
void fsm_main_event_trig(enum fsm_main_event_enum event, void *arg);
uint8_t fsm_main_get_current_state(void);
const char* fsm_main_get_current_state_str(void);

#ifdef __cplusplus
}
#endif