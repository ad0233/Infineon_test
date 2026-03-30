#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "cli_task.h"
#include "ifx_sensor_dsp.h"
#include "resource_map.h"
#include "xensiv_bgt60trxx_esp.h"
#include "xensiv_radar_presence.h"

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
#define XENSIV_BGT60TRXX_SPI_FREQUENCY      (1000000UL)

#define RADAR_PROFILE_START_FREQ_HZ         (58000000000ULL)
#define RADAR_PROFILE_BANDWIDTH_HZ          (4500000000ULL)
#define RADAR_PROFILE_NUM_SAMPLES_PER_CHIRP (64U)
#define RADAR_PROFILE_NUM_CHIRPS_PER_FRAME  (64U)
#define RADAR_PROFILE_NUM_RX_ANTENNAS       (3U)
#define RADAR_PROFILE_ADC_DIV               (60U)
#define RADAR_PROFILE_VGA_GAIN_RX1          (3U)

#define RADAR_DISTANCE_THRESHOLD_DB         (2.1f)
#define RADAR_DISTANCE_LOG_INTERVAL_MS      (250U)
#define RADAR_NO_TARGET_LOG_INTERVAL_MS     (1000U)
#define RADAR_DISTANCE_FIRST_VALID_BIN      (4U)

#define RADAR_PRESENCE_MIN_RANGE_BIN        (1)
#define RADAR_PRESENCE_MAX_RANGE_M          (0.67f)

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
static bool radar_detect_nearest_target_cm(float32_t *frame,
                                           float32_t *distance_cm,
                                           int32_t *range_bin,
                                           float32_t *level_db);
void presence_detection_cb(xensiv_radar_presence_handle_t handle,
                           const xensiv_radar_presence_event_t* event,
                           void *data);


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

/* float32 buffer for one chirp (RX antenna 0), fed to presence algorithm */
static float32_t chirp_frame[NUM_SAMPLES_PER_CHIRP];
static float32_t chirp_distance_frame[NUM_SAMPLES_PER_CHIRP];
static float32_t distance_window[NUM_SAMPLES_PER_CHIRP];
static cfloat32_t distance_range_fft[NUM_SAMPLES_PER_CHIRP / 2U];

static xensiv_radar_presence_handle_t presence_handle = NULL;

static TaskHandle_t radar_task_handler;
static uint32_t radar_frame_counter = 0;
static uint32_t radar_fifo_error_counter = 0;
static uint32_t radar_fifo_consecutive_errors = 0;
static uint32_t radar_distance_last_log_ms = 0;
static uint32_t radar_no_target_last_log_ms = 0;


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

    xensiv_radar_presence_config_t presence_config;
    xensiv_radar_presence_init_config(&presence_config);
    presence_config.num_samples_per_chirp = NUM_SAMPLES_PER_CHIRP;
    presence_config.micro_fft_size = NUM_SAMPLES_PER_CHIRP;
    presence_config.bandwidth = RADAR_BANDWIDTH_HZ;
    presence_config.min_range_bin = RADAR_PRESENCE_MIN_RANGE_BIN;
    presence_config.max_range_bin = (int32_t)(RADAR_PRESENCE_MAX_RANGE_M /
                                              radar_get_range_bin_length_m());
    if (presence_config.max_range_bin >= (NUM_SAMPLES_PER_CHIRP / 2))
    {
        presence_config.max_range_bin = (NUM_SAMPLES_PER_CHIRP / 2) - 1;
    }
    presence_config.macro_threshold = 0.5f;
    presence_config.micro_threshold = 10.0f;
    presence_config.mode = XENSIV_RADAR_PRESENCE_MODE_MICRO_IF_MACRO;

    if (xensiv_radar_presence_alloc(&presence_handle, &presence_config) != XENSIV_RADAR_PRESENCE_OK)
    {
        ESP_LOGE(TAG, "Presence alloc failed");
        vTaskDelete(NULL);
        return;
    }

    xensiv_radar_presence_set_callback(presence_handle, presence_detection_cb, NULL);
    ifx_window_hann_f32(distance_window, NUM_SAMPLES_PER_CHIRP);

    if (xensiv_bgt60trxx_start_frame(&bgt60_obj.dev, true) != XENSIV_BGT60TRXX_STATUS_OK)
    {
        ESP_LOGE(TAG, "start_frame failed");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Radar presence detection started. bin_length=%.3fm",
             xensiv_radar_presence_get_bin_length(presence_handle));

    for (;;)
    {
        while (gpio_get_level(PIN_XENSIV_BGT60TRXX_IRQ) == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        int32_t fifo_status = xensiv_bgt60trxx_get_fifo_data(&bgt60_obj.dev,
                                                             bgt60_buffer,
                                                             NUM_SAMPLES_PER_FRAME);
        if (fifo_status != XENSIV_BGT60TRXX_STATUS_OK)
        {
            vTaskDelay(pdMS_TO_TICKS(2));
            fifo_status = xensiv_bgt60trxx_get_fifo_data(&bgt60_obj.dev,
                                                         bgt60_buffer,
                                                         NUM_SAMPLES_PER_FRAME);
            if (fifo_status != XENSIV_BGT60TRXX_STATUS_OK)
            {
                uint32_t fifo_hw_status = 0;
                int32_t fifo_hw_status_rslt =
                    xensiv_bgt60trxx_get_fifo_status(&bgt60_obj.dev, &fifo_hw_status);

                radar_fifo_error_counter++;
                radar_fifo_consecutive_errors++;
                ESP_LOGW(TAG,
                         "get_fifo_data failed (%" PRIu32 " total, %" PRIu32 " consecutive), "
                         "status=%" PRIi32 ", fstat_rslt=%" PRIi32 ", fstat=0x%06" PRIX32,
                         radar_fifo_error_counter,
                         radar_fifo_consecutive_errors,
                         fifo_status,
                         fifo_hw_status_rslt,
                         fifo_hw_status);

                while (gpio_get_level(PIN_XENSIV_BGT60TRXX_IRQ) != 0)
                {
                    vTaskDelay(pdMS_TO_TICKS(1));
                }

                if (radar_fifo_consecutive_errors >= 3U)
                {
                    ESP_LOGW(TAG, "Restarting radar frame generator after repeated FIFO errors");
                    (void)xensiv_bgt60trxx_start_frame(&bgt60_obj.dev, false);
                    vTaskDelay(pdMS_TO_TICKS(2));
                    (void)xensiv_bgt60trxx_set_fifo_limit(&bgt60_obj.dev, NUM_SAMPLES_PER_FRAME);
                    (void)xensiv_bgt60trxx_start_frame(&bgt60_obj.dev, true);
                    radar_fifo_consecutive_errors = 0;
                }
                continue;
            }
        }

        radar_fifo_consecutive_errors = 0;

        while (gpio_get_level(PIN_XENSIV_BGT60TRXX_IRQ) != 0)
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        for (int i = 0; i < NUM_SAMPLES_PER_CHIRP; i++)
        {
            chirp_frame[i] = ((float32_t)bgt60_buffer[i * NUM_RX_ANTENNAS] - 2048.0f) / 2048.0f;
        }

        memcpy(chirp_distance_frame, chirp_frame, sizeof(chirp_distance_frame));

        radar_frame_counter++;
        if ((radar_frame_counter % 32U) == 0U)
        {
            ESP_LOGI(TAG, "Radar frames processed: %" PRIu32, radar_frame_counter);
        }

        uint32_t time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        {
            float32_t nearest_distance_cm = 0.0f;
            float32_t nearest_level_db = 0.0f;
            int32_t nearest_range_bin = -1;

            if (radar_detect_nearest_target_cm(chirp_distance_frame,
                                               &nearest_distance_cm,
                                               &nearest_range_bin,
                                               &nearest_level_db))
            {
                radar_no_target_last_log_ms = time_ms;
                if ((radar_distance_last_log_ms == 0U) ||
                    ((time_ms - radar_distance_last_log_ms) >= RADAR_DISTANCE_LOG_INTERVAL_MS))
                {
                    ESP_LOGI(TAG,
                             "Nearest target: %.1f cm (bin=%" PRIi32 ", level=%.2f dB)",
                             nearest_distance_cm,
                             nearest_range_bin,
                             nearest_level_db);
                    radar_distance_last_log_ms = time_ms;
                }
            }
            else if ((radar_no_target_last_log_ms == 0U) ||
                     ((time_ms - radar_no_target_last_log_ms) >= RADAR_NO_TARGET_LOG_INTERVAL_MS))
            {
                ESP_LOGI(TAG, "No target above threshold %.2f dB", RADAR_DISTANCE_THRESHOLD_DB);
                radar_no_target_last_log_ms = time_ms;
            }
        }

        xensiv_radar_presence_process_frame(presence_handle, chirp_frame, time_ms);
    }
}


