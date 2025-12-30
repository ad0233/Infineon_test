#include <inttypes.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "cli_task.h"
#include "resource_map.h"
#include "xensiv_bgt60trxx_esp.h"
#include "xensiv_radar_presence.h"

#include "my_lidar_inf.h"
#include "esp_log.h"

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

/* Defaults to averaging of samples across chirps, may affect sensitivity depending on use case 
 * Uncomment out to only use the samples in the first chirp */
//#define USE_FIRST_CHIRP_ONLY

/*******************************************************************************
* Macros
********************************************************************************/
#define XENSIV_BGT60TRXX_SPI_FREQUENCY      (20000000UL)

#define NUM_SAMPLES_PER_FRAME               (XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP *\
                                             XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME *\
                                             XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS)

#define NUM_CHIRPS_PER_FRAME                XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME
#define NUM_SAMPLES_PER_CHIRP               XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP

/* RTOS tasks */
#define RADAR_TASK_NAME                     "radar_task"
#define RADAR_TASK_STACK_SIZE               (configMINIMAL_STACK_SIZE * 4)
#define RADAR_TASK_PRIORITY                 (configMAX_PRIORITIES - 1)
#define PROCESSING_TASK_NAME                "processing_task"
#define PROCESSING_TASK_STACK_SIZE          (configMINIMAL_STACK_SIZE * 4)
#define PROCESSING_TASK_PRIORITY            (configMAX_PRIORITIES - 2)

#define CLI_TASK_NAME                       "cli_task"
#define CLI_TASK_STACK_SIZE                 (configMINIMAL_STACK_SIZE * 10)
#define CLI_TASK_PRIORITY                   (tskIDLE_PRIORITY)


/*******************************************************************************
* Function Prototypes
********************************************************************************/
static void radar_task(void *pvParameters);
static void processing_task(void *pvParameters);
static void timer_callback(TimerHandle_t xTimer);

static int32_t init_leds(void);
static int32_t init_sensor(void);
static void xensiv_bgt60trxx_interrupt_handler(void* args);
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

static float32_t frame[NUM_SAMPLES_PER_FRAME];
static float32_t avg_chirp[NUM_SAMPLES_PER_CHIRP];

static TaskHandle_t radar_task_handler;
static TaskHandle_t processing_task_handler;
static TimerHandle_t timer_handler;

int my_lidar_inf_init(void)
{    
    timer_handler = xTimerCreate("timer", pdMS_TO_TICKS(1000), pdTRUE, NULL, timer_callback);    
    if (timer_handler == NULL)
    {
        ESP_LOGE("my_lidar_inf_init", "xTimerCreate failed");
        return -1;
    }

    if (xTimerStart(timer_handler, 0) != pdPASS)
    {
        ESP_LOGE("my_lidar_inf_init", "xTimerStart failed");
        return -1;
    }

    /* Create the radar task */    
    if (xTaskCreatePinnedToCore(radar_task, RADAR_TASK_NAME, RADAR_TASK_STACK_SIZE, NULL, RADAR_TASK_PRIORITY, &radar_task_handler, 0) != pdPASS)
    {
        ESP_LOGE("my_lidar_inf_init", "xTaskCreatePinnedToCore failed");
        return -1;
    }    

    return 0;
}

