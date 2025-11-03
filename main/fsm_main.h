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
    FM_W_FAI,
    FM_W_SUC
};
uint8_t fm_has_w_c_state(void);

void fsm_main_lidar_clock_update(void *arg);
void fsm_main_lidar_find(void *arg);
void fsm_main_lidar_find_suc(void *arg);
void fsm_main_lidar_find_fail(void *arg);

void fsm_main_wifi_guide(void *arg);
void fsm_main_wifi_connecting(void *arg);
void fsm_main_wifi_conn_suc(void *arg);
void fsm_main_wifi_conn_fail(void *arg);

#ifdef __cplusplus
}
#endif