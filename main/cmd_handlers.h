#pragma once

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 处理 WiFi 连接命令
 * 
 * @param params JSON params 对象
 * @return int 0成功，-1失败
 */
int cmd_handle_wifi_connect(cJSON *params);

/**
 * @brief 处理忘记 WiFi 命令
 * 
 * @param params JSON params 对象
 * @return int 0成功，-1失败
 */
int cmd_handle_forget_wifi(cJSON *params);

#ifdef __cplusplus
}
#endif