/*******************************************************************************
* Function Name: radar_task
********************************************************************************
* Summary:
* This is the radar task.
*    1. Create the processing RTOS task
*    2. Initializes the hardware interface to the sensor and LEDs
*    3. Initializes the radar device
*    4. In an infinite loop
*       - Waits for interrupt from radar device indicating availability of data
*       - Reads the data, converts it to floating point and notifies the processing task
* Parameters:
*  void
*
* Return:
*  none
*
*******************************************************************************/
static void radar_task(void *pvParameters)
{
    (void)pvParameters;

    /* Create the processing task */
    if (xTaskCreatePinnedToCore(processing_task, PROCESSING_TASK_NAME, PROCESSING_TASK_STACK_SIZE, NULL, PROCESSING_TASK_PRIORITY, &processing_task_handler, 1) != pdPASS)
    {
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    if (init_sensor() != 0)
    {
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    if (init_leds () != 0)
    {
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");

    printf("****************** "
           "XENSIV BGT60TR13C radar solution demo "
           "****************** \r\n\n"
           "Human presence detection using XENSIV BGT60TR13C radar and ESP32\r\n"
           );

    printf("Press ENTER to enter setup mode, press ESC to quit setup mode \r\n");

    if (xensiv_bgt60trxx_start_frame(&bgt60_obj.dev, true) != XENSIV_BGT60TRXX_STATUS_OK)
    {
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    for(;;)
    {
        /* Wait for the GPIO interrupt to indicate that another slice is available */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (xensiv_bgt60trxx_get_fifo_data(&bgt60_obj.dev, 
                                           bgt60_buffer,
                                           NUM_SAMPLES_PER_FRAME) == XENSIV_BGT60TRXX_STATUS_OK)
        {
            /* Tell processing task to take over */
            xTaskNotifyGive(processing_task_handler);
        }
        else
        {
            printf("Error receiving fifo data\n");
        }
    }
}


/*******************************************************************************
* Function Name: processing_task
********************************************************************************
* Summary:
* This is the data processing task.
*    1. Initializes the presence sensing library and register an event callback
*    2. It creates a console task to handle parameter configuration for the library
*    3. In a loop
*       - receives notification from main task
*       - do necessary preprocessing on received buffer data
*       - executes the presence algorithm and provides the result on terminal and LEDs
*
* Parameters:
*  void
*
* Return:
*  None
*
*******************************************************************************/
static void processing_task(void *pvParameters)
{
    (void)pvParameters;

    static const xensiv_radar_presence_config_t default_config =
    {
        .bandwidth                         = 460E6,
        .num_samples_per_chirp             = XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP,
        .micro_fft_decimation_enabled      = false,
        .micro_fft_size                    = 128,
        .macro_threshold                   = 0.5f,
        .micro_threshold                   = 12.5f,
        .min_range_bin                     = 1,
        .max_range_bin                     = 5,
        .macro_compare_interval_ms         = 250,
        .macro_movement_validity_ms        = 1000,
        .micro_movement_validity_ms        = 4000,
        .macro_movement_confirmations      = 0,
        .macro_trigger_range               = 1,
        .mode                              = XENSIV_RADAR_PRESENCE_MODE_MICRO_IF_MACRO,
        .macro_fft_bandpass_filter_enabled = false,
        .micro_movement_compare_idx        = 5
    };

    xensiv_radar_presence_handle_t handle;

    xensiv_radar_presence_set_malloc_free(pvPortMalloc,
                                          vPortFree);

    if (xensiv_radar_presence_alloc(&handle, &default_config) != 0)
    {
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    xensiv_radar_presence_set_callback(handle, presence_detection_cb, NULL);
    
    if (xTaskCreate(console_task, CLI_TASK_NAME, CLI_TASK_STACK_SIZE, handle, CLI_TASK_PRIORITY, NULL) != pdPASS)
    {
        ESP_ERROR_CHECK(ESP_FAIL);
    } 

    for(;;)
    {
        /* Wait for frame data available to process */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Data preprocessing */
        uint16_t *bgt60_buffer_ptr = &bgt60_buffer[0];
        float32_t *frame_ptr = &frame[0];  

        for (int32_t sample = 0; sample < NUM_SAMPLES_PER_FRAME; ++sample)
        {
            *frame_ptr++ = ((float32_t)(*bgt60_buffer_ptr++) / 4096.0F);
        }

#ifndef USE_FIRST_CHIRP_ONLY
        // Zero out the avg_chirp arr
        for (int sample = 0; sample < NUM_SAMPLES_PER_CHIRP; sample++)
        {
            avg_chirp[sample] = 0.0f;
        }
        

        // Accumulate sum of samples across chirps
        for (int chirp = 0; chirp < NUM_CHIRPS_PER_FRAME; chirp++)
        {
            dsps_add_f32(avg_chirp, &frame[NUM_SAMPLES_PER_CHIRP * chirp], avg_chirp, NUM_SAMPLES_PER_CHIRP,
                            1, 1, 1);
        }

        // Calculate mean from sum
        dsps_mulc_f32(avg_chirp, avg_chirp, NUM_SAMPLES_PER_CHIRP, 1.0f / (float32_t) NUM_CHIRPS_PER_FRAME,
                        1, 1);

        xensiv_radar_presence_process_frame(handle, avg_chirp, xTaskGetTickCount() * portTICK_PERIOD_MS);
#else
        xensiv_radar_presence_process_frame(handle, frame, xTaskGetTickCount() * portTICK_PERIOD_MS);
#endif        
    }
}


/*******************************************************************************
* Function Name: presence_detection_cb
********************************************************************************
* Summary:
* This is the callback function o indicate presence/absence events on terminal 
* and LEDs.
* Parameters:
*  void
*
* Return:
*  None
*
*******************************************************************************/
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
            printf("[INFO] macro presence %" PRIi32 " %" PRIi32 "\n",
                   event->range_bin,
                   event->timestamp);
            break;

        case XENSIV_RADAR_PRESENCE_STATE_MICRO_PRESENCE:
            gpio_set_level(PIN_LED_RED, 1);    
            gpio_set_level(PIN_LED_GREEN, 0);   
            printf("[INFO] micro presence %" PRIi32 " %" PRIi32 "\n",
                    event->range_bin,
                    event->timestamp);
            break;

        case XENSIV_RADAR_PRESENCE_STATE_ABSENCE:
            gpio_set_level(PIN_LED_RED, 0);    
            gpio_set_level(PIN_LED_GREEN, 1);   
            printf("[INFO] absence %" PRIu32 "\n", event->timestamp);
            break;

        default:
            printf("[WARN]: Unknown reported state in event handling\n");
            break;
    }
}


/*******************************************************************************
* Function Name: init_sensor
********************************************************************************
* Summary:
* This function configures the SPI interface, initializes radar and interrupt
* service routine to indicate the availability of radar data. 
* 
* Parameters:
*  void
*
* Return:
*  Success or error 
*
*******************************************************************************/
static int32_t init_sensor(void)
{
#ifdef USE_FIFO_SPI_TRANSACTION
    bool use_dma = false;
#else
    bool use_dma = true;
#endif

    /* Initialize the SPI interface to BGT60. */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_XENSIV_BGT60TRXX_SPI_MOSI,
        .miso_io_num = PIN_XENSIV_BGT60TRXX_SPI_MISO,
        .sclk_io_num = PIN_XENSIV_BGT60TRXX_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = NUM_SAMPLES_PER_FRAME * 2
    };

    /* Using DMA and single transaction */
    if (spi_bus_initialize(SPI3_HOST, &bus_cfg,
                           use_dma ? SPI_DMA_CH_AUTO : SPI_DMA_DISABLED) != ESP_OK)
    {
        printf("ERROR: spi_bus_initialize failed\n");
        return -1;
    }

    /* Set SPI data rate to communicate with sensor */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = XENSIV_BGT60TRXX_SPI_FREQUENCY,
        .mode = 0,
        .spics_io_num = PIN_XENSIV_BGT60TRXX_SPI_CSN,
        .queue_size = 1,
        .pre_cb = NULL,
        .post_cb = NULL
    };

    if (spi_bus_add_device(SPI3_HOST, &dev_cfg, &spi_obj) != ESP_OK)
    {
        printf("ERROR: spi_bus_add_device failed\n");
        return -1;
    }


    /* Enable the LDO. */
    gpio_config_t ldo_cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_DEF_OUTPUT,
        .pin_bit_mask = (1ULL << PIN_XENSIV_BGT60TRXX_LDO_EN),
        .pull_down_en = 0,
        .pull_up_en = 1
    };

    if (gpio_config(&ldo_cfg) != ESP_OK)
    {
        printf("ERROR: LDO gpio_config failed\n");
        return -1;
    }

    gpio_set_level(PIN_XENSIV_BGT60TRXX_LDO_EN, 1);

    /* Wait LDO stable */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Use a DMA or FIFO for SPI transaction */
    if (xensiv_bgt60trxx_esp_init(&bgt60_obj,
                                  spi_obj,
                                  PIN_XENSIV_BGT60TRXX_SPI_CSN,
                                  PIN_XENSIV_BGT60TRXX_RSTN,
                                  use_dma,
                                  register_list,
                                  XENSIV_BGT60TRXX_CONF_NUM_REGS) != ESP_OK) 
    {
        printf("ERROR: xensiv_bgt60trxx_esp_init failed\n");
        return -1;
    }

    /* The sensor will generate an interrupt once the sensor FIFO level is
       NUM_SAMPLES_PER_FRAME */
    if (xensiv_bgt60trxx_esp_interrupt_init(&bgt60_obj,
                                            NUM_SAMPLES_PER_FRAME,
                                            PIN_XENSIV_BGT60TRXX_IRQ,
                                            xensiv_bgt60trxx_interrupt_handler,
                                            NULL) != ESP_OK)
    {
        printf("ERROR: xensiv_bgt60trxx_esp_interrupt_init failed\n");
        return -1;
    }                                                 

    return 0;
}


