#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int fsm_main_init(void);

uint8_t fm_has_wifi_config(void);

void fsm_main_do_init(void *arg);

#ifdef __cplusplus
}
#endif