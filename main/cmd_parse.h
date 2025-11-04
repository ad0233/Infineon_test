#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 解析命令数据
 * 
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return int 0成功，-1失败
 */
int cmd_parse(const uint8_t *data, int len);

#ifdef __cplusplus
}
#endif
