#pragma once

#include <stdint.h>

typedef void (*my_h264_callback_t)(const uint8_t *rgb565_buf, uint32_t rgb565_buf_len, void *context);

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MY_H264_ANIM_BRAND_MOTION2 = 0,
    MY_H264_ANIM_FAIL2,
    MY_H264_ANIM_GO_UP,
    MY_H264_ANIM_HUMAN_RECOGNIZED,
    MY_H264_ANIM_PROCESSING,
    MY_H264_ANIM_SUCCESS2,
} my_h264_animation_t;

void my_h264_init(my_h264_callback_t callback, void *context);

int my_h264_start(my_h264_animation_t animation, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif