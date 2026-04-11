#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 对外输出数据 ---- */
typedef struct {
    /* 人存检测 */
    uint8_t  detected;          /* 1=有人, 0=无人 */
    float    confidence;        /* 置信度 0.0-1.0 */
    float    distance_cm;       /* 目标距离 cm (无人时=0) */

    /* 生命体征 */
    float    breath_bpm;        /* 呼吸率 BPM (0=未检测) */
    float    heart_bpm;         /* 心率 BPM (0=未检测) */

    /* 体动 */
    uint8_t  rbm;               /* 1=正在体动, 0=静止 */
    float    body_move;         /* 体动强度 0-100% */

    /* 帧信息 */
    uint32_t frame;             /* 帧计数 */
} radar_result_t;

/* ---- 内部调试数据（日志/CLI 用，外部一般不需要） ---- */
typedef struct {
    uint8_t  target_detected;
    float    signal_db;
    int32_t  range_bin;
    float    movement_energy;
    float    phase_excursion_mm;
    float    amplitude_cv;
    float    bin_span;
    uint8_t  breath_present;
    uint8_t  phase_present;
    uint8_t  amplitude_present;
    uint8_t  bin_present;
    float    breath_wave;
    float    heart_wave;
    float    rbm_ratio;
    float    rbm_amp_cv_1s;
    float    rbm_phase_jump;
} radar_debug_t;

/* ---- Presence 校准阈值（CLI 运行时调节） ---- */
typedef struct {
    float    peak_height;
    float    phase_exc_mm_th;
    float    amp_cv_th;
    float    bin_span_th;
    float    confidence_th;
    uint32_t miss_limit;
    uint8_t  debug_log_enabled;
} radar_presence_thresholds_t;

/* ---- API ---- */
int  my_lidar_inf_init(void);
void my_lidar_inf_get_result(radar_result_t *out);
void my_lidar_inf_get_debug(radar_debug_t *out);
void radar_presence_get_thresholds(radar_presence_thresholds_t *out);
void radar_presence_set_thresholds(const radar_presence_thresholds_t *in);

/* 兼容旧接口 */
typedef radar_result_t radar_data_t;
#define my_lidar_inf_get_data(out) my_lidar_inf_get_result(out)

#ifdef __cplusplus
}
#endif
