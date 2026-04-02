#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t target_detected;
    float distance_cm;
    float signal_db;
    int32_t range_bin;
    float movement_energy;
    uint8_t presence_detected;
    float presence_confidence;
    float presence_distance_cm;
    float phase_excursion_mm;
    float amplitude_cv;
    float bin_span;
    uint8_t breath_present;
    uint8_t phase_present;
    uint8_t amplitude_present;
    uint8_t bin_present;
    float breath_rate_bpm;      /* 0 = 未检测到 */
    float heart_rate_bpm;       /* 0 = 未检测到 */
    float breath_wave;          /* 实时呼吸波形（IIR 滤波后） */
    float heart_wave;           /* 实时心率波形（IIR 滤波后） */
    uint32_t frame_counter;
} radar_data_t;

int my_lidar_inf_init(void);
void my_lidar_inf_get_data(radar_data_t *out);

#ifdef __cplusplus
}
#endif
