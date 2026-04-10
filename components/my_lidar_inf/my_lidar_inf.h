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
    uint8_t rbm_detected;       /* 当前帧体动标记 */
    float rbm_ratio;            /* 窗口内体动帧占比 (0.0-1.0) */
    float rbm_phase_jump;       /* 当前帧相位跳变量 rad */
    float body_movement_mm;     /* 1s 滑窗体动强度 mm */
    uint32_t frame_counter;
} radar_data_t;

/* Presence 校准阈值（运行时可通过 CLI 调节） */
typedef struct {
    float peak_height;       /* 呼吸峰高阈值, 默认 0.03 */
    float phase_exc_mm_th;   /* 相位偏移阈值 mm, 默认 0.08 */
    float amp_cv_th;         /* 振幅 CV 阈值, 默认 0.35 */
    float bin_span_th;       /* bin 跨度阈值, 默认 2.0 */
    float confidence_th;     /* 置信度门限, 默认 0.35 */
    uint32_t miss_limit;     /* 锁存容忍帧数, 默认 3 */
    uint8_t debug_log_enabled; /* 10Hz 子指标日志开关 */
} radar_presence_thresholds_t;

int my_lidar_inf_init(void);
void my_lidar_inf_get_data(radar_data_t *out);
void radar_presence_get_thresholds(radar_presence_thresholds_t *out);
void radar_presence_set_thresholds(const radar_presence_thresholds_t *in);

#ifdef __cplusplus
}
#endif
