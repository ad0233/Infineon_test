#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "cli_task.h"
#include "ifx_sensor_dsp.h"
#include "resource_map.h"
#include "xensiv_bgt60trxx_esp.h"

#include "my_lidar_inf.h"
#include "cli_task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "dsps_biquad.h"
#include "dsps_biquad_gen.h"
#include "dsps_fft2r.h"

#define XENSIV_BGT60TRXX_CONF_IMPL
/* Defaults to tr13c, uncomment for utr11 */
//#define DEVICE_UTR11
#ifndef DEVICE_UTR11
    #include "radar_settings_tr13c.h"
#else
    #include "radar_settings_utr11.h"
#endif

/* Defaults to dma for spi, uncomment for FIFO which may have more latency */
//#define USE_FIFO_SPI_TRANSACTION

/*******************************************************************************
* Macros
********************************************************************************/
#define XENSIV_BGT60TRXX_SPI_FREQUENCY      (20000000UL)  /* ESP32-P4 default SPI clock source uses XTAL; 20MHz is the safe upper limit here */

#define RADAR_PROFILE_START_FREQ_HZ         (59000000000ULL)  /* 官方配置 59 GHz */
#define RADAR_PROFILE_BANDWIDTH_HZ          (4000000000ULL)   /* BW=4 GHz (59-63) */
#define RADAR_PROFILE_NUM_SAMPLES_PER_CHIRP (128U)            /* 官方配置 128点 */
#define RADAR_PROFILE_NUM_CHIRPS_PER_FRAME  (64U)
#define RADAR_PROFILE_NUM_RX_ANTENNAS       (3U)
#define RADAR_PROFILE_ADC_DIV               (80U)             /* ADC_DIV=80 -> 1MHz */
#define RADAR_PROFILE_VGA_GAIN_RX1          (5U)              /* VGA=5 -> IF 43dB */

#define RADAR_DISTANCE_THRESHOLD_DB         (-5.0f)           /* 基于实测信号水平调整 */
#define RADAR_DISTANCE_LOG_INTERVAL_MS      (1000U)           /* 综合日志 1Hz */
#define RADAR_IRQ_WAIT_HIGH_TIMEOUT_MS      (250U)
#define RADAR_IRQ_WAIT_LOW_TIMEOUT_MS       (20U)
#define RADAR_MEASUREMENT_REARM_DELAY_MS    (100U)
#define RADAR_DISTANCE_FIRST_VALID_BIN      (10U)             /* bin10=37.5cm, 排除近场杂波 */
#define RADAR_MAIN_RX_IDX                   (1U)              /* 参考脚本 MAIN_RX_IDX=1 */
#define RADAR_RANGE_TRACK_HISTORY_LEN       (5U)
#define RADAR_RANGE_TRACK_LOCAL_RADIUS_BINS (3)
#define RADAR_RANGE_TRACK_SWITCH_RATIO      (1.25f)
#define RADAR_RANGE_TRACK_MAX_STEP_BINS     (2)
#define RADAR_PRESENCE_HISTORY_LEN          (200U)            /* 10Hz * 20s, 足够心率分析 */
#define RADAR_PRESENCE_FRAME_RATE_HZ        (10.0f)
#define RADAR_PRESENCE_ENERGY_RADIUS_BINS   (2)
#define RADAR_PHASE_WINDOW_SECONDS          (6.0f)
#define RADAR_AMP_WINDOW_SECONDS            (5.0f)
#define RADAR_BIN_WINDOW_SECONDS            (5.0f)
#define RADAR_BREATH_WINDOW_SECONDS         (6.0f)
/* Presence 阈值默认值（运行时通过 s_presence_thresholds 覆盖） */
#define RADAR_PRESENCE_DEFAULT_PEAK_HEIGHT      (0.03f)
#define RADAR_PRESENCE_DEFAULT_PHASE_EXC_MM_TH  (0.08f)
#define RADAR_PRESENCE_DEFAULT_AMP_CV_TH        (0.35f)
#define RADAR_PRESENCE_DEFAULT_BIN_SPAN_TH      (2.0f)
#define RADAR_PRESENCE_DEFAULT_CONFIDENCE_TH    (0.80f)
#define RADAR_PRESENCE_DEFAULT_MISS_LIMIT       (3U)
#define RADAR_PHASE_DIFF_CLIP               (0.30f)
#define RADAR_RBM_PHASE_JUMP_TH            (2.0f)            /* 帧间相位跳变 > 2.0 rad 判定为体动（呼吸约 0.3-1.5 rad） */
#define RADAR_RBM_WINDOW_FRAMES            (200U)            /* RBM 统计窗口 = vitals 窗口 */
#define RADAR_RBM_RATIO_TH                 (0.15f)           /* 窗口内 >15% 帧为体动 → 跳过 vitals */
#define RADAR_PHASE_SMOOTH_LEN              (7U)
#define RADAR_WAVELENGTH_MM                 (5.0f)            /* c / 60GHz */
#define RADAR_VITALS_FFT_SIZE               (1024U)           /* 200 samples × 4 零填充 → 对齐 1024 */
#define RADAR_VITALS_INTERVAL_FRAMES        (200U)            /* 每 200 帧 (20s) 做一次 vitals 估算 */
#define RADAR_PI_F                          (3.14159265358979323846f)

/* FIFO 分片读取：BGT60TR13C FIFO 上限 8192 样本，24576 总样本需分 6 片
 * FIFO_SLICE_SAMPLES=4096 对应 FIFO_CREF=2047，与官方寄存器导出一致 */
#define FIFO_SLICE_SAMPLES                  (4096U)
#define NUM_SLICES_PER_FRAME                (NUM_SAMPLES_PER_FRAME / FIFO_SLICE_SAMPLES)

/* Official Arduino distance_measure helper constants */
#define RADAR_FIELD_MASK_24BIT              (0x00FFFFFFUL)
#define RADAR_SFCTL_FIFO_CREF_POS           (0U)
#define RADAR_SFCTL_FIFO_CREF_MSK           (0x001FFFUL)
#define RADAR_ADC0_DIV_POS                  (14U)
#define RADAR_ADC0_DIV_MSK                  (0x003FC000UL)
#define RADAR_PLL1_0_FSU_POS                (0U)
#define RADAR_PLL1_0_FSU_MSK                (0x00FFFFFFUL)
#define RADAR_PLL1_1_RSU_POS                (0U)
#define RADAR_PLL1_1_RSU_MSK                (0x00FFFFFFUL)
#define RADAR_PLL1_2_RTU_POS                (0U)
#define RADAR_PLL1_2_RTU_MSK                (0x00003FFFUL)
#define RADAR_PLL1_3_APU0_POS               (0U)
#define RADAR_PLL1_3_APU0_MSK               (0x00000FFFUL)
#define RADAR_CCR0_CONT_MODE_POS            (9U)
#define RADAR_CCR0_CONT_MODE_MSK            (0x00000200UL)
#define RADAR_CCR1_PD_MODE_POS              (9U)
#define RADAR_CCR1_PD_MODE_MSK              (0x00000600UL)
#define RADAR_CCR1_PD_MODE_IDLE             (1U)
#define RADAR_CSU1_2_VGA_GAIN1_POS          (2U)
#define RADAR_CSU1_2_VGA_GAIN1_MSK          (0x0000001CUL)
#define RADAR_T_SETUP                       (60U)
#define RADAR_PLL_CONVERSION_DEN_KHZ        (640000.0)
#define RADAR_PLL_FIXED_POINT_SCALE         (1048576.0)

#define NUM_SAMPLES_PER_FRAME               (RADAR_PROFILE_NUM_SAMPLES_PER_CHIRP * \
                                             RADAR_PROFILE_NUM_CHIRPS_PER_FRAME * \
                                             RADAR_PROFILE_NUM_RX_ANTENNAS)
#define NUM_CHIRPS_PER_FRAME                RADAR_PROFILE_NUM_CHIRPS_PER_FRAME
#define NUM_SAMPLES_PER_CHIRP               RADAR_PROFILE_NUM_SAMPLES_PER_CHIRP
#define NUM_RX_ANTENNAS                     RADAR_PROFILE_NUM_RX_ANTENNAS
#define RADAR_BANDWIDTH_HZ                  ((float32_t)RADAR_PROFILE_BANDWIDTH_HZ)

/* RTOS tasks */
#define RADAR_TASK_NAME                     "radar_task"
#define RADAR_TASK_STACK_SIZE               (configMINIMAL_STACK_SIZE * 16)
#define RADAR_TASK_PRIORITY                 (configMAX_PRIORITIES - 1)

#define TAG                                 "radar_task"

/*******************************************************************************
* Function Prototypes
********************************************************************************/
static void radar_task(void *pvParameters);

static int32_t init_leds(void);
static int32_t init_sensor(void);
static int32_t radar_apply_distance_profile(xensiv_bgt60trxx_t *dev);
static int32_t radar_update_reg_field(const xensiv_bgt60trxx_t *dev,
                                      uint32_t reg_addr,
                                      uint32_t field_mask,
                                      uint32_t field_pos,
                                      uint32_t value);
static uint32_t radar_calculate_fsu(uint64_t start_freq_hz);
static uint32_t radar_calculate_rtu(uint32_t adc_div, uint32_t samples_per_chirp);
static uint32_t radar_calculate_rsu(uint64_t bandwidth_hz, uint32_t rtu);
static float32_t radar_get_range_bin_length_m(void);
static float32_t radar_calculate_distance_cm_from_bin(int32_t range_bin);
static float32_t radar_calculate_fft_level_db(const cfloat32_t *spectrum, int32_t range_bin);
static bool radar_wait_for_irq_level(int target_level, uint32_t timeout_ms);
static uint32_t radar_get_fstat_reg_addr(void);
static void radar_log_timeout_diagnostics(const char *reason);
static void radar_rearm_next_measurement(void);
static void radar_restart_frame_generator(const char *reason);
static float32_t radar_clampf(float32_t value, float32_t min_value, float32_t max_value);
static bool radar_analyze_frame(void *result_ptr);
static int32_t radar_select_tracked_range_bin(const float32_t *range_energy,
                                              int32_t bin_min,
                                              int32_t bin_max,
                                              int32_t tracked_bin);
static void radar_append_int_history(int32_t *buffer,
                                     uint32_t *count,
                                     uint32_t capacity,
                                     int32_t value);
static int32_t radar_median_int(const int32_t *values, uint32_t count);
static float32_t radar_median_float(const float32_t *values, uint32_t count);
static float32_t radar_std_float(const float32_t *values, uint32_t count);
static float32_t radar_percentile_abs_dev(const float32_t *values,
                                          uint32_t count,
                                          float32_t percentile);
static float32_t radar_unwrap_phase(float32_t raw_phase);
static void radar_process_phase_signal(const float32_t *phase_values,
                                       uint32_t count,
                                       float32_t *processed_out);
static uint32_t radar_count_peaks(const float32_t *values,
                                  uint32_t count,
                                  float32_t threshold,
                                  bool positive_peaks);
static void radar_update_presence(void *result_ptr);
static void radar_vitals_init_filters(void);
static void radar_filtfilt_biquad(float32_t *data, uint32_t len,
                                  float coefs[][5], uint32_t num_stages,
                                  float32_t *temp);
static float32_t radar_estimate_rate_hz(const float32_t *signal, uint32_t len,
                                         float32_t fs,
                                         float32_t freq_min, float32_t freq_max);
static void radar_remove_breath_harmonics(float32_t *heart_sig, uint32_t len,
                                           float32_t breath_hz, float32_t fs);
static void radar_linearize_float(const float32_t *ring, uint32_t ring_len,
                                   uint32_t head, uint32_t total, uint32_t want,
                                   float32_t *out);
static float32_t radar_small_median(const float32_t *arr, uint32_t n);


typedef struct
{
    bool target_detected;
    float32_t distance_cm;
    float32_t signal_db;
    int32_t range_bin;
    float32_t movement_energy;
    float32_t phase_unwrapped;
    float32_t amplitude_metric;
    bool presence_detected;
    float32_t presence_confidence;
    float32_t presence_distance_cm;
    float32_t phase_excursion_mm;
    float32_t amplitude_cv;
    float32_t bin_span;
    bool breath_present;
    bool phase_present;
    bool amplitude_present;
    bool bin_present;
    float32_t breath_rate_bpm;
    float32_t heart_rate_bpm;
    float32_t breath_wave;              /* 实时呼吸波形 */
    float32_t heart_wave;               /* 实时心率波形 */
    bool rbm_detected;                  /* 当前帧体动标记 */
    float32_t rbm_ratio;                /* 窗口内体动帧占比 */
    float32_t rbm_phase_jump;           /* 当前帧相位跳变量 (rad) */
    float32_t body_movement_mm;         /* 1s 滑窗体动强度 (mm) */
} radar_frame_result_t;

static void radar_estimate_vitals(radar_frame_result_t *result);

typedef struct
{
    int32_t search_bin_history[RADAR_RANGE_TRACK_HISTORY_LEN];
    uint32_t search_bin_count;
    float32_t phase_history[RADAR_PRESENCE_HISTORY_LEN];
    float32_t amplitude_history[RADAR_PRESENCE_HISTORY_LEN];
    int32_t bin_history[RADAR_PRESENCE_HISTORY_LEN];
    uint32_t history_count;
    uint32_t history_head;              /* 循环缓冲区写入位置 */
    bool has_unwrapped_phase;
    float32_t last_unwrapped_phase;
    bool presence_latched;
    uint32_t presence_misses;

    /* ---- 生命体征 IIR 滤波器 ---- */
    float breath_bpf_coef[2][5];        /* 呼吸 BPF: 2 级二阶节 */
    float breath_bpf_w[2][2];           /* 延迟线 */
    float heart_bpf_coef[3][5];         /* 心率 BPF: 3 级二阶节 */
    float heart_bpf_w[3][2];            /* 延迟线 */

    /* 频率估算结果中值平滑历史 */
    float breath_hz_history[3];
    float heart_hz_history[5];
    uint32_t breath_hz_count;
    uint32_t heart_hz_count;

    /* 最终输出 BPM */
    float32_t breath_rate_bpm;
    float32_t heart_rate_bpm;
    uint32_t vitals_frame_counter;      /* 用于控制 vitals 计算频率 */
    bool vitals_filters_inited;

    /* 实时波形 IIR 延迟线（单向因果滤波，每帧更新） */
    float rt_breath_w[2][2];            /* 呼吸 BPF 2 级延迟线 */
    float rt_heart_w[3][2];             /* 心率 BPF 3 级延迟线 */
    float32_t rt_breath_wave;           /* 当前帧呼吸波形值 */
    float32_t rt_heart_wave;            /* 当前帧心率波形值 */

    /* ---- 体动检测 (RBM) ---- */
    uint8_t rbm_flags[RADAR_RBM_WINDOW_FRAMES]; /* 环形缓冲: 1=体动, 0=正常 */
    uint32_t rbm_sum;                   /* 窗口内体动帧计数（避免每次遍历） */

    /* ---- 自适应基线体动 ---- */
    float32_t baseline[NUM_SAMPLES_PER_CHIRP / 2U]; /* 背景频谱基线 */
    bool baseline_inited;               /* 基线是否已初始化 */
    float32_t body_move_raw;            /* 当前帧前向体动原始值 */
    float32_t body_move_smooth;         /* 10帧平滑后的体动值 */
    float32_t body_move_buf[10];        /* 10 帧体动原始值环形缓冲 */
    uint32_t body_move_idx;
    uint32_t body_move_count;
    float32_t noise_floor;              /* 静止时的噪声底（自动学习） */
} radar_presence_state_t;


/*******************************************************************************
* Global Variables
********************************************************************************/
static spi_device_handle_t spi_obj;
static xensiv_bgt60trxx_esp_t bgt60_obj;

#ifdef USE_FIFO_SPI_TRANSACTION
    static uint16_t bgt60_buffer[NUM_SAMPLES_PER_FRAME];
#else
    static DMA_ATTR uint16_t bgt60_buffer[NUM_SAMPLES_PER_FRAME];
#endif

/* Scratch buffers for per-chirp FFT and frame-level coherent accumulation */
static float32_t chirp_distance_frame[NUM_SAMPLES_PER_CHIRP];
static float32_t distance_window[NUM_SAMPLES_PER_CHIRP];
static cfloat32_t distance_range_fft[NUM_SAMPLES_PER_CHIRP / 2U];
static float32_t range_energy_accum[NUM_SAMPLES_PER_CHIRP / 2U];
static cfloat32_t coherent_range_accum[NUM_SAMPLES_PER_CHIRP / 2U];
static cfloat32_t coherent_range_avg[NUM_SAMPLES_PER_CHIRP / 2U];

/* 生命体征 FFT 工作缓冲区（1024 点复数 = 2048 float = 8KB，静态分配） */
static float32_t vitals_fft_buf[RADAR_VITALS_FFT_SIZE * 2U];

/* Presence 校准阈值（运行时可通过 CLI 调节，typedef 在 my_lidar_inf.h） */
static radar_presence_thresholds_t s_presence_thresholds = {
    .peak_height       = RADAR_PRESENCE_DEFAULT_PEAK_HEIGHT,
    .phase_exc_mm_th   = RADAR_PRESENCE_DEFAULT_PHASE_EXC_MM_TH,
    .amp_cv_th         = RADAR_PRESENCE_DEFAULT_AMP_CV_TH,
    .bin_span_th       = RADAR_PRESENCE_DEFAULT_BIN_SPAN_TH,
    .confidence_th     = 0.80f,
    .miss_limit        = RADAR_PRESENCE_DEFAULT_MISS_LIMIT,
    .debug_log_enabled = false,
};

static TaskHandle_t radar_task_handler;
static uint32_t radar_frame_counter = 0;
static uint32_t radar_fifo_error_counter = 0;
static uint32_t radar_fifo_consecutive_errors = 0;
static uint32_t radar_irq_timeout_counter = 0;
static uint32_t radar_distance_last_log_ms = 0;


/* ---- 导出数据（受临界区保护） ---- */
static radar_data_t s_radar_data;
static portMUX_TYPE s_radar_data_mux = portMUX_INITIALIZER_UNLOCKED;
static radar_presence_state_t s_presence_state;


void my_lidar_inf_get_data(radar_data_t *out)
{
    if (out == NULL) return;
    taskENTER_CRITICAL(&s_radar_data_mux);
    *out = s_radar_data;
    taskEXIT_CRITICAL(&s_radar_data_mux);
}

void radar_presence_get_thresholds(radar_presence_thresholds_t *out)
{
    if (out == NULL) { return; }
    out->peak_height       = s_presence_thresholds.peak_height;
    out->phase_exc_mm_th   = s_presence_thresholds.phase_exc_mm_th;
    out->amp_cv_th         = s_presence_thresholds.amp_cv_th;
    out->bin_span_th       = s_presence_thresholds.bin_span_th;
    out->confidence_th     = s_presence_thresholds.confidence_th;
    out->miss_limit        = s_presence_thresholds.miss_limit;
    out->debug_log_enabled = s_presence_thresholds.debug_log_enabled;
}

void radar_presence_set_thresholds(const radar_presence_thresholds_t *in)
{
    if (in == NULL) { return; }
    s_presence_thresholds.peak_height       = in->peak_height;
    s_presence_thresholds.phase_exc_mm_th   = in->phase_exc_mm_th;
    s_presence_thresholds.amp_cv_th         = in->amp_cv_th;
    s_presence_thresholds.bin_span_th       = in->bin_span_th;
    s_presence_thresholds.confidence_th     = in->confidence_th;
    s_presence_thresholds.miss_limit        = in->miss_limit;
    s_presence_thresholds.debug_log_enabled = in->debug_log_enabled;
}

int my_lidar_inf_init(void)
{
    if (xTaskCreatePinnedToCore(radar_task,
                                RADAR_TASK_NAME,
                                RADAR_TASK_STACK_SIZE,
                                NULL,
                                RADAR_TASK_PRIORITY,
                                &radar_task_handler,
                                0) != pdPASS)
    {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore failed");
        return -1;
    }

    return 0;
}

/*******************************************************************************
* Function Name: radar_task
********************************************************************************/
static void radar_task(void *pvParameters)
{
    (void)pvParameters;

    if (init_sensor() != 0)
    {
        ESP_LOGE(TAG, "Sensor init failed. Check wiring and pin definitions.");
        vTaskDelete(NULL);
        return;
    }

    if (init_leds() != 0)
    {
        ESP_LOGE(TAG, "LED init failed.");
        vTaskDelete(NULL);
        return;
    }

    ifx_window_hann_f32(distance_window, NUM_SAMPLES_PER_CHIRP);

    if (xensiv_bgt60trxx_start_frame(&bgt60_obj.dev, true) != XENSIV_BGT60TRXX_STATUS_OK)
    {
        ESP_LOGE(TAG, "start_frame failed");
        vTaskDelete(NULL);
        return;
    }

    gpio_set_level(PIN_LED_RED, 0);
    gpio_set_level(PIN_LED_GREEN, 1);
    ESP_LOGI(TAG,
             "Radar distance/presence measurement started. bin_length=%.3fm rearm_delay=%ums",
             radar_get_range_bin_length_m(),
             (unsigned)RADAR_MEASUREMENT_REARM_DELAY_MS);
    ESP_LOGI(TAG,
             "Presence reference: main_rx=%u track_hist=%u radius=%d switch=%.2f max_step=%d "
             "phase>=%.2fmm amp_cv<=%.2f bin_span<=%.1f",
             (unsigned)RADAR_MAIN_RX_IDX,
             (unsigned)RADAR_RANGE_TRACK_HISTORY_LEN,
             RADAR_RANGE_TRACK_LOCAL_RADIUS_BINS,
             RADAR_RANGE_TRACK_SWITCH_RATIO,
             RADAR_RANGE_TRACK_MAX_STEP_BINS,
             s_presence_thresholds.phase_exc_mm_th,
             s_presence_thresholds.amp_cv_th,
             s_presence_thresholds.bin_span_th);

    for (;;)
    {
        /* 分片读取：每帧 = NUM_SLICES_PER_FRAME 片 × FIFO_SLICE_SAMPLES 样本
         * 每片等待 IRQ 拉高后读取，避免超出 BGT60TR13C 硬件 FIFO 上限 8192 */
        bool frame_ok = true;
        for (uint32_t slice = 0U; slice < NUM_SLICES_PER_FRAME; slice++)
        {
            if (!radar_wait_for_irq_level(1, RADAR_IRQ_WAIT_HIGH_TIMEOUT_MS))
            {
                radar_irq_timeout_counter++;
                radar_log_timeout_diagnostics("IRQ did not assert for slice");
                ESP_LOGW(TAG,
                         "IRQ wait high timeout slice=%" PRIu32 " (%" PRIu32 " total). "
                         "Restarting frame generator",
                         slice, radar_irq_timeout_counter);
                radar_restart_frame_generator("IRQ slice timeout");
                frame_ok = false;
                break;
            }

            int32_t fifo_status = xensiv_bgt60trxx_get_fifo_data(
                &bgt60_obj.dev,
                bgt60_buffer + slice * FIFO_SLICE_SAMPLES,
                FIFO_SLICE_SAMPLES);

            if (fifo_status != XENSIV_BGT60TRXX_STATUS_OK)
            {
                uint32_t fifo_hw_status = 0;
                int32_t fifo_hw_status_rslt =
                    xensiv_bgt60trxx_get_fifo_status(&bgt60_obj.dev, &fifo_hw_status);

                radar_fifo_error_counter++;
                radar_fifo_consecutive_errors++;
                ESP_LOGW(TAG,
                         "get_fifo_data failed slice=%" PRIu32
                         " (%" PRIu32 " total, %" PRIu32 " consecutive), "
                         "status=%" PRIi32 ", fstat_rslt=%" PRIi32 ", fstat=0x%06" PRIX32,
                         slice,
                         radar_fifo_error_counter,
                         radar_fifo_consecutive_errors,
                         fifo_status,
                         fifo_hw_status_rslt,
                         fifo_hw_status);

                if (radar_fifo_consecutive_errors >= 3U)
                {
                    radar_restart_frame_generator("Repeated FIFO errors");
                    radar_fifo_consecutive_errors = 0;
                }
                frame_ok = false;
                break;
            }

            if (!radar_wait_for_irq_level(0, RADAR_IRQ_WAIT_LOW_TIMEOUT_MS))
            {
                ESP_LOGW(TAG,
                         "IRQ remained high after slice=%" PRIu32 " read; continuing",
                         slice);
            }
        }

        if (!frame_ok)
        {
            continue;
        }

        radar_fifo_consecutive_errors = 0;

        radar_frame_counter++;
        if ((radar_frame_counter % 32U) == 0U)
        {
            ESP_LOGI(TAG, "Radar frames processed: %" PRIu32, radar_frame_counter);
        }

        uint32_t time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        radar_frame_result_t frame_result = {0};
        if (!radar_analyze_frame(&frame_result))
        {
            radar_restart_frame_generator("Frame analysis failed");
            continue;
        }

        gpio_set_level(PIN_LED_RED, frame_result.target_detected ? 1 : 0);
        gpio_set_level(PIN_LED_GREEN, frame_result.target_detected ? 0 : 1);

        radar_data_t export_snapshot;
        taskENTER_CRITICAL(&s_radar_data_mux);
        s_radar_data.target_detected = frame_result.target_detected ? 1U : 0U;
        s_radar_data.distance_cm = frame_result.distance_cm;
        s_radar_data.signal_db = frame_result.signal_db;
        s_radar_data.range_bin = frame_result.range_bin;
        s_radar_data.movement_energy = frame_result.movement_energy;
        s_radar_data.presence_detected = frame_result.presence_detected ? 1U : 0U;
        s_radar_data.presence_confidence = frame_result.presence_confidence;
        s_radar_data.presence_distance_cm = frame_result.presence_distance_cm;
        s_radar_data.phase_excursion_mm = frame_result.phase_excursion_mm;
        s_radar_data.amplitude_cv = frame_result.amplitude_cv;
        s_radar_data.bin_span = frame_result.bin_span;
        s_radar_data.breath_present = frame_result.breath_present ? 1U : 0U;
        s_radar_data.phase_present = frame_result.phase_present ? 1U : 0U;
        s_radar_data.amplitude_present = frame_result.amplitude_present ? 1U : 0U;
        s_radar_data.bin_present = frame_result.bin_present ? 1U : 0U;
        s_radar_data.breath_rate_bpm = frame_result.breath_rate_bpm;
        s_radar_data.heart_rate_bpm = frame_result.heart_rate_bpm;
        s_radar_data.breath_wave = frame_result.breath_wave;
        s_radar_data.heart_wave = frame_result.heart_wave;
        s_radar_data.rbm_detected = frame_result.rbm_detected ? 1U : 0U;
        s_radar_data.rbm_ratio = frame_result.rbm_ratio;
        s_radar_data.rbm_phase_jump = frame_result.rbm_phase_jump;
        s_radar_data.body_movement_mm = frame_result.body_movement_mm;
        s_radar_data.frame_counter = radar_frame_counter;
        export_snapshot = s_radar_data;
        taskEXIT_CRITICAL(&s_radar_data_mux);

        /* 波形日志：关闭（校准 presence 时不需要，需要时取消注释） */
        /* ESP_LOGI(TAG, "Wave: breath=%.6f heart=%.6f frame=%" PRIu32,
                 export_snapshot.breath_wave,
                 export_snapshot.heart_wave,
                 export_snapshot.frame_counter); */

        /* 综合日志：1Hz */
        if ((radar_distance_last_log_ms == 0U) ||
            ((time_ms - radar_distance_last_log_ms) >= RADAR_DISTANCE_LOG_INTERVAL_MS))
        {
            ESP_LOGI(TAG,
                     "Radar: detected=%s bin=%" PRIi32 " level=%.1fdB movement=%.3f "
                     "confidence=%.2f distance=%.1fcm "
                     "breath=%.1fbpm heart=%.1fbpm frame=%" PRIu32
                     " phExc=%.3fmm(%s) ampCV=%.3f(%s) binSpan=%.1f(%s) breathPk=%s"
                     " rbm=%s(%.0f%%) bodyMov=%.4f raw=%.4f noise=%.4f",
                     (export_snapshot.presence_detected != 0U) ? "yes" : "no",
                     export_snapshot.range_bin,
                     export_snapshot.signal_db,
                     export_snapshot.movement_energy,
                     export_snapshot.presence_confidence,
                     export_snapshot.presence_distance_cm,
                     export_snapshot.breath_rate_bpm,
                     export_snapshot.heart_rate_bpm,
                     export_snapshot.frame_counter,
                     export_snapshot.phase_excursion_mm,
                     export_snapshot.phase_present ? "Y" : "N",
                     export_snapshot.amplitude_cv,
                     export_snapshot.amplitude_present ? "Y" : "N",
                     export_snapshot.bin_span,
                     export_snapshot.bin_present ? "Y" : "N",
                     export_snapshot.breath_present ? "Y" : "N",
                     export_snapshot.rbm_detected ? "Y" : "N",
                     export_snapshot.rbm_ratio * 100.0f,
                     export_snapshot.body_movement_mm,
                     export_snapshot.rbm_phase_jump,
                     s_presence_state.noise_floor);
            radar_distance_last_log_ms = time_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(RADAR_MEASUREMENT_REARM_DELAY_MS));
        radar_rearm_next_measurement();
    }
}


