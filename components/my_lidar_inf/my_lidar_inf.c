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
#include "esp_log.h"
#include "driver/gpio.h"

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
#define RADAR_DISTANCE_LOG_INTERVAL_MS      (250U)
#define RADAR_IRQ_WAIT_HIGH_TIMEOUT_MS      (250U)
#define RADAR_IRQ_WAIT_LOW_TIMEOUT_MS       (20U)
#define RADAR_MEASUREMENT_REARM_DELAY_MS    (100U)
#define RADAR_DISTANCE_FIRST_VALID_BIN      (8U)              /* bin8=30cm, 官方有效范围起点 */
#define RADAR_MAIN_RX_IDX                   (1U)              /* 参考脚本 MAIN_RX_IDX=1 */
#define RADAR_RANGE_TRACK_HISTORY_LEN       (5U)
#define RADAR_RANGE_TRACK_LOCAL_RADIUS_BINS (3)
#define RADAR_RANGE_TRACK_SWITCH_RATIO      (1.35f)
#define RADAR_RANGE_TRACK_MAX_STEP_BINS     (1)
#define RADAR_PRESENCE_HISTORY_LEN          (100U)            /* 10Hz * 10s */
#define RADAR_PRESENCE_FRAME_RATE_HZ        (10.0f)
#define RADAR_PRESENCE_ENERGY_RADIUS_BINS   (2)
#define RADAR_PHASE_WINDOW_SECONDS          (6.0f)
#define RADAR_AMP_WINDOW_SECONDS            (5.0f)
#define RADAR_BIN_WINDOW_SECONDS            (5.0f)
#define RADAR_BREATH_WINDOW_SECONDS         (10.0f)
#define RADAR_PRESENCE_PEAK_HEIGHT          (0.03f)
#define RADAR_PRESENCE_PHASE_EXC_MM_TH      (0.08f)
#define RADAR_PRESENCE_AMP_CV_TH            (0.35f)
#define RADAR_PRESENCE_BIN_SPAN_TH          (2.0f)
#define RADAR_PRESENCE_CONFIDENCE_TH        (0.35f)
#define RADAR_PRESENCE_MISS_LIMIT           (3U)
#define RADAR_PHASE_DIFF_CLIP               (0.30f)
#define RADAR_PHASE_SMOOTH_LEN              (7U)
#define RADAR_WAVELENGTH_MM                 (5.0f)            /* c / 60GHz */
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
#define RADAR_TASK_STACK_SIZE               (configMINIMAL_STACK_SIZE * 8)
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
static void radar_append_float_history(float32_t *buffer,
                                       uint32_t *count,
                                       uint32_t capacity,
                                       float32_t value);
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
} radar_frame_result_t;


typedef struct
{
    int32_t search_bin_history[RADAR_RANGE_TRACK_HISTORY_LEN];
    uint32_t search_bin_count;
    float32_t phase_history[RADAR_PRESENCE_HISTORY_LEN];
    float32_t amplitude_history[RADAR_PRESENCE_HISTORY_LEN];
    int32_t bin_history[RADAR_PRESENCE_HISTORY_LEN];
    uint32_t history_count;
    bool has_unwrapped_phase;
    float32_t last_unwrapped_phase;
    bool presence_latched;
    uint32_t presence_misses;
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
             RADAR_PRESENCE_PHASE_EXC_MM_TH,
             RADAR_PRESENCE_AMP_CV_TH,
             RADAR_PRESENCE_BIN_SPAN_TH);

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
        s_radar_data.frame_counter = radar_frame_counter;
        export_snapshot = s_radar_data;
        taskEXIT_CRITICAL(&s_radar_data_mux);

        if ((radar_distance_last_log_ms == 0U) ||
            ((time_ms - radar_distance_last_log_ms) >= RADAR_DISTANCE_LOG_INTERVAL_MS))
        {
            if (export_snapshot.target_detected != 0U)
            {
                ESP_LOGI(TAG,
                         "Distance export: detected=yes distance=%.1fcm bin=%" PRIi32
                         " level=%.2fdB movement=%.3f frame=%" PRIu32,
                         export_snapshot.distance_cm,
                         export_snapshot.range_bin,
                         export_snapshot.signal_db,
                         export_snapshot.movement_energy,
                         export_snapshot.frame_counter);
            }
            else
            {
                ESP_LOGI(TAG,
                         "Distance export: detected=no candidate=%.1fcm bin=%" PRIi32
                         " level=%.2fdB movement=%.3f frame=%" PRIu32
                         " threshold=%.2fdB",
                         export_snapshot.distance_cm,
                         export_snapshot.range_bin,
                         export_snapshot.signal_db,
                         export_snapshot.movement_energy,
                         export_snapshot.frame_counter,
                         RADAR_DISTANCE_THRESHOLD_DB);
            }
            ESP_LOGI(TAG,
                     "Presence export: detected=%s confidence=%.2f distance=%.1fcm "
                     "phase_exc=%.3fmm amp_cv=%.3f bin_span=%.1f flags(breath=%u phase=%u amp=%u bin=%u)",
                     (export_snapshot.presence_detected != 0U) ? "yes" : "no",
                     export_snapshot.presence_confidence,
                     export_snapshot.presence_distance_cm,
                     export_snapshot.phase_excursion_mm,
                     export_snapshot.amplitude_cv,
                     export_snapshot.bin_span,
                     (unsigned)export_snapshot.breath_present,
                     (unsigned)export_snapshot.phase_present,
                     (unsigned)export_snapshot.amplitude_present,
                     (unsigned)export_snapshot.bin_present);
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
* Function Name: radar_append_float_history
********************************************************************************/
static void radar_append_float_history(float32_t *buffer,
                                       uint32_t *count,
                                       uint32_t capacity,
                                       float32_t value)
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

    memmove(buffer, buffer + 1, sizeof(float32_t) * (capacity - 1U));
    buffer[capacity - 1U] = value;
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
* Function Name: radar_update_presence
********************************************************************************/
static void radar_update_presence(void *result_ptr)
{
    radar_frame_result_t *result = (radar_frame_result_t *)result_ptr;
    if (result == NULL)
    {
        return;
    }

    if (s_presence_state.history_count < RADAR_PRESENCE_HISTORY_LEN)
    {
        const uint32_t index = s_presence_state.history_count;
        s_presence_state.phase_history[index] = result->phase_unwrapped;
        s_presence_state.amplitude_history[index] = result->amplitude_metric;
        s_presence_state.bin_history[index] = result->range_bin;
        s_presence_state.history_count++;
    }
    else
    {
        memmove(s_presence_state.phase_history,
                s_presence_state.phase_history + 1,
                sizeof(float32_t) * (RADAR_PRESENCE_HISTORY_LEN - 1U));
        memmove(s_presence_state.amplitude_history,
                s_presence_state.amplitude_history + 1,
                sizeof(float32_t) * (RADAR_PRESENCE_HISTORY_LEN - 1U));
        memmove(s_presence_state.bin_history,
                s_presence_state.bin_history + 1,
                sizeof(int32_t) * (RADAR_PRESENCE_HISTORY_LEN - 1U));
        s_presence_state.phase_history[RADAR_PRESENCE_HISTORY_LEN - 1U] = result->phase_unwrapped;
        s_presence_state.amplitude_history[RADAR_PRESENCE_HISTORY_LEN - 1U] = result->amplitude_metric;
        s_presence_state.bin_history[RADAR_PRESENCE_HISTORY_LEN - 1U] = result->range_bin;
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

    const float32_t *phase_recent = &s_presence_state.phase_history[total_count - phase_count];
    const float32_t *amp_recent = &s_presence_state.amplitude_history[total_count - amp_count];
    const int32_t *bin_recent = &s_presence_state.bin_history[total_count - bin_count];

    result->phase_excursion_mm =
        radar_percentile_abs_dev(phase_recent, phase_count, 90.0f) *
        (RADAR_WAVELENGTH_MM / (4.0f * RADAR_PI_F));
    result->phase_present = (result->phase_excursion_mm >= RADAR_PRESENCE_PHASE_EXC_MM_TH);

    float32_t amp_abs[RADAR_PRESENCE_HISTORY_LEN];
    for (uint32_t i = 0; i < amp_count; i++)
    {
        amp_abs[i] = fabsf(amp_recent[i]);
    }
    const float32_t amp_median = fmaxf(radar_median_float(amp_abs, amp_count), 1.0e-6f);
    result->amplitude_cv = radar_std_float(amp_recent, amp_count) / amp_median;
    result->amplitude_present = (result->amplitude_cv <= RADAR_PRESENCE_AMP_CV_TH);

    int32_t bin_min = bin_recent[0];
    int32_t bin_max = bin_recent[0];
    for (uint32_t i = 1; i < bin_count; i++)
    {
        if (bin_recent[i] < bin_min)
        {
            bin_min = bin_recent[i];
        }
        if (bin_recent[i] > bin_max)
        {
            bin_max = bin_recent[i];
        }
    }
    result->bin_span = (float32_t)(bin_max - bin_min);
    result->bin_present = (result->bin_span <= RADAR_PRESENCE_BIN_SPAN_TH);

    float32_t breath_processed[RADAR_PRESENCE_HISTORY_LEN];
    memset(breath_processed, 0, sizeof(breath_processed));
    radar_process_phase_signal(&s_presence_state.phase_history[total_count - breath_count],
                               breath_count,
                               breath_processed);
    const uint32_t peaks_pos = radar_count_peaks(breath_processed,
                                                 breath_count,
                                                 RADAR_PRESENCE_PEAK_HEIGHT,
                                                 true);
    const uint32_t peaks_neg = radar_count_peaks(breath_processed,
                                                 breath_count,
                                                 RADAR_PRESENCE_PEAK_HEIGHT,
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

    const bool raw_present =
        (confidence >= RADAR_PRESENCE_CONFIDENCE_TH) ||
        result->breath_present ||
        (result->phase_present && result->bin_present);

    if (raw_present)
    {
        s_presence_state.presence_latched = true;
        s_presence_state.presence_misses = 0U;
    }
    else if (s_presence_state.presence_latched &&
             (s_presence_state.presence_misses < RADAR_PRESENCE_MISS_LIMIT))
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