/*******************************************************************************
* Function Name: presence_detection_cb
********************************************************************************/
void presence_detection_cb(xensiv_radar_presence_handle_t handle,
                           const xensiv_radar_presence_event_t* event,
                           void *data)
{
    (void)handle;
    (void)data;

    switch (event->state)
    {
        case XENSIV_RADAR_PRESENCE_STATE_MACRO_PRESENCE:
            gpio_set_level(PIN_LED_RED, 1);
            gpio_set_level(PIN_LED_GREEN, 0);
            ESP_LOGI(TAG, "[MACRO PRESENCE] range_bin=%" PRIi32 " time=%" PRIi32 "ms",
                     event->range_bin, event->timestamp);
            break;

        case XENSIV_RADAR_PRESENCE_STATE_MICRO_PRESENCE:
            gpio_set_level(PIN_LED_RED, 1);
            gpio_set_level(PIN_LED_GREEN, 0);
            ESP_LOGI(TAG, "[MICRO PRESENCE] range_bin=%" PRIi32 " time=%" PRIi32 "ms",
                     event->range_bin, event->timestamp);
            break;

        case XENSIV_RADAR_PRESENCE_STATE_ABSENCE:
            gpio_set_level(PIN_LED_RED, 0);
            gpio_set_level(PIN_LED_GREEN, 1);
            ESP_LOGI(TAG, "[ABSENCE] time=%" PRIu32 "ms", event->timestamp);
            break;

        default:
            ESP_LOGW(TAG, "Unknown presence state");
            break;
    }
}


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

    if (xensiv_bgt60trxx_set_fifo_limit(&bgt60_obj.dev, NUM_SAMPLES_PER_FRAME) !=
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
                 "Range bin length=%.3fm threshold=%.2fdB presence_max_bin=%" PRIi32,
                 radar_get_range_bin_length_m(),
                 RADAR_DISTANCE_THRESHOLD_DB,
                 (int32_t)(RADAR_PRESENCE_MAX_RANGE_M / radar_get_range_bin_length_m()));
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
* Function Name: radar_detect_nearest_target_cm
********************************************************************************/
static bool radar_detect_nearest_target_cm(float32_t *frame,
                                           float32_t *distance_cm,
                                           int32_t *range_bin,
                                           float32_t *level_db)
{
    if (ifx_range_fft_f32(frame,
                          distance_range_fft,
                          true,
                          distance_window,
                          NUM_SAMPLES_PER_CHIRP,
                          1U) != IFX_SENSOR_DSP_STATUS_OK)
    {
        ESP_LOGW(TAG, "ifx_range_fft_f32 failed in distance detection");
        return false;
    }

    for (int32_t bin = (int32_t)RADAR_DISTANCE_FIRST_VALID_BIN;
         bin < (int32_t)(NUM_SAMPLES_PER_CHIRP / 2U);
         bin++)
    {
        float32_t current_level_db = radar_calculate_fft_level_db(distance_range_fft, bin);
        if (current_level_db > RADAR_DISTANCE_THRESHOLD_DB)
        {
            *distance_cm = radar_calculate_distance_cm_from_bin(bin);
            *range_bin = bin;
            *level_db = current_level_db;
            return true;
        }
    }

    return false;
}


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