/*******************************************************************************
* Function Name: xensiv_bgt60trxx_interrupt_handler
********************************************************************************
* Summary:
* This is the interrupt handler to react on sensor indicating the availability 
* of new data
*    1. Notifies radar task on interrupt from sensor
*
* Parameters:
*  void
*
* Return:
*  none
*
*******************************************************************************/
static void IRAM_ATTR xensiv_bgt60trxx_interrupt_handler(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    vTaskNotifyGiveFromISR(radar_task_handler, &xHigherPriorityTaskWoken);

    /* Context switch needed? */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


/*******************************************************************************
* Function Name: init_leds
********************************************************************************
* Summary:
* This function initializes the GPIOs for LEDs and set them to off state.
* Parameters:
*  void
*
* Return:
*  Success or error
*
*******************************************************************************/
static int32_t init_leds(void)
{
    gpio_config_t led_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_DEF_OUTPUT,
        .pin_bit_mask = ((1ULL<<PIN_LED_RED) | (1ULL<<PIN_LED_GREEN)),
        .pull_down_en = 0,
        .pull_up_en = 1
    };

    if (gpio_config(&led_conf) != ESP_OK)
    {
        return -1;
    }
    
    gpio_set_level(PIN_LED_RED, 0);    
    gpio_set_level(PIN_LED_GREEN, 0);   
    //gpio_set_level(PIN_LED_BLUE, 0); Blue pin is input only

    return 0;
}


/*******************************************************************************
* Function Name: timer_callback
********************************************************************************
* Summary:
* This is the timer_callback
*
* Parameters:
*  void
*
* Return:
*  none
*
*******************************************************************************/
static void timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
}

/* [] END OF FILE */