#if 0
/*******************************************************************************
* Function Name: presence_detection_cb
********************************************************************************/
void presence_detection_cb(xensiv_radar_presence_handle_t handle,
                           const xensiv_radar_presence_event_t* event,
                           void *data)
{
    (void)data;

    float presence_distance_cm = 0.0f;
    bool is_macro_presence = false;
    bool is_micro_presence = false;

    switch (event->state)
    {
        case XENSIV_RADAR_PRESENCE_STATE_MACRO_PRESENCE:
            is_macro_presence = true;
            presence_distance_cm = (float)event->range_bin *
                                   radar_get_range_bin_length_m() * 100.0f;
            gpio_set_level(PIN_LED_RED, 1);
            gpio_set_level(PIN_LED_GREEN, 0);
            break;

        case XENSIV_RADAR_PRESENCE_STATE_MICRO_PRESENCE:
            is_micro_presence = true;
            presence_distance_cm = (float)event->range_bin *
                                   radar_get_range_bin_length_m() * 100.0f;
            gpio_set_level(PIN_LED_RED, 1);
            gpio_set_level(PIN_LED_GREEN, 0);
            break;

        case XENSIV_RADAR_PRESENCE_STATE_ABSENCE:
        default:
            presence_distance_cm = 0.0f;
            gpio_set_level(PIN_LED_RED, 0);
            gpio_set_level(PIN_LED_GREEN, 1);
            break;
    }

    /* 获取算法内部的宏/微运动功率用于置信度计算 */
    float macro_power = 0.0f;
    float micro_power = 0.0f;
    int   macro_idx   = 0;
    int   micro_idx   = 0;
    xensiv_radar_presence_get_max_macro(handle, &macro_power, &macro_idx);
    xensiv_radar_presence_get_max_micro(handle, &micro_power, &micro_idx);

    xensiv_radar_presence_config_t cfg;
    xensiv_radar_presence_get_config(handle, &cfg);

    float score = 0.0f;
    if (is_macro_presence)
    {
        score = macro_power / cfg.macro_threshold;
        if (score > 1.0f) score = 1.0f;
    }
    else if (is_micro_presence)
    {
        score = (micro_power / cfg.micro_threshold) * 0.5f;
        if (score > 0.5f) score = 0.5f;
    }

    if (is_macro_presence)
    {
        ESP_LOGI(TAG, "[MACRO PRESENCE] range_bin=%" PRIi32 " (%.1fcm) confidence=%.0f%% time=%" PRIi32 "ms",
                 event->range_bin, presence_distance_cm, score * 100.0f, event->timestamp);
    }
    else if (is_micro_presence)
    {
        ESP_LOGI(TAG, "[MICRO PRESENCE] range_bin=%" PRIi32 " (%.1fcm) confidence=%.0f%% time=%" PRIi32 "ms",
                 event->range_bin, presence_distance_cm, score * 100.0f, event->timestamp);
    }
    else
    {
        ESP_LOGI(TAG, "[ABSENCE] time=%" PRIu32 "ms", event->timestamp);
    }

}
#endif


/*******************************************************************************
* Function Name: init_sensor
********************************************************************************/
static int32_t init_sensor(void)
{
#ifdef USE_FIFO_SPI_TRANSACTION
    bool use_dma = false;
#else
    bool use_dma = true;
#endif

    gpio_hold_dis(PIN_XENSIV_BGT60TRXX_SPI_CSN);
    gpio_hold_dis(PIN_XENSIV_BGT60TRXX_SPI_SCLK);
    gpio_hold_dis(PIN_XENSIV_BGT60TRXX_SPI_MOSI);
    gpio_hold_dis(PIN_XENSIV_BGT60TRXX_SPI_MISO);
    gpio_hold_dis(PIN_XENSIV_BGT60TRXX_IRQ);

    gpio_set_pull_mode(PIN_XENSIV_BGT60TRXX_SPI_MISO, GPIO_FLOATING);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_XENSIV_BGT60TRXX_SPI_MOSI,
        .miso_io_num = PIN_XENSIV_BGT60TRXX_SPI_MISO,
        .sclk_io_num = PIN_XENSIV_BGT60TRXX_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = NUM_SAMPLES_PER_FRAME * 2
    };

    if (spi_bus_initialize(SPI3_HOST, &bus_cfg,
                           use_dma ? SPI_DMA_CH_AUTO : SPI_DMA_DISABLED) != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_initialize failed");
        return -1;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = XENSIV_BGT60TRXX_SPI_FREQUENCY,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
        .pre_cb = NULL,
        .post_cb = NULL
    };

    if (spi_bus_add_device(SPI3_HOST, &dev_cfg, &spi_obj) != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_add_device failed");
        return -1;
    }

    gpio_config_t radar_power_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PIN_XENSIV_BGT60TRXX_LDO_EN),
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    if (gpio_config(&radar_power_conf) != ESP_OK)
    {
        ESP_LOGE(TAG, "LDO_EN gpio_config failed");
        return -1;
    }
    gpio_set_level(PIN_XENSIV_BGT60TRXX_LDO_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    if (xensiv_bgt60trxx_esp_init(&bgt60_obj,
                                  spi_obj,
                                  PIN_XENSIV_BGT60TRXX_SPI_CSN,
                                  PIN_XENSIV_BGT60TRXX_RSTN,
                                  use_dma,
                                  register_list,
                                  XENSIV_BGT60TRXX_CONF_NUM_REGS) != ESP_OK)
    {
        ESP_LOGE(TAG, "xensiv_bgt60trxx_esp_init failed");
        return -1;
    }

    if (radar_apply_distance_profile(&bgt60_obj.dev) != XENSIV_BGT60TRXX_STATUS_OK)
    {
        ESP_LOGE(TAG, "radar_apply_distance_profile failed");
        return -1;
    }

    gpio_config_t irq_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIN_XENSIV_BGT60TRXX_IRQ),
        .pull_down_en = 1,
        .pull_up_en = 0
    };
    if (gpio_config(&irq_conf) != ESP_OK)
    {
        ESP_LOGE(TAG, "IRQ gpio_config failed");
        return -1;
    }

    /* 使用分片大小设置 FIFO 阈值：每片 4096 样本，对应 FIFO_CREF=2047
     * BGT60TR13C FIFO 上限 8192，不能直接传入整帧 24576 */
    if (xensiv_bgt60trxx_set_fifo_limit(&bgt60_obj.dev, FIFO_SLICE_SAMPLES) !=
        XENSIV_BGT60TRXX_STATUS_OK)
    {
        ESP_LOGE(TAG, "set_fifo_limit failed");
        return -1;
    }

    ESP_LOGI(TAG, "Sensor initialized OK");
    return 0;
}


/*******************************************************************************
* Function Name: radar_update_reg_field
********************************************************************************/
static int32_t radar_update_reg_field(const xensiv_bgt60trxx_t *dev,
                                      uint32_t reg_addr,
                                      uint32_t field_mask,
                                      uint32_t field_pos,
                                      uint32_t value)
{
    uint32_t reg_data = 0;
    int32_t status = xensiv_bgt60trxx_get_reg(dev, reg_addr, &reg_data);

    if (status != XENSIV_BGT60TRXX_STATUS_OK)
    {
        return status;
    }

    reg_data &= ~field_mask;
    reg_data |= ((value << field_pos) & field_mask);
    return xensiv_bgt60trxx_set_reg(dev, reg_addr, reg_data);
}


/*******************************************************************************
* Function Name: radar_calculate_fsu
********************************************************************************/
static uint32_t radar_calculate_fsu(uint64_t start_freq_hz)
{
    const double start_freq_khz = (double)start_freq_hz / 1000.0;
    const int32_t fsu_signed = (int32_t)(RADAR_PLL_FIXED_POINT_SCALE *
                                         ((start_freq_khz / RADAR_PLL_CONVERSION_DEN_KHZ) - 96.0));
    return ((uint32_t)fsu_signed) & RADAR_FIELD_MASK_24BIT;
}


/*******************************************************************************
* Function Name: radar_calculate_rtu
********************************************************************************/
static uint32_t radar_calculate_rtu(uint32_t adc_div, uint32_t samples_per_chirp)
{
    return ((adc_div * samples_per_chirp) / 8U) + RADAR_T_SETUP;
}


/*******************************************************************************
* Function Name: radar_calculate_rsu
********************************************************************************/
static uint32_t radar_calculate_rsu(uint64_t bandwidth_hz, uint32_t rtu)
{
    const double bandwidth_khz = (double)bandwidth_hz / 1000.0;
    const double chirp_step_khz = bandwidth_khz / (8.0 * (double)rtu);
    return ((uint32_t)(RADAR_PLL_FIXED_POINT_SCALE * chirp_step_khz /
                       RADAR_PLL_CONVERSION_DEN_KHZ)) & RADAR_FIELD_MASK_24BIT;
}


/*******************************************************************************
* Function Name: radar_get_range_bin_length_m
********************************************************************************/
static float32_t radar_get_range_bin_length_m(void)
{
    return (float32_t)(IFX_LIGHT_SPEED_M_S / (2.0 * (double)RADAR_PROFILE_BANDWIDTH_HZ));
}


/*******************************************************************************
* Function Name: radar_calculate_distance_cm_from_bin
********************************************************************************/
static float32_t radar_calculate_distance_cm_from_bin(int32_t range_bin)
{
    return (float32_t)range_bin * radar_get_range_bin_length_m() * 100.0f;
}


/*******************************************************************************
* Function Name: radar_apply_distance_profile
********************************************************************************/
static int32_t radar_apply_distance_profile(xensiv_bgt60trxx_t *dev)
{
    const uint32_t fsu = radar_calculate_fsu(RADAR_PROFILE_START_FREQ_HZ);
    const uint32_t rtu = radar_calculate_rtu(RADAR_PROFILE_ADC_DIV, NUM_SAMPLES_PER_CHIRP);
    const uint32_t rsu = radar_calculate_rsu(RADAR_PROFILE_BANDWIDTH_HZ, rtu);
    const uint32_t fifo_cref = (NUM_SAMPLES_PER_FRAME / 2U) - 1U;
    int32_t status = XENSIV_BGT60TRXX_STATUS_OK;

    status = radar_update_reg_field(dev,
                                    XENSIV_BGT60TRXX_REG_ADC0,
                                    RADAR_ADC0_DIV_MSK,
                                    RADAR_ADC0_DIV_POS,
                                    RADAR_PROFILE_ADC_DIV);
    if (status == XENSIV_BGT60TRXX_STATUS_OK)
    {
        status = radar_update_reg_field(dev,
                                        XENSIV_BGT60TRXX_REG_PLL1_0,
                                        RADAR_PLL1_0_FSU_MSK,
                                        RADAR_PLL1_0_FSU_POS,
                                        fsu);
    }
    if (status == XENSIV_BGT60TRXX_STATUS_OK)
    {
        status = radar_update_reg_field(dev,
                                        XENSIV_BGT60TRXX_REG_PLL1_1,
                                        RADAR_PLL1_1_RSU_MSK,
                                        RADAR_PLL1_1_RSU_POS,
                                        rsu);
    }
    if (status == XENSIV_BGT60TRXX_STATUS_OK)
    {
        status = radar_update_reg_field(dev,
                                        XENSIV_BGT60TRXX_REG_PLL1_2,
                                        RADAR_PLL1_2_RTU_MSK,
                                        RADAR_PLL1_2_RTU_POS,
                                        rtu);
    }
    if (status == XENSIV_BGT60TRXX_STATUS_OK)
    {
        status = radar_update_reg_field(dev,
                                        XENSIV_BGT60TRXX_REG_PLL1_3,
                                        RADAR_PLL1_3_APU0_MSK,
                                        RADAR_PLL1_3_APU0_POS,
                                        NUM_SAMPLES_PER_CHIRP);
    }
    if (status == XENSIV_BGT60TRXX_STATUS_OK)
    {
        status = radar_update_reg_field(dev,
                                        XENSIV_BGT60TRXX_REG_CSU1_2,
                                        RADAR_CSU1_2_VGA_GAIN1_MSK,
                                        RADAR_CSU1_2_VGA_GAIN1_POS,
                                        RADAR_PROFILE_VGA_GAIN_RX1);
    }
    if (status == XENSIV_BGT60TRXX_STATUS_OK)
    {
        status = radar_update_reg_field(dev,
                                        XENSIV_BGT60TRXX_REG_CCR0,
                                        RADAR_CCR0_CONT_MODE_MSK,
                                        RADAR_CCR0_CONT_MODE_POS,
                                        1U);
    }
    if (status == XENSIV_BGT60TRXX_STATUS_OK)
    {
        status = radar_update_reg_field(dev,
                                        XENSIV_BGT60TRXX_REG_CCR1,
                                        RADAR_CCR1_PD_MODE_MSK,
                                        RADAR_CCR1_PD_MODE_POS,
                                        RADAR_CCR1_PD_MODE_IDLE);
    }
    if (status == XENSIV_BGT60TRXX_STATUS_OK)
    {
        status = radar_update_reg_field(dev,
                                        XENSIV_BGT60TRXX_REG_SFCTL,
                                        RADAR_SFCTL_FIFO_CREF_MSK,
                                        RADAR_SFCTL_FIFO_CREF_POS,
                                        fifo_cref);
    }

    if (status == XENSIV_BGT60TRXX_STATUS_OK)
    {
        ESP_LOGI(TAG,
                 "Applied distance profile: start=%.3fGHz bandwidth=%.3fGHz samples=%u chirps=%u rx=%u",
                 (double)RADAR_PROFILE_START_FREQ_HZ / 1.0e9,
                 (double)RADAR_PROFILE_BANDWIDTH_HZ / 1.0e9,
                 (unsigned)NUM_SAMPLES_PER_CHIRP,
                 (unsigned)NUM_CHIRPS_PER_FRAME,
                 (unsigned)NUM_RX_ANTENNAS);
        ESP_LOGI(TAG,
                 "Profile registers: FSU=0x%06" PRIX32 " RTU=%" PRIu32 " RSU=0x%06" PRIX32
                 " ADC_DIV=%u VGA_RX1=%u FIFO_CREF=%" PRIu32,
                 fsu,
                 rtu,
                 rsu,
                 (unsigned)RADAR_PROFILE_ADC_DIV,
                 (unsigned)RADAR_PROFILE_VGA_GAIN_RX1,
                 fifo_cref);
        ESP_LOGI(TAG,
                 "Range bin length=%.3fm threshold=%.2fdB first_valid_bin=%u frame_samples=%u",
                 radar_get_range_bin_length_m(),
                 RADAR_DISTANCE_THRESHOLD_DB,
                 (unsigned)RADAR_DISTANCE_FIRST_VALID_BIN,
                 (unsigned)NUM_SAMPLES_PER_FRAME);
        ESP_LOGI(TAG, "Frame control: CCR0.CONT_MODE=1 CCR1.PD_MODE=IDLE");
    }

    return status;
}


/*******************************************************************************
* Function Name: radar_calculate_fft_level_db
********************************************************************************/
static float32_t radar_calculate_fft_level_db(const cfloat32_t *spectrum, int32_t range_bin)
{
    float32_t real = CREAL_F32(spectrum[range_bin]);
    float32_t imag = CIMAG_F32(spectrum[range_bin]);
    float32_t magnitude = sqrtf((real * real) + (imag * imag));

    if (magnitude < 0.001f)
    {
        magnitude = 0.001f;
    }

    return 10.0f * log10f(magnitude);
}


/*******************************************************************************
* Function Name: radar_append_int_history
********************************************************************************/
static void radar_append_int_history(int32_t *buffer,
                                     uint32_t *count,
                                     uint32_t capacity,
                                     int32_t value)
{
    if ((buffer == NULL) || (count == NULL) || (capacity == 0U))
    {
        return;
    }

    if (*count < capacity)
    {
        buffer[*count] = value;
        (*count)++;
        return;
    }

    memmove(buffer, buffer + 1, sizeof(int32_t) * (capacity - 1U));
    buffer[capacity - 1U] = value;
}


/*******************************************************************************
* Function Name: radar_median_float
********************************************************************************/
static float32_t radar_median_float(const float32_t *values, uint32_t count)
{
    if ((values == NULL) || (count == 0U))
    {
        return 0.0f;
    }

    float32_t scratch[RADAR_PRESENCE_HISTORY_LEN];
    for (uint32_t i = 0; i < count; i++)
    {
        scratch[i] = values[i];
    }

    for (uint32_t i = 1; i < count; i++)
    {
        const float32_t key = scratch[i];
        int32_t j = (int32_t)i - 1;
        while ((j >= 0) && (scratch[j] > key))
        {
            scratch[j + 1] = scratch[j];
            j--;
        }
        scratch[j + 1] = key;
    }

    if ((count & 1U) != 0U)
    {
        return scratch[count / 2U];
    }

    return 0.5f * (scratch[(count / 2U) - 1U] + scratch[count / 2U]);
}


/*******************************************************************************
* Function Name: radar_median_int
********************************************************************************/
static int32_t radar_median_int(const int32_t *values, uint32_t count)
{
    if ((values == NULL) || (count == 0U))
    {
        return -1;
    }

    int32_t scratch[RADAR_PRESENCE_HISTORY_LEN];
    for (uint32_t i = 0; i < count; i++)
    {
        scratch[i] = values[i];
    }

    for (uint32_t i = 1; i < count; i++)
    {
        const int32_t key = scratch[i];
        int32_t j = (int32_t)i - 1;
        while ((j >= 0) && (scratch[j] > key))
        {
            scratch[j + 1] = scratch[j];
            j--;
        }
        scratch[j + 1] = key;
    }

    if ((count & 1U) != 0U)
    {
        return scratch[count / 2U];
    }

    return (scratch[(count / 2U) - 1U] + scratch[count / 2U]) / 2;
}


/*******************************************************************************
* Function Name: radar_std_float
********************************************************************************/
static float32_t radar_std_float(const float32_t *values, uint32_t count)
{
    if ((values == NULL) || (count < 2U))
    {
        return 0.0f;
    }

    float32_t sum = 0.0f;
    for (uint32_t i = 0; i < count; i++)
    {
        sum += values[i];
    }
    const float32_t mean = sum / (float32_t)count;

    float32_t sq_sum = 0.0f;
    for (uint32_t i = 0; i < count; i++)
    {
        const float32_t diff = values[i] - mean;
        sq_sum += diff * diff;
    }

    return sqrtf(sq_sum / (float32_t)count);
}


/*******************************************************************************
* Function Name: radar_percentile_abs_dev
********************************************************************************/
static float32_t radar_percentile_abs_dev(const float32_t *values,
                                          uint32_t count,
                                          float32_t percentile)
{
    if ((values == NULL) || (count == 0U))
    {
        return 0.0f;
    }

    const float32_t median = radar_median_float(values, count);
    float32_t scratch[RADAR_PRESENCE_HISTORY_LEN];
    for (uint32_t i = 0; i < count; i++)
    {
        scratch[i] = fabsf(values[i] - median);
    }

    for (uint32_t i = 1; i < count; i++)
    {
        const float32_t key = scratch[i];
        int32_t j = (int32_t)i - 1;
        while ((j >= 0) && (scratch[j] > key))
        {
            scratch[j + 1] = scratch[j];
            j--;
        }
        scratch[j + 1] = key;
    }

    const float32_t clipped_percentile = radar_clampf(percentile, 0.0f, 100.0f);
    uint32_t index = (uint32_t)(((clipped_percentile / 100.0f) * (float32_t)(count - 1U)) + 0.5f);
    if (index >= count)
    {
        index = count - 1U;
    }

    return scratch[index];
}


/*******************************************************************************
* Function Name: radar_unwrap_phase
********************************************************************************/
static float32_t radar_unwrap_phase(float32_t raw_phase)
{
    if (!s_presence_state.has_unwrapped_phase)
    {
        s_presence_state.has_unwrapped_phase = true;
        s_presence_state.last_unwrapped_phase = raw_phase;
        return raw_phase;
    }

    const float32_t wrapped_prev = remainderf(s_presence_state.last_unwrapped_phase, 2.0f * RADAR_PI_F);
    float32_t diff = raw_phase - wrapped_prev;
    while (diff > RADAR_PI_F)
    {
        diff -= 2.0f * RADAR_PI_F;
    }
    while (diff < -RADAR_PI_F)
    {
        diff += 2.0f * RADAR_PI_F;
    }

    s_presence_state.last_unwrapped_phase += diff;
    return s_presence_state.last_unwrapped_phase;
}


/*******************************************************************************
* Function Name: radar_process_phase_signal
********************************************************************************/
static void radar_process_phase_signal(const float32_t *phase_values,
                                       uint32_t count,
                                       float32_t *processed_out)
{
    if ((phase_values == NULL) || (processed_out == NULL) || (count == 0U))
    {
        return;
    }

    float32_t mean = 0.0f;
    for (uint32_t i = 0; i < count; i++)
    {
        mean += phase_values[i];
    }
    mean /= (float32_t)count;

    float32_t diff_values[RADAR_PRESENCE_HISTORY_LEN];
    float32_t prev = phase_values[0] - mean;
    diff_values[0] = 0.0f;

    for (uint32_t i = 1; i < count; i++)
    {
        const float32_t current = phase_values[i] - mean;
        diff_values[i] = radar_clampf(current - prev,
                                      -RADAR_PHASE_DIFF_CLIP,
                                      RADAR_PHASE_DIFF_CLIP);
        prev = current;
    }

    const int32_t radius = (int32_t)(RADAR_PHASE_SMOOTH_LEN / 2U);
    for (uint32_t i = 0; i < count; i++)
    {
        float32_t sum = 0.0f;
        uint32_t samples = 0U;
        const int32_t center = (int32_t)i;

        for (int32_t j = center - radius; j <= center + radius; j++)
        {
            if ((j >= 0) && ((uint32_t)j < count))
            {
                sum += diff_values[j];
                samples++;
            }
        }

        processed_out[i] = (samples > 0U) ? (sum / (float32_t)samples) : 0.0f;
    }
}


/*******************************************************************************
* Function Name: radar_count_peaks
********************************************************************************/
static uint32_t radar_count_peaks(const float32_t *values,
                                  uint32_t count,
                                  float32_t threshold,
                                  bool positive_peaks)
{
    if ((values == NULL) || (count < 3U))
    {
        return 0U;
    }

    uint32_t peaks = 0U;
    for (uint32_t i = 1U; i + 1U < count; i++)
    {
        const float32_t prev = positive_peaks ? values[i - 1U] : -values[i - 1U];
        const float32_t curr = positive_peaks ? values[i] : -values[i];
        const float32_t next = positive_peaks ? values[i + 1U] : -values[i + 1U];

        if ((curr >= threshold) && (curr > prev) && (curr >= next))
        {
            peaks++;
        }
    }

    return peaks;
}


/*******************************************************************************
* Function Name: radar_select_tracked_range_bin
********************************************************************************/
static int32_t radar_select_tracked_range_bin(const float32_t *range_energy,
                                              int32_t bin_min,
                                              int32_t bin_max,
                                              int32_t tracked_bin)
{
    if ((range_energy == NULL) || (bin_max <= bin_min))
    {
        return tracked_bin;
    }

    int32_t global_bin = bin_min;
    float32_t global_energy = range_energy[bin_min];
    for (int32_t bin = bin_min + 1; bin < bin_max; bin++)
    {
        if (range_energy[bin] > global_energy)
        {
            global_energy = range_energy[bin];
            global_bin = bin;
        }
    }

    if (tracked_bin < bin_min)
    {
        return global_bin;
    }

    tracked_bin = (tracked_bin >= bin_max) ? (bin_max - 1) : tracked_bin;
    const int32_t local_lo = (tracked_bin - RADAR_RANGE_TRACK_LOCAL_RADIUS_BINS > bin_min) ?
                             (tracked_bin - RADAR_RANGE_TRACK_LOCAL_RADIUS_BINS) : bin_min;
    const int32_t local_hi = (tracked_bin + RADAR_RANGE_TRACK_LOCAL_RADIUS_BINS + 1 < bin_max) ?
                             (tracked_bin + RADAR_RANGE_TRACK_LOCAL_RADIUS_BINS + 1) : bin_max;

    int32_t local_bin = local_lo;
    float32_t local_energy = range_energy[local_lo];
    for (int32_t bin = local_lo + 1; bin < local_hi; bin++)
    {
        if (range_energy[bin] > local_energy)
        {
            local_energy = range_energy[bin];
            local_bin = bin;
        }
    }

    int32_t candidate_bin = local_bin;
    if ((global_bin < local_lo) || (global_bin >= local_hi))
    {
        if (global_energy > fmaxf(local_energy, 1.0e-9f) * RADAR_RANGE_TRACK_SWITCH_RATIO)
        {
            /* 全局能量 >2× 局部：直接跳（新目标出现/旧目标消失） */
            if (global_energy > fmaxf(local_energy, 1.0e-9f) * 2.0f)
            {
                candidate_bin = global_bin;
            }
            else
            {
                int32_t step = global_bin - tracked_bin;
                if (step > RADAR_RANGE_TRACK_MAX_STEP_BINS)
                {
                    step = RADAR_RANGE_TRACK_MAX_STEP_BINS;
                }
                else if (step < -RADAR_RANGE_TRACK_MAX_STEP_BINS)
                {
                    step = -RADAR_RANGE_TRACK_MAX_STEP_BINS;
                }
                candidate_bin = tracked_bin + step;
            }
        }
    }

    if (candidate_bin < bin_min)
    {
        candidate_bin = bin_min;
    }
    else if (candidate_bin >= bin_max)
    {
        candidate_bin = bin_max - 1;
    }

    return candidate_bin;
}


/*******************************************************************************
* Function Name: radar_clampf
********************************************************************************/
static float32_t radar_clampf(float32_t value, float32_t min_value, float32_t max_value)
{
    if (value < min_value)
    {
        return min_value;
    }

    if (value > max_value)
    {
        return max_value;
    }

    return value;
}


