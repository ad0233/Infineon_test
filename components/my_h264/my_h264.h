#pragma once

#include <stdint.h>

typedef void (*my_h264_callback_t)(const uint8_t *rgb565_buf, uint32_t rgb565_buf_len, void *context);

#ifdef __cplusplus
extern "C" {
#endif

void my_h264_init(my_h264_callback_t callback, void *context);

int my_h264_start(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif