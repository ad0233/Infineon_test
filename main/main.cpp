/* volc rtc example code

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
 
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <algorithm>
#include <string>

#include "freertos/idf_additions.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "audio_sys.h"
#include "audio_thread.h"
#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "periph_spiffs.h"
#include "periph_sdcard.h"
#include "audio_mem.h"
#include "pwm_control.h"
#include "board.h"

#include "my_lcd.h"
#include "encoder.h"

#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "ui.h"
#include "my_ui_behavior.h"
#include "my_rtc.h"
#include "my_lidar.h"
#include "my_nvs.h"
#include "my_ble.h"
#include "my_wifi.h"
#include "my_ota.h"

#include "rust_lunawake.h"
#include "cmd_parse.h"

#include "fsm_main.h"
#include "my_h264.h"

// 时间调整函数 - 根据编码器变化调整时间
static void adjust_time_by_encoder(int32_t diff, uint8_t *hour, uint8_t *min) {
    // 计算新的分钟值
    int new_min = *min + diff;
    
    // 处理分钟进位和借位
    if(new_min > 59) {
        // 分钟超过59，需要进位
        *min = new_min - 60;
        *hour += 1;
        if(*hour > 23) {
            *hour = 0;  // 小时超过23，回到0
        }
    } else if(new_min < 0) {
        // 分钟小于0，需要借位
        *min = new_min + 60;
        if(*hour == 0) {
            *hour = 23;  // 小时为0时，借位后变成23
        } else {
            *hour -= 1;
        }
    } else {
        // 正常情况，直接设置分钟
        *min = new_min;
    }
}

// 人体存在检测数据回调函数
void human_presence_callback(const radar_human_data_t *data)
{
    // 有人没人 - 状态变化时上报
    // ESP_LOGI("HUMAN", "有人: %s", data->presence ? "是" : "否");
    my_ui_radar_update_presence(data->presence);
}

// 体动参数数据回调函数（已不使用，保留接口）
void human_movement_callback(const radar_human_data_t *data)
{
    // 体动参数功能已移除，不再使用
    // my_ui_radar_update_movement 已重新调试，不依赖体动参数
}

// 呼吸监测数据回调函数
void respiratory_data_callback(const radar_respiratory_data_t *data)
{
    if (data->respiratory_switch && data->respiratory_value > 0) {
        // ESP_LOGI("RESPIRATORY", "呼吸: %d 次/min", data->respiratory_value);
        my_ui_radar_update_respiratory(data->respiratory_value);
    }
}

// 心率监测数据回调函数
void heart_rate_data_callback(const radar_heart_rate_data_t *data)
{
    // ESP_LOGI("HEART_RATE", "=== 心率监测数据更新 ===");
    // ESP_LOGI("HEART_RATE", "心率监测开关: %s", data->heart_rate_switch ? "开启" : "关闭");
    
    if (data->heart_rate_switch) {
        // ESP_LOGI("HEART_RATE", "心率数值: %d 次/min", data->heart_rate_value);
        my_ui_radar_update_heart_rate(data->heart_rate_value);
        // ESP_LOGI("HEART_RATE", "心率波形: %02X %02X %02X %02X %02X", 
        //          data->heart_rate_waveform[0], data->heart_rate_waveform[1], 
        //          data->heart_rate_waveform[2], data->heart_rate_waveform[3], 
        //          data->heart_rate_waveform[4]);
    }
    // ESP_LOGI("HEART_RATE", "========================");
}

#include "audio_recorder.h"
#include "audio_processor.h"
#include <my_utils.h>

// #define ENABLE_TASK_MONITOR

static const char *TAG = "main";
static audio_board_handle_t board_handle;

#if defined(ENABLE_TASK_MONITOR)
static void monitor_task(void *arg)
{
    while (1) {
        audio_sys_get_real_time_stats();
        AUDIO_MEM_SHOW(TAG);
        vTaskDelay(10000 / portTICK_RATE_MS);
    }
}
#endif

static void rust_task(void *arg)
{
    my_ui_in_start();
    for (;;) {
        if(esp_log_timestamp() > 3000) break;
        vTaskDelay(1);
    }
    // 进入时钟显示
    
    
    while (1) {
        vTaskDelay(1);
    }
}

void encoder_test(void *arg);
void print_mem_info(void)
{
    ESP_LOGI("MEM", "Internal RAM Free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI("MEM", "SPIRAM Free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI("MEM", "DMA Free: %d bytes", heap_caps_get_free_size(MALLOC_CAP_DMA));
    // heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    // heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
}
extern "C" void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());

    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI("BOOT", "Reset reason: %d", reason);
    
    ESP_LOGI(TAG, "Initialize board peripherals");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    rust_lib_init();
    my_lcd_init();
    my_h264_init([](const uint8_t *rgb565_buf, uint32_t rgb565_buf_len, void *context) {
        my_lcd_draw_rgb565(reinterpret_cast<const uint16_t *>(rgb565_buf), rgb565_buf_len / 2);
    }, NULL, [](void *context) {
        ESP_LOGI(TAG, "my_h264_playback_done, current state: %s", fsm_main_get_current_state_str());
        // lvgl_port_resume();
        // my_lvgl_force_refresh();
        // print_mem_info();
        ESP_LOGI(TAG, "Triggering F_MAIN_E_ANIM_PLAY_SUC event");
        fsm_main_event_trig(F_MAIN_E_ANIM_PLAY_SUC, nullptr);
        ESP_LOGI(TAG, "After trigger, current state: %s", fsm_main_get_current_state_str());
    }, nullptr);
    my_h264_set_fps(24);

    print_mem_info();

    if (fsm_main_init() != 0) {
        ESP_LOGE(TAG, "fsm_main_init failed");
        vTaskDelete(nullptr);
    }
    my_ble_init(NULL);  // 使用默认名称，或传入自定义名称
    my_wifi_init();
    my_rtc_init();

    gpio_set_direction(PA_ENABLE_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(PA_ENABLE_GPIO, 1); // Disable PA
    vTaskDelay(100 / portTICK_PERIOD_MS);

    board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(PA_ENABLE_GPIO, 0); // Enable PA

    print_mem_info();
    xTaskCreate(encoder_test, "encoder_test", 1024 * 4, NULL, 5, NULL);
    // xTaskCreate(rust_task, "rust_task", 8 * 1024, NULL, 5, NULL);

    print_mem_info();

    // 触发状态机初始化事件，启动开机动画
    xTaskCreate([](void *arg) {
        vTaskDelay(100 / portTICK_PERIOD_MS);  // 短暂延迟确保所有初始化完成
        fsm_main_event_trig(F_MAIN_E_INIT, nullptr);
        vTaskDelete(nullptr);
    }, "init_fsm_task", 1024 * 4, NULL, 5, NULL);

    return;
}

void tone_play_callback(audio_element_status_t evt) {
    ESP_LOGI(__func__, "%d", evt);
    if(AEL_STATUS_STATE_FINISHED == evt) {
        // audio_tone_play("spiffs://spiffs/on.wav");
    }
}

static QueueHandle_t event_queue;
static rotary_encoder_t re;

#define RE_A_GPIO   GPIO_NUM_20
#define RE_B_GPIO   GPIO_NUM_39
#define RE_BTN_GPIO GPIO_NUM_38
#define EV_QUEUE_LEN 5

static void alarm_set_tips();

void encoder_test(void *arg)
{
    // Create event queue for rotary encoders
    event_queue = xQueueCreate(EV_QUEUE_LEN, sizeof(rotary_encoder_event_t));

    // Setup rotary encoder library
    ESP_ERROR_CHECK(rotary_encoder_init(event_queue));

    // Add one encoder
    memset(&re, 0, sizeof(rotary_encoder_t));
    re.pin_a = RE_A_GPIO;
    re.pin_b = RE_B_GPIO;
    re.pin_btn = RE_BTN_GPIO;
    ESP_ERROR_CHECK(rotary_encoder_add(&re));

    rotary_encoder_event_t e;
    int32_t val = 0;

    ESP_LOGI(TAG, "Initial value: %" PRIi32, val);
    //clear
    while(xQueueReceive(event_queue, &e, 1) == pdTRUE) {}

    while (1)
    {
        if(xQueueReceive(event_queue, &e, 10) == pdFALSE) {
            continue;
        }
        switch (e.type)
        {
            case RE_ET_BTN_CLICKED:
                fsm_main_event_trig(F_MAIN_E_BTN_CLICKED, nullptr);
                break;
            case RE_ET_BTN_LONG_PRESSED:
                fsm_main_event_trig(F_MAIN_E_BTN_L_CLICKED, nullptr);
                break;
            // case RE_ET_CHANGED:
            // if (e.diff > 0)
            // {
                
            // }
            // // fsm_main_event_trig(F_MAIN_E_TURN, (void *)(&e.diff));
            //     break; 
            default:
                break;
        }
    }
}

static void alarm_set_tips() {
    uint8_t alarm_hour, alarm_minute;
    my_ui_alarm_get_time(&alarm_hour ,&alarm_minute);
    struct tm time;
    memset(&time, 0, sizeof(time));
    bool valid = false;
    my_rtc_get_time(&time, &valid);
    if(alarm_hour < time.tm_hour ||
        (alarm_hour == time.tm_hour && alarm_minute <= time.tm_min)) {
        my_ui_alarm_set_title("TOMORROW");
    } else {
        my_ui_alarm_set_title("TODAY");
    }
}