/*******************************************************************************
* Function Name: radar_wait_for_irq_level
********************************************************************************/
static bool radar_wait_for_irq_level(int target_level, uint32_t timeout_ms)
{
    const TickType_t start_tick = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (gpio_get_level(PIN_XENSIV_BGT60TRXX_IRQ) != target_level)
    {
        if ((xTaskGetTickCount() - start_tick) >= timeout_ticks)
        {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return true;
}


/*******************************************************************************
* Function Name: radar_get_fstat_reg_addr
********************************************************************************/
static uint32_t radar_get_fstat_reg_addr(void)
{
    switch (xensiv_bgt60trxx_get_device(&bgt60_obj.dev))
    {
        case XENSIV_DEVICE_BGT60UTR11:
            return XENSIV_BGT60TRXX_REG_FSTAT_UTR11;

        case XENSIV_DEVICE_BGT60UTR13D:
            return XENSIV_BGT60TRXX_REG_FSTAT_UTR13D;

        case XENSIV_DEVICE_BGT60TR13C:
        case XENSIV_DEVICE_UNKNOWN:
        default:
            return XENSIV_BGT60TRXX_REG_FSTAT_TR13C;
    }
}


/*******************************************************************************
* Function Name: radar_log_timeout_diagnostics
********************************************************************************/
static void radar_log_timeout_diagnostics(const char *reason)
{
    uint32_t main_reg = 0;
    uint32_t stat1_reg = 0;
    uint32_t fstat_reg = 0;
    const int irq_level = gpio_get_level(PIN_XENSIV_BGT60TRXX_IRQ);
    const uint32_t fstat_addr = radar_get_fstat_reg_addr();
    const int32_t main_rslt = xensiv_bgt60trxx_get_reg(&bgt60_obj.dev,
                                                       XENSIV_BGT60TRXX_REG_MAIN,
                                                       &main_reg);
    const int32_t stat1_rslt = xensiv_bgt60trxx_get_reg(&bgt60_obj.dev,
                                                        XENSIV_BGT60TRXX_REG_STAT1,
                                                        &stat1_reg);
    const int32_t fstat_rslt = xensiv_bgt60trxx_get_reg(&bgt60_obj.dev,
                                                        fstat_addr,
                                                        &fstat_reg);
    const uint32_t frame_cnt =
        (stat1_reg & XENSIV_BGT60TRXX_REG_STAT1_FRAME_CNT_MSK) >>
        XENSIV_BGT60TRXX_REG_STAT1_FRAME_CNT_POS;
    const uint32_t shape_grp_cnt =
        (stat1_reg & XENSIV_BGT60TRXX_REG_STAT1_SHAPE_GRP_CNT_MSK) >>
        XENSIV_BGT60TRXX_REG_STAT1_SHAPE_GRP_CNT_POS;
    const uint32_t fill_status =
        (fstat_reg & XENSIV_BGT60TRXX_REG_FSTAT_FILL_STATUS_MSK) >>
        XENSIV_BGT60TRXX_REG_FSTAT_FILL_STATUS_POS;

    ESP_LOGW(TAG,
             "Timeout diag (%s): IRQ=%d MAIN=0x%06" PRIX32 " (rslt=%" PRIi32 ") "
             "STAT1=0x%06" PRIX32 " frame_cnt=%" PRIu32 " shape_grp=%" PRIu32 " (rslt=%" PRIi32 ") "
             "FSTAT=0x%06" PRIX32 " fill=%" PRIu32 " empty=%u cref=%u full=%u fou=%u fuf=%u "
             "spi_burst=%u clk_num=%u (rslt=%" PRIi32 ")",
             reason,
             irq_level,
             main_reg,
             main_rslt,
             stat1_reg,
             frame_cnt,
             shape_grp_cnt,
             stat1_rslt,
             fstat_reg,
             fill_status,
             (unsigned)((fstat_reg & XENSIV_BGT60TRXX_REG_FSTAT_EMPTY_MSK) != 0U),
             (unsigned)((fstat_reg & XENSIV_BGT60TRXX_REG_FSTAT_CREF_MSK) != 0U),
             (unsigned)((fstat_reg & XENSIV_BGT60TRXX_REG_FSTAT_FULL_MSK) != 0U),
             (unsigned)((fstat_reg & XENSIV_BGT60TRXX_REG_FSTAT_FOF_ERR_MSK) != 0U),
             (unsigned)((fstat_reg & XENSIV_BGT60TRXX_REG_FSTAT_FUF_ERR_MSK) != 0U),
             (unsigned)((fstat_reg & XENSIV_BGT60TRXX_REG_FSTAT_SPI_BURST_ERR_MSK) != 0U),
             (unsigned)((fstat_reg & XENSIV_BGT60TRXX_REG_FSTAT_CLK_NUM_ERR_MSK) != 0U),
             fstat_rslt);
}


/*******************************************************************************
* Function Name: radar_rearm_next_measurement
********************************************************************************/
static void radar_rearm_next_measurement(void)
{
    (void)xensiv_bgt60trxx_start_frame(&bgt60_obj.dev, false);
    (void)xensiv_bgt60trxx_soft_reset(&bgt60_obj.dev, XENSIV_BGT60TRXX_RESET_FIFO);
    vTaskDelay(pdMS_TO_TICKS(2));
    (void)xensiv_bgt60trxx_set_fifo_limit(&bgt60_obj.dev, FIFO_SLICE_SAMPLES);
    if (xensiv_bgt60trxx_start_frame(&bgt60_obj.dev, true) != XENSIV_BGT60TRXX_STATUS_OK)
    {
        radar_restart_frame_generator("Failed to re-arm the next distance measurement");
    }
}


/*******************************************************************************
* Function Name: radar_restart_frame_generator
********************************************************************************/
static void radar_restart_frame_generator(const char *reason)
{
    ESP_LOGW(TAG, "Restarting radar frame generator: %s", reason);
    (void)xensiv_bgt60trxx_start_frame(&bgt60_obj.dev, false);
    (void)xensiv_bgt60trxx_soft_reset(&bgt60_obj.dev,
                                      (xensiv_bgt60trxx_reset_t)(XENSIV_BGT60TRXX_RESET_FIFO |
                                                                 XENSIV_BGT60TRXX_RESET_FSM));
    vTaskDelay(pdMS_TO_TICKS(2));
    (void)xensiv_bgt60trxx_set_fifo_limit(&bgt60_obj.dev, FIFO_SLICE_SAMPLES);
    (void)xensiv_bgt60trxx_start_frame(&bgt60_obj.dev, true);
}


/*******************************************************************************
* 生命体征 IIR 滤波器初始化
* 呼吸 BPF: HPF@0.1Hz + LPF@0.6Hz (2 级级联)
* 心率 BPF: HPF@0.85Hz + LPF@2.2Hz + BPF@1.525Hz (3 级级联)
********************************************************************************/
static void radar_vitals_init_filters(void)
{
    /* 强制 FFT twiddle 表扩展到 1024 点。
     * sensor-dsp 可能先以 64/128 初始化，表太小导致 1024-FFT 越界。
     * deinit 释放旧表，再 init 1024 → 兼容所有尺寸。 */
    dsps_fft2r_deinit_fc32();
    esp_err_t fft_ret = dsps_fft2r_init_fc32(NULL, RADAR_VITALS_FFT_SIZE);
    if (fft_ret != ESP_OK) {
        ESP_LOGE(TAG, "FFT init 1024 failed: %d", (int)fft_ret);
    }

    const float fs = RADAR_PRESENCE_FRAME_RATE_HZ;
    const float q_butter = 0.7071f;  /* Butterworth Q */

    /* 呼吸 BPF: 0.1–0.6 Hz */
    dsps_biquad_gen_hpf_f32(s_presence_state.breath_bpf_coef[0],
                            0.1f / fs, q_butter);
    dsps_biquad_gen_lpf_f32(s_presence_state.breath_bpf_coef[1],
                            0.6f / fs, q_butter);

    /* 心率 BPF: 0.85–2.2 Hz */
    dsps_biquad_gen_hpf_f32(s_presence_state.heart_bpf_coef[0],
                            0.85f / fs, q_butter);
    dsps_biquad_gen_lpf_f32(s_presence_state.heart_bpf_coef[1],
                            2.2f / fs, q_butter);
    dsps_biquad_gen_bpf0db_f32(s_presence_state.heart_bpf_coef[2],
                               1.525f / fs, 1.13f);

    /* 清零延迟线 */
    memset(s_presence_state.breath_bpf_w, 0, sizeof(s_presence_state.breath_bpf_w));
    memset(s_presence_state.heart_bpf_w, 0, sizeof(s_presence_state.heart_bpf_w));

    /* 清零频率历史 */
    memset(s_presence_state.breath_hz_history, 0, sizeof(s_presence_state.breath_hz_history));
    memset(s_presence_state.heart_hz_history, 0, sizeof(s_presence_state.heart_hz_history));
    s_presence_state.breath_hz_count = 0;
    s_presence_state.heart_hz_count = 0;
    s_presence_state.breath_rate_bpm = 0.0f;
    s_presence_state.heart_rate_bpm = 0.0f;
    s_presence_state.vitals_frame_counter = 0;

    /* 实时波形 IIR 延迟线清零 */
    memset(s_presence_state.rt_breath_w, 0, sizeof(s_presence_state.rt_breath_w));
    memset(s_presence_state.rt_heart_w, 0, sizeof(s_presence_state.rt_heart_w));
    s_presence_state.rt_breath_wave = 0.0f;
    s_presence_state.rt_heart_wave = 0.0f;

    s_presence_state.vitals_filters_inited = true;
}


/*******************************************************************************
* filtfilt: 零相位 IIR 滤波（正向+反向）
* coefs: N 组 [5] 系数, num_stages: 级联阶数
* 注意: data 会被原地修改, temp 为等长工作缓冲区
********************************************************************************/
static void radar_filtfilt_biquad(float32_t *data, uint32_t len,
                                  float coefs[][5], uint32_t num_stages,
                                  float32_t *temp)
{
    if (len < 2U) { return; }

    /* 正向滤波 */
    for (uint32_t s = 0; s < num_stages; s++)
    {
        float w[2] = {0.0f, 0.0f};
        dsps_biquad_f32(data, temp, (int)len, coefs[s], w);
        memcpy(data, temp, len * sizeof(float32_t));
    }

    /* 反转 */
    for (uint32_t i = 0; i < len / 2U; i++)
    {
        float32_t t = data[i];
        data[i] = data[len - 1U - i];
        data[len - 1U - i] = t;
    }

    /* 反向滤波 */
    for (uint32_t s = 0; s < num_stages; s++)
    {
        float w[2] = {0.0f, 0.0f};
        dsps_biquad_f32(data, temp, (int)len, coefs[s], w);
        memcpy(data, temp, len * sizeof(float32_t));
    }

    /* 再次反转恢复原序 */
    for (uint32_t i = 0; i < len / 2U; i++)
    {
        float32_t t = data[i];
        data[i] = data[len - 1U - i];
        data[len - 1U - i] = t;
    }
}


/*******************************************************************************
* radar_estimate_rate_hz: FFT 频谱峰值频率估算
* Blackman 窗 → 零填充到 RADAR_VITALS_FFT_SIZE → FFT → ROI 内找峰 → 抛物线插值
* 返回 Hz, 失败返回 0
********************************************************************************/
static float32_t radar_estimate_rate_hz(const float32_t *signal, uint32_t len,
                                         float32_t fs,
                                         float32_t freq_min, float32_t freq_max)
{
    if (len < 8U) { return 0.0f; }

    const uint32_t fft_n = RADAR_VITALS_FFT_SIZE;

    /* 清零 FFT 缓冲区 */
    memset(vitals_fft_buf, 0, sizeof(vitals_fft_buf));

    /* Blackman 窗并写入复数交错格式 (re, im=0) */
    for (uint32_t i = 0; i < len; i++)
    {
        const float32_t w = 0.42f
            - 0.50f * cosf(2.0f * RADAR_PI_F * (float32_t)i / (float32_t)(len - 1U))
            + 0.08f * cosf(4.0f * RADAR_PI_F * (float32_t)i / (float32_t)(len - 1U));
        vitals_fft_buf[i * 2U] = signal[i] * w;     /* real */
        /* vitals_fft_buf[i*2+1] = 0 已由 memset 清零 */
    }

    /* FFT + bit reversal */
    dsps_fft2r_fc32(vitals_fft_buf, (int)fft_n);
    dsps_bit_rev_fc32(vitals_fft_buf, (int)fft_n);

    /* 计算幅度谱 (只需前 N/2 个 bin) */
    const uint32_t half_n = fft_n / 2U;
    const float32_t bin_hz = fs / (float32_t)fft_n;

    /* ROI: [freq_min, freq_max] 对应的 bin 范围 */
    uint32_t bin_lo = (uint32_t)(freq_min / bin_hz);
    uint32_t bin_hi = (uint32_t)(freq_max / bin_hz);
    if (bin_lo < 1U) { bin_lo = 1U; }
    if (bin_hi >= half_n) { bin_hi = half_n - 1U; }
    if (bin_lo >= bin_hi) { return 0.0f; }

    /* 在 ROI 内找最大幅度 bin */
    float32_t max_mag = 0.0f;
    uint32_t max_bin = bin_lo;
    for (uint32_t k = bin_lo; k <= bin_hi; k++)
    {
        float32_t re = vitals_fft_buf[k * 2U];
        float32_t im = vitals_fft_buf[k * 2U + 1U];
        float32_t mag = re * re + im * im;
        if (mag > max_mag)
        {
            max_mag = mag;
            max_bin = k;
        }
    }

    if (max_mag < 1.0e-12f) { return 0.0f; }

    /* 抛物线插值精确频率 */
    float32_t peak_bin = (float32_t)max_bin;
    if (max_bin > bin_lo && max_bin < bin_hi)
    {
        float32_t re_l = vitals_fft_buf[(max_bin - 1U) * 2U];
        float32_t im_l = vitals_fft_buf[(max_bin - 1U) * 2U + 1U];
        float32_t re_r = vitals_fft_buf[(max_bin + 1U) * 2U];
        float32_t im_r = vitals_fft_buf[(max_bin + 1U) * 2U + 1U];
        float32_t alpha = sqrtf(re_l * re_l + im_l * im_l);
        float32_t beta  = sqrtf(max_mag);
        float32_t gamma = sqrtf(re_r * re_r + im_r * im_r);
        float32_t denom = alpha - 2.0f * beta + gamma;
        if (fabsf(denom) > 1.0e-10f)
        {
            peak_bin += 0.5f * (alpha - gamma) / denom;
        }
    }

    return peak_bin * bin_hz;
}


/*******************************************************************************
* radar_remove_breath_harmonics: 去除心率信号中的呼吸谐波
* 对 breath_hz 的 2~8 次谐波，在心率频段 (0.85–2.2 Hz) 内施加 notch 滤波
********************************************************************************/
static void radar_remove_breath_harmonics(float32_t *heart_sig, uint32_t len,
                                           float32_t breath_hz, float32_t fs)
{
    if (breath_hz < 0.05f || len < 4U) { return; }

    const float32_t heart_lo = 0.85f;
    const float32_t heart_hi = 2.2f;
    float32_t temp[RADAR_PRESENCE_HISTORY_LEN];

    for (uint32_t h = 2; h <= 8; h++)
    {
        float32_t harmonic = breath_hz * (float32_t)h;
        if (harmonic < heart_lo || harmonic > heart_hi)
        {
            continue;
        }

        float notch_coef[5];
        dsps_biquad_gen_notch_f32(notch_coef, harmonic / fs, -40.0f, 30.0f);

        float w[2] = {0.0f, 0.0f};
        dsps_biquad_f32(heart_sig, temp, (int)len, notch_coef, w);
        memcpy(heart_sig, temp, len * sizeof(float32_t));
    }
}


/*******************************************************************************
* 循环缓冲区辅助：将最近 want 个样本线性化到 out
********************************************************************************/
static void radar_linearize_float(const float32_t *ring, uint32_t ring_len,
                                   uint32_t head, uint32_t total, uint32_t want,
                                   float32_t *out)
{
    if (want > total) { want = total; }
    uint32_t start = (head + ring_len - want) % ring_len;
    if (start + want <= ring_len)
    {
        memcpy(out, &ring[start], want * sizeof(float32_t));
    }
    else
    {
        uint32_t first = ring_len - start;
        memcpy(out, &ring[start], first * sizeof(float32_t));
        memcpy(out + first, ring, (want - first) * sizeof(float32_t));
    }
}


/*******************************************************************************
* 简单中值（小数组用，n<=5）
********************************************************************************/
static float32_t radar_small_median(const float32_t *arr, uint32_t n)
{
    float32_t sorted[5];
    if (n == 0U) { return 0.0f; }
    if (n > 5U) { n = 5U; }
    memcpy(sorted, arr, n * sizeof(float32_t));
    for (uint32_t i = 0; i < n - 1U; i++)
    {
        for (uint32_t j = i + 1U; j < n; j++)
        {
            if (sorted[j] < sorted[i])
            {
                float32_t t = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = t;
            }
        }
    }
    return sorted[n / 2U];
}


/*******************************************************************************
* radar_estimate_vitals: 生命体征估算主函数
* 每 RADAR_VITALS_INTERVAL_FRAMES 帧调用一次
* 从相位历史提取呼吸和心率频率
********************************************************************************/
static void radar_estimate_vitals(radar_frame_result_t *result)
{
    if (!s_presence_state.vitals_filters_inited)
    {
        radar_vitals_init_filters();
    }

    const uint32_t total = s_presence_state.history_count;
    if (total < RADAR_PRESENCE_HISTORY_LEN)
    {
        return;  /* 缓冲区未满，不进行分析 */
    }

    const float32_t fs = RADAR_PRESENCE_FRAME_RATE_HZ;
    const uint32_t head = s_presence_state.history_head;
    const uint32_t len = RADAR_PRESENCE_HISTORY_LEN;

    /* 线性化最近 200 帧相位 */
    float32_t phase_buf[RADAR_PRESENCE_HISTORY_LEN];
    radar_linearize_float(s_presence_state.phase_history, len,
                          head, total, len, phase_buf);

    /* 相位预处理: 去均值 → 差分裁剪 → 平滑（用于呼吸） */
    float32_t processed[RADAR_PRESENCE_HISTORY_LEN];
    radar_process_phase_signal(phase_buf, len, processed);

    /* 差分裁剪但不平滑的版本（用于心率，保留高频分量）
     * 复用 phase_buf 避免额外栈分配 */
    {
        float32_t mean = 0.0f;
        for (uint32_t i = 0; i < len; i++) { mean += phase_buf[i]; }
        mean /= (float32_t)len;
        float32_t prev = phase_buf[0] - mean;
        phase_buf[0] = 0.0f;
        for (uint32_t i = 1; i < len; i++)
        {
            float32_t cur = phase_buf[i] - mean;
            phase_buf[i] = radar_clampf(cur - prev,
                                        -RADAR_PHASE_DIFF_CLIP,
                                        RADAR_PHASE_DIFF_CLIP);
            prev = cur;
        }
    }
    /* phase_buf 现在是 diff-clipped 信号（无平滑），用于心率 */

    /* filtfilt 工作缓冲区 */
    float32_t filt_temp[RADAR_PRESENCE_HISTORY_LEN];

    /* ---- 呼吸率 ---- */
    float32_t breath_sig[RADAR_PRESENCE_HISTORY_LEN];
    memcpy(breath_sig, processed, len * sizeof(float32_t));
    radar_filtfilt_biquad(breath_sig, len,
                          s_presence_state.breath_bpf_coef, 2U,
                          filt_temp);
    float32_t breath_hz = radar_estimate_rate_hz(breath_sig, len,
                                                  fs, 0.20f, 0.50f);

    /* 中值平滑呼吸率 */
    if (breath_hz > 0.0f)
    {
        uint32_t idx = s_presence_state.breath_hz_count % 3U;
        s_presence_state.breath_hz_history[idx] = breath_hz;
        s_presence_state.breath_hz_count++;
        uint32_t n = (s_presence_state.breath_hz_count < 3U)
                     ? s_presence_state.breath_hz_count : 3U;
        breath_hz = radar_small_median(s_presence_state.breath_hz_history, n);
    }

    /* ---- 心率：使用未平滑的差分信号，保留 0.85-2.2Hz 分量 ---- */
    float32_t heart_sig[RADAR_PRESENCE_HISTORY_LEN];
    memcpy(heart_sig, phase_buf, len * sizeof(float32_t));
    radar_filtfilt_biquad(heart_sig, len,
                          s_presence_state.heart_bpf_coef, 3U,
                          filt_temp);

    /* 去除呼吸谐波 */
    if (breath_hz > 0.0f)
    {
        radar_remove_breath_harmonics(heart_sig, len, breath_hz, fs);
    }

    float32_t heart_hz = radar_estimate_rate_hz(heart_sig, len,
                                                 fs, 0.85f, 2.2f);

    ESP_LOGI(TAG, "Vitals raw: breath_hz=%.4f heart_hz=%.4f (%.1f/%.1f BPM)",
             breath_hz, heart_hz, breath_hz * 60.0f, heart_hz * 60.0f);

    /* FIX: 心率离群值拒绝 — 前 3 次不拒绝（让初始值稳定），之后放宽到 ±25 BPM */
    if (heart_hz > 0.0f && s_presence_state.heart_hz_count >= 3U)
    {
        float32_t heart_bpm_raw = heart_hz * 60.0f;
        uint32_t n_prev = (s_presence_state.heart_hz_count < 5U)
                          ? s_presence_state.heart_hz_count : 5U;
        float32_t prev_median = radar_small_median(
            s_presence_state.heart_hz_history, n_prev) * 60.0f;
        if (fabsf(heart_bpm_raw - prev_median) > 25.0f)
        {
            ESP_LOGW(TAG, "Heart outlier rejected: %.1f vs median %.1f",
                     heart_bpm_raw, prev_median);
            heart_hz = 0.0f;
        }
    }

    if (heart_hz > 0.0f)
    {
        uint32_t idx = s_presence_state.heart_hz_count % 5U;
        s_presence_state.heart_hz_history[idx] = heart_hz;
        s_presence_state.heart_hz_count++;
        uint32_t n = (s_presence_state.heart_hz_count < 5U)
                     ? s_presence_state.heart_hz_count : 5U;
        heart_hz = radar_small_median(s_presence_state.heart_hz_history, n);
    }

    /* 写入结果 */
    s_presence_state.breath_rate_bpm = breath_hz * 60.0f;
    s_presence_state.heart_rate_bpm = heart_hz * 60.0f;

    result->breath_rate_bpm = s_presence_state.breath_rate_bpm;
    result->heart_rate_bpm = s_presence_state.heart_rate_bpm;

    ESP_LOGI(TAG, "Vitals result: breath=%.1fbpm heart=%.1fbpm",
             result->breath_rate_bpm, result->heart_rate_bpm);
}


/*******************************************************************************
* Function Name: radar_update_presence
********************************************************************************/
static void radar_update_presence(void *result_ptr)
{
    radar_frame_result_t *result = (radar_frame_result_t *)result_ptr;
    if (result == NULL)
    {
        return;
    }

    /* 循环缓冲区写入 */
    {
        const uint32_t idx = s_presence_state.history_head;
        s_presence_state.phase_history[idx] = result->phase_unwrapped;
        s_presence_state.amplitude_history[idx] = result->amplitude_metric;
        s_presence_state.bin_history[idx] = result->range_bin;

        /* ---- RBM 标记：基于自适应基线体动值（在 radar_analyze_frame 中已计算） ---- */
        uint8_t rbm_flag = (result->body_movement_mm > 0.0f) ? 1U : 0U;

        /* 更新 RBM 环形缓冲：减去旧值，加入新值 */
        s_presence_state.rbm_sum -= s_presence_state.rbm_flags[idx];
        s_presence_state.rbm_flags[idx] = rbm_flag;
        s_presence_state.rbm_sum += rbm_flag;
        result->rbm_detected = (rbm_flag != 0U);

        const uint32_t rbm_window = (s_presence_state.history_count < RADAR_RBM_WINDOW_FRAMES)
                                    ? s_presence_state.history_count : RADAR_RBM_WINDOW_FRAMES;
        result->rbm_ratio = (rbm_window > 0U)
                            ? (float32_t)s_presence_state.rbm_sum / (float32_t)rbm_window
                            : 0.0f;

        s_presence_state.history_head = (idx + 1U) % RADAR_PRESENCE_HISTORY_LEN;
        if (s_presence_state.history_count < RADAR_PRESENCE_HISTORY_LEN)
        {
            s_presence_state.history_count++;
        }
    }

    const uint32_t total_count = s_presence_state.history_count;
    const uint32_t phase_count = (uint32_t)fminf((float32_t)total_count,
                                                 RADAR_PHASE_WINDOW_SECONDS *
                                                 RADAR_PRESENCE_FRAME_RATE_HZ);
    const uint32_t amp_count = (uint32_t)fminf((float32_t)total_count,
                                               RADAR_AMP_WINDOW_SECONDS *
                                               RADAR_PRESENCE_FRAME_RATE_HZ);
    const uint32_t bin_count = (uint32_t)fminf((float32_t)total_count,
                                               RADAR_BIN_WINDOW_SECONDS *
                                               RADAR_PRESENCE_FRAME_RATE_HZ);
    const uint32_t breath_count = (uint32_t)fminf((float32_t)total_count,
                                                  RADAR_BREATH_WINDOW_SECONDS *
                                                  RADAR_PRESENCE_FRAME_RATE_HZ);

    if ((phase_count < 2U) || (amp_count < 2U) || (bin_count < 2U))
    {
        result->presence_detected = false;
        result->presence_confidence = 0.0f;
        result->presence_distance_cm = 0.0f;
        result->phase_excursion_mm = 0.0f;
        result->amplitude_cv = 0.0f;
        result->bin_span = 99.0f;
        return;
    }

    /* 复用的线性化临时缓冲区 */
    float32_t hist_linear[RADAR_PRESENCE_HISTORY_LEN];
    const uint32_t head = s_presence_state.history_head;

    /* ---- 相位偏移 ---- */
    radar_linearize_float(s_presence_state.phase_history, RADAR_PRESENCE_HISTORY_LEN,
                          head, total_count, phase_count, hist_linear);
    result->phase_excursion_mm =
        radar_percentile_abs_dev(hist_linear, phase_count, 90.0f) *
        (RADAR_WAVELENGTH_MM / (4.0f * RADAR_PI_F));
    result->phase_present = (result->phase_excursion_mm >= s_presence_thresholds.phase_exc_mm_th);

    /* ---- 振幅变异系数 ---- */
    radar_linearize_float(s_presence_state.amplitude_history, RADAR_PRESENCE_HISTORY_LEN,
                          head, total_count, amp_count, hist_linear);
    float32_t amp_abs[RADAR_PRESENCE_HISTORY_LEN];
    for (uint32_t i = 0; i < amp_count; i++)
    {
        amp_abs[i] = fabsf(hist_linear[i]);
    }
    const float32_t amp_median = fmaxf(radar_median_float(amp_abs, amp_count), 1.0e-6f);
    result->amplitude_cv = radar_std_float(hist_linear, amp_count) / amp_median;
    result->amplitude_present = (result->amplitude_cv <= s_presence_thresholds.amp_cv_th);

    /* ---- bin 跨度（直接遍历环形缓冲区，无需线性化） ---- */
    {
        uint32_t bin_start = (head + RADAR_PRESENCE_HISTORY_LEN - bin_count)
                             % RADAR_PRESENCE_HISTORY_LEN;
        int32_t bin_min = s_presence_state.bin_history[bin_start];
        int32_t bin_max = bin_min;
        for (uint32_t i = 1; i < bin_count; i++)
        {
            uint32_t phys = (bin_start + i) % RADAR_PRESENCE_HISTORY_LEN;
            int32_t v = s_presence_state.bin_history[phys];
            if (v < bin_min) { bin_min = v; }
            if (v > bin_max) { bin_max = v; }
        }
        result->bin_span = (float32_t)(bin_max - bin_min);
        result->bin_present = (result->bin_span <= s_presence_thresholds.bin_span_th);
    }

    /* ---- 呼吸峰计数 ---- */
    float32_t breath_processed[RADAR_PRESENCE_HISTORY_LEN];
    memset(breath_processed, 0, sizeof(breath_processed));
    radar_linearize_float(s_presence_state.phase_history, RADAR_PRESENCE_HISTORY_LEN,
                          head, total_count, breath_count, hist_linear);
    radar_process_phase_signal(hist_linear, breath_count, breath_processed);
    const uint32_t peaks_pos = radar_count_peaks(breath_processed,
                                                 breath_count,
                                                 s_presence_thresholds.peak_height,
                                                 true);
    const uint32_t peaks_neg = radar_count_peaks(breath_processed,
                                                 breath_count,
                                                 s_presence_thresholds.peak_height,
                                                 false);
    result->breath_present = ((peaks_pos >= 2U) || (peaks_neg >= 2U));

    float32_t confidence = 0.0f;
    if (result->breath_present)
    {
        confidence += 0.55f;
    }
    if (result->phase_present)
    {
        confidence += 0.25f;
    }
    if (result->amplitude_present)
    {
        confidence += 0.10f;
    }
    if (result->bin_present)
    {
        confidence += 0.10f;
    }
    result->presence_confidence = confidence;

    /* 10Hz 子指标详细日志（CLI 开关控制） */
    if (s_presence_thresholds.debug_log_enabled)
    {
        ESP_LOGI(TAG,
                 "PRES_DBG: phExc=%.4fmm ampCV=%.4f binSpan=%.1f "
                 "peaks+=%"PRIu32" peaks-=%"PRIu32" conf=%.2f",
                 result->phase_excursion_mm,
                 result->amplitude_cv,
                 result->bin_span,
                 peaks_pos, peaks_neg,
                 confidence);
    }

    const bool raw_present =
        (confidence >= s_presence_thresholds.confidence_th) ||
        (result->breath_present && result->phase_present) ||
        (result->phase_present && result->bin_present);

    if (raw_present)
    {
        s_presence_state.presence_latched = true;
        s_presence_state.presence_misses = 0U;
    }
    else if (s_presence_state.presence_latched &&
             (s_presence_state.presence_misses < s_presence_thresholds.miss_limit))
    {
        s_presence_state.presence_misses++;
    }
    else
    {
        s_presence_state.presence_latched = false;
    }

    result->presence_detected = s_presence_state.presence_latched;
    result->presence_distance_cm =
        result->presence_detected ? radar_calculate_distance_cm_from_bin(result->range_bin) : 0.0f;

    /* 生命体征估算（每 RADAR_VITALS_INTERVAL_FRAMES 帧执行一次） */
    s_presence_state.vitals_frame_counter++;
    if (s_presence_state.vitals_frame_counter >= RADAR_VITALS_INTERVAL_FRAMES)
    {
        s_presence_state.vitals_frame_counter = 0;
        if (result->presence_detected && result->rbm_ratio <= RADAR_RBM_RATIO_TH)
        {
            radar_estimate_vitals(result);
        }
        else if (result->presence_detected && result->rbm_ratio > RADAR_RBM_RATIO_TH)
        {
            ESP_LOGW(TAG, "Vitals skipped: RBM ratio=%.0f%% (>%.0f%%)",
                     result->rbm_ratio * 100.0f, RADAR_RBM_RATIO_TH * 100.0f);
            result->breath_rate_bpm = s_presence_state.breath_rate_bpm;
            result->heart_rate_bpm = s_presence_state.heart_rate_bpm;
        }
        else
        {
            /* 无人时清零 */
            s_presence_state.breath_rate_bpm = 0.0f;
            s_presence_state.heart_rate_bpm = 0.0f;
            s_presence_state.breath_hz_count = 0;
            s_presence_state.heart_hz_count = 0;
            result->breath_rate_bpm = 0.0f;
            result->heart_rate_bpm = 0.0f;
        }
    }
    else
    {
        /* 非估算帧：输出上次的结果 */
        result->breath_rate_bpm = s_presence_state.breath_rate_bpm;
        result->heart_rate_bpm = s_presence_state.heart_rate_bpm;
    }

    /* ---- 实时波形：每帧 IIR 滤波当前相位样本 ---- */
    if (s_presence_state.vitals_filters_inited)
    {
        float32_t sample = result->phase_unwrapped;
        float32_t filtered;

        /* 呼吸波形：2 级 BPF 级联 */
        filtered = sample;
        for (uint32_t s = 0; s < 2U; s++)
        {
            float out;
            dsps_biquad_f32(&filtered, &out, 1,
                            s_presence_state.breath_bpf_coef[s],
                            s_presence_state.rt_breath_w[s]);
            filtered = out;
        }
        s_presence_state.rt_breath_wave = filtered;
        result->breath_wave = filtered;

        /* 心率波形：3 级 BPF 级联 */
        filtered = sample;
        for (uint32_t s = 0; s < 3U; s++)
        {
            float out;
            dsps_biquad_f32(&filtered, &out, 1,
                            s_presence_state.heart_bpf_coef[s],
                            s_presence_state.rt_heart_w[s]);
            filtered = out;
        }
        s_presence_state.rt_heart_wave = filtered;
        result->heart_wave = filtered;
    }
}


/*******************************************************************************
* Function Name: radar_analyze_frame
********************************************************************************/
static bool radar_analyze_frame(void *result_ptr)
{
    radar_frame_result_t *result = (radar_frame_result_t *)result_ptr;
    if (result == NULL)
    {
        return false;
    }

    memset(result, 0, sizeof(*result));
    memset(range_energy_accum, 0, sizeof(range_energy_accum));
    memset(coherent_range_accum, 0, sizeof(coherent_range_accum));
    memset(coherent_range_avg, 0, sizeof(coherent_range_avg));

    for (uint32_t chirp = 0U; chirp < NUM_CHIRPS_PER_FRAME; chirp++)
    {
        const uint32_t chirp_offset = chirp * NUM_SAMPLES_PER_CHIRP * NUM_RX_ANTENNAS;
        for (uint32_t sample = 0U; sample < NUM_SAMPLES_PER_CHIRP; sample++)
        {
            const uint32_t raw_index = chirp_offset + (sample * NUM_RX_ANTENNAS) + RADAR_MAIN_RX_IDX;
            chirp_distance_frame[sample] =
                ((float32_t)bgt60_buffer[raw_index] - 2048.0f) / 2048.0f;
        }

        if (ifx_range_fft_f32(chirp_distance_frame,
                              distance_range_fft,
                              true,
                              distance_window,
                              NUM_SAMPLES_PER_CHIRP,
                              1U) != IFX_SENSOR_DSP_STATUS_OK)
        {
            ESP_LOGW(TAG, "ifx_range_fft_f32 failed during frame analysis");
            return false;
        }

        for (uint32_t bin = 0U; bin < (NUM_SAMPLES_PER_CHIRP / 2U); bin++)
        {
            const float32_t re = CREAL_F32(distance_range_fft[bin]);
            const float32_t im = CIMAG_F32(distance_range_fft[bin]);
            range_energy_accum[bin] += sqrtf((re * re) + (im * im));
            coherent_range_accum[bin] += distance_range_fft[bin];
        }
    }

    for (uint32_t bin = 0U; bin < (NUM_SAMPLES_PER_CHIRP / 2U); bin++)
    {
        coherent_range_avg[bin] = coherent_range_accum[bin] / (float32_t)NUM_CHIRPS_PER_FRAME;
    }

    const int32_t tracked_seed =
        (s_presence_state.search_bin_count > 0U) ?
        radar_median_int(s_presence_state.search_bin_history, s_presence_state.search_bin_count) :
        -1;
    const int32_t search_bin =
        radar_select_tracked_range_bin(range_energy_accum,
                                       (int32_t)RADAR_DISTANCE_FIRST_VALID_BIN,
                                       (int32_t)(NUM_SAMPLES_PER_CHIRP / 2U),
                                       tracked_seed);
    radar_append_int_history(s_presence_state.search_bin_history,
                             &s_presence_state.search_bin_count,
                             RADAR_RANGE_TRACK_HISTORY_LEN,
                             search_bin);

    result->range_bin = radar_median_int(s_presence_state.search_bin_history,
                                         s_presence_state.search_bin_count);
    result->distance_cm = radar_calculate_distance_cm_from_bin(result->range_bin);
    result->signal_db = radar_calculate_fft_level_db(coherent_range_avg, result->range_bin);
    result->target_detected = (result->signal_db > RADAR_DISTANCE_THRESHOLD_DB);

    const int32_t bin_start = (int32_t)RADAR_DISTANCE_FIRST_VALID_BIN;
    const int32_t bin_end = (int32_t)(NUM_SAMPLES_PER_CHIRP / 2U);
    if (bin_end > bin_start)
    {
        for (int32_t b = bin_start; b < bin_end; b++)
        {
            const float32_t re = CREAL_F32(coherent_range_avg[b]);
            const float32_t im = CIMAG_F32(coherent_range_avg[b]);
            result->movement_energy += sqrtf((re * re) + (im * im));
        }
        result->movement_energy /= (float32_t)(bin_end - bin_start);
    }

    const int32_t energy_lo = (result->range_bin - RADAR_PRESENCE_ENERGY_RADIUS_BINS > bin_start) ?
                              (result->range_bin - RADAR_PRESENCE_ENERGY_RADIUS_BINS) : bin_start;
    const int32_t energy_hi = (result->range_bin + RADAR_PRESENCE_ENERGY_RADIUS_BINS + 1 < bin_end) ?
                              (result->range_bin + RADAR_PRESENCE_ENERGY_RADIUS_BINS + 1) : bin_end;
    for (int32_t b = energy_lo; b < energy_hi; b++)
    {
        const float32_t re = CREAL_F32(coherent_range_avg[b]);
        const float32_t im = CIMAG_F32(coherent_range_avg[b]);
        result->amplitude_metric += sqrtf((re * re) + (im * im));
    }

    result->phase_unwrapped = radar_unwrap_phase(atan2f(CIMAG_F32(coherent_range_avg[result->range_bin]),
                                                        CREAL_F32(coherent_range_avg[result->range_bin])));

    /* ---- 自适应基线体动检测 ---- */
    {
        const int32_t n_bins = (int32_t)(NUM_SAMPLES_PER_CHIRP / 2U);

        /* 提取当前帧频谱幅度 */
        float32_t cur_spectrum[NUM_SAMPLES_PER_CHIRP / 2U];
        for (int32_t b = 0; b < n_bins; b++)
        {
            const float32_t re = CREAL_F32(coherent_range_avg[b]);
            const float32_t im = CIMAG_F32(coherent_range_avg[b]);
            cur_spectrum[b] = sqrtf(re * re + im * im);
        }

        /* 初始化基线：第一帧直接赋值 */
        if (!s_presence_state.baseline_inited)
        {
            for (int32_t b = 0; b < n_bins; b++)
            {
                s_presence_state.baseline[b] = cur_spectrum[b];
            }
            s_presence_state.baseline_inited = true;
            s_presence_state.noise_floor = 0.0f;
            s_presence_state.body_move_raw = 0.0f;
            s_presence_state.body_move_smooth = 0.0f;
        }

        /* 前向能量差分：目标 bin ±5 附近的频谱与基线的差异 */
        float32_t diff_sum = 0.0f;
        const int32_t target_bin = result->range_bin;
        const int32_t roi_radius = 5;
        int32_t roi_lo = target_bin - roi_radius;
        int32_t roi_hi = target_bin + roi_radius + 1;
        if (roi_lo < bin_start) { roi_lo = bin_start; }
        if (roi_hi > n_bins)    { roi_hi = n_bins; }
        if (roi_hi <= roi_lo)   { roi_lo = bin_start; roi_hi = bin_start + 1; }
        for (int32_t b = roi_lo; b < roi_hi; b++)
        {
            diff_sum += fabsf(cur_spectrum[b] - s_presence_state.baseline[b]);
        }
        float32_t move_raw = diff_sum / (float32_t)(roi_hi - roi_lo);

        /* 校准阶段：前 30 帧 (3s) */
        const bool calibrating = (s_presence_state.body_move_count < 30U);

        /* 运动判定：raw 超过 noise 的 1.8 倍 */
        const float32_t move_gate = fmaxf(s_presence_state.noise_floor * 1.8f, 0.02f);
        const bool is_moving = !calibrating && (move_raw > move_gate);

        /* 自适应基线更新
         * α 控制隐式高通截止: f_cut ≈ α × fs / 2π
         * α=0.5 → f_cut ≈ 0.8Hz → 呼吸(0.3Hz)被基线吸收，体动(突发)产生差值 */
        float32_t alpha;
        if (calibrating)
        {
            alpha = 0.30f;   /* 校准期：快速收敛 */
        }
        else if (is_moving)
        {
            alpha = 0.01f;   /* 运动中：极慢更新，保持运动前的基线 */
        }
        else
        {
            alpha = 0.50f;   /* 静止：紧密跟随，吸收呼吸波动 */
        }
        for (int32_t b = roi_lo; b < roi_hi; b++)
        {
            s_presence_state.baseline[b] =
                alpha * cur_spectrum[b] + (1.0f - alpha) * s_presence_state.baseline[b];
        }

        /* 噪声底学习：跟踪静坐时 raw 的均值水平
         * 静止时 IIR 跟踪 raw → noise 代表"正常静坐的 raw 水平"
         * 运动时冻结 noise → 死区不被运动拉高 */
        if (calibrating)
        {
            s_presence_state.noise_floor =
                0.15f * move_raw + 0.85f * s_presence_state.noise_floor;
        }
        else if (!is_moving)
        {
            /* 静止时：中等速度跟踪 raw 均值 */
            s_presence_state.noise_floor =
                0.05f * move_raw + 0.95f * s_presence_state.noise_floor;
        }
        /* 运动时不更新 noise → 保持静坐水平 */

        /* 帧计数（用于校准阶段判定） */
        s_presence_state.body_move_count++;

        /* 10 帧滑窗平滑 */
        {
            const uint32_t mi = s_presence_state.body_move_idx;
            s_presence_state.body_move_buf[mi] = move_raw;
            s_presence_state.body_move_idx = (mi + 1U) % 10U;

            const uint32_t n = (s_presence_state.body_move_count < 10U)
                               ? s_presence_state.body_move_count : 10U;
            float32_t smooth_sum = 0.0f;
            for (uint32_t i = 0; i < n; i++)
            {
                smooth_sum += s_presence_state.body_move_buf[i];
            }
            s_presence_state.body_move_smooth = smooth_sum / (float32_t)n;
        }

        /* 死区：校准期 或 低于噪声底 × 1.8 → 输出 0
         * noise 现在跟踪静坐均值 (~0.15-0.25), ×1.8 ≈ 0.27-0.45 */
        s_presence_state.body_move_raw = move_raw;
        float32_t final_move = 0.0f;
        if (!calibrating)
        {
            final_move = s_presence_state.body_move_smooth;
            if (final_move < s_presence_state.noise_floor * 1.8f)
            {
                final_move = 0.0f;
            }
        }

        result->body_movement_mm = final_move;   /* 体动强度（归一化能量差） */
        result->rbm_phase_jump = move_raw;        /* 当前帧原始差分值 */
    }

    radar_update_presence(result);
    return true;
}


#if 0
/*******************************************************************************
* Function Name: radar_update_tuning_suggestions
********************************************************************************/
static void radar_update_tuning_suggestions(float32_t macro_power,
                                            float32_t micro_power,
                                            float32_t movement_energy,
                                            bool macro_candidate,
                                            bool micro_candidate)
{
    const float32_t quiet_alpha = 0.08f;
    const float32_t quiet_gate = fmaxf(0.06f, s_radar_tuning.quiet_movement_ema * 1.35f);

    if ((s_radar_tuning.quiet_macro_ema <= 0.0f) && (macro_power > 0.0f))
    {
        s_radar_tuning.quiet_macro_ema = macro_power;
    }
    if ((s_radar_tuning.quiet_movement_ema <= 0.0f) && (movement_energy > 0.0f))
    {
        s_radar_tuning.quiet_movement_ema = movement_energy;
    }

    if (!macro_candidate && !micro_candidate)
    {
        s_radar_tuning.quiet_macro_ema =
            ((1.0f - quiet_alpha) * s_radar_tuning.quiet_macro_ema) + (quiet_alpha * macro_power);
        s_radar_tuning.quiet_movement_ema =
            ((1.0f - quiet_alpha) * s_radar_tuning.quiet_movement_ema) +
            (quiet_alpha * movement_energy);
    }

    if (macro_candidate || (movement_energy > quiet_gate))
    {
        const float32_t observed_peak = fmaxf(macro_power, micro_power);
        if (observed_peak > s_radar_tuning.active_macro_peak)
        {
            s_radar_tuning.active_macro_peak = observed_peak;
        }
        else
        {
            s_radar_tuning.active_macro_peak =
                fmaxf(s_radar_tuning.active_macro_peak * 0.995f, observed_peak);
        }
    }
    else
    {
        s_radar_tuning.active_macro_peak =
            fmaxf(s_radar_tuning.active_macro_peak * 0.997f,
                  s_radar_tuning.quiet_macro_ema * 1.25f);
    }

    const float32_t background_level =
        fmaxf(s_radar_tuning.quiet_macro_ema, s_radar_tuning.quiet_movement_ema * 0.60f);
    const float32_t active_reference =
        fmaxf(s_radar_tuning.active_macro_peak, background_level + 0.05f);
    const float32_t dynamic_span = fmaxf(active_reference - background_level, 0.05f);
    const float32_t micro_upper_bound =
        fmaxf(RADAR_SUGGESTED_MICRO_MIN,
              fminf(RADAR_SUGGESTED_MICRO_MAX,
                    (background_level + (dynamic_span * 0.28f))));

    s_radar_tuning.suggested_macro_threshold =
        radar_clampf(background_level + (dynamic_span * 0.45f),
                     RADAR_SUGGESTED_MACRO_MIN,
                     RADAR_SUGGESTED_MACRO_MAX);
    s_radar_tuning.suggested_micro_threshold =
        radar_clampf(background_level + (dynamic_span * 0.18f),
                     RADAR_SUGGESTED_MICRO_MIN,
                     micro_upper_bound);
}


/*******************************************************************************
* Function Name: radar_feedback_label
********************************************************************************/
static const char *radar_feedback_label(bool macro_candidate,
                                        bool micro_candidate,
                                        float32_t movement_energy)
{
    if (macro_candidate)
    {
        return "MACRO";
    }

    if (micro_candidate)
    {
        return "MICRO";
    }

    if (movement_energy > fmaxf(0.06f, s_radar_tuning.quiet_movement_ema * 1.35f))
    {
        return "WEAK_MOTION";
    }

    return "QUIET";
}
#endif


/*******************************************************************************
* Function Name: init_leds
********************************************************************************/
static int32_t init_leds(void)
{
    gpio_config_t led_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_DEF_OUTPUT,
        .pin_bit_mask = ((1ULL << PIN_LED_RED) | (1ULL << PIN_LED_GREEN)),
        .pull_down_en = 0,
        .pull_up_en = 1
    };

    if (gpio_config(&led_conf) != ESP_OK)
    {
        return -1;
    }

    gpio_set_level(PIN_LED_RED, 0);
    gpio_set_level(PIN_LED_GREEN, 0);

    return 0;
}

/* [] END OF FILE */
