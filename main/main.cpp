/* volc rtc example code

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
 
#include <cstdint>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <algorithm>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include "board.h"
#include "esp_log_timestamp.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "audio_sys.h"
#include "audio_thread.h"
#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "periph_spiffs.h"
#include "periph_sdcard.h"
#include "audio_mem.h"
#include "portmacro.h"
#include "pwm_control.h"
#include "board.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "board_pins_config.h"

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
#include "my_mqtt.h"
#include "my_mqtt.h"
#include "broadcast.h"
#include "my_board.h"

#include "single_parse.h"
#include "cmd_parse.h"

#include "fsm_main.h"
#include "my_h264.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_protocol.h"
#include "ble_protocol.h"

#include "my_ui_canvas.h"
#include "my_ui_lottie.h"
#include "btn.h"

#include "my_lidar_inf.h"

static const char *TAG = "main";

// 时间调整函数 - 根据编码器变化调整时间
__attribute__((unused)) static void adjust_time_by_encoder(int32_t diff, uint8_t *hour, uint8_t *min) {
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

static void check_alarms_and_trigger(void) {
    my_nvs_check_alarm_triggers();
}


#include "audio_recorder.h"
#include "audio_processor.h"
#include <my_utils.h>

// #define ENABLE_TASK_MONITOR

static my_rtc_handle_t s_rtc_handle = NULL;  // RTC句柄，仅在main.cpp中使用
static my_lidar_handle_t s_lidar_handle = NULL;  // 雷达句柄，仅在main.cpp中使用
static my_wifi_handle_t s_wifi_handle = NULL;  // WiFi句柄
static audio_board_handle_t board_handle = NULL;  // 音频板句柄

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

// 按固定模板由 thing_name 生成订阅主题（从外部内存分配）
static char *t_req = nullptr;
static char *t_resp = nullptr;
static char *t_cmd = nullptr;
static char *t_shadow_upd = nullptr;
static char *t_shadow_upd_delta = nullptr;
static char *t_shadow_upd_acc = nullptr;
static char *t_shadow_upd_rej = nullptr;
static char *t_shadow_get = nullptr;
static char *t_shadow_get_acc = nullptr;
static char *t_shadow_get_rej = nullptr;
static char *t_radar = nullptr;

extern "C" void app_main()
{
    bsp_audio_init();
    // my_lidar_inf_init();
    // print_mem_info();
    return;
}

void tone_play_callback(audio_element_status_t evt) {
    ESP_LOGI(__func__, "%d", evt);
    if(AEL_STATUS_STATE_FINISHED == evt) {
        uint8_t current_state = fsm_main_get_current_state();
        // 允许在以下状态下触发播放完成事件：
        // - F_MAIN_S_GoodMorning_DEMO: 早报播放完成
        // - F_MAIN_S_SLEEPMODE: 入睡提示播放完成
        // - F_MAIN_S_SLEEPMODE_READY: 呼吸音频播放完成
        if (current_state == F_MAIN_S_GoodMorning_DEMO || 
            current_state == F_MAIN_S_SLEEPMODE ||
            current_state == F_MAIN_S_SLEEPMODE_READY) {
            ESP_LOGI(__func__, "Audio finished in state %d, triggering event", current_state);
            fsm_main_event_trig(F_MAIN_E_WAV_PLAY_FINISHED, NULL);
        } else {
            ESP_LOGI(__func__, "Audio finished but current state is %d, ignoring event", current_state);
        }
    }
}

static QueueHandle_t event_queue;
static rotary_encoder_t re;

#define EV_QUEUE_LEN 5

static void alarm_set_tips();

// 雷达检测使能标志位已移至 fsm_main.cpp，通过 fsm_main_get_radar_detect_enabled() 访问

void encoder_test(void *arg)
{
    // Create event queue for rotary encoders
    event_queue = xQueueCreate(EV_QUEUE_LEN, sizeof(rotary_encoder_event_t));

    // Setup rotary encoder library
    ESP_ERROR_CHECK(rotary_encoder_init(event_queue));

    // Add one encoder
    board_encoder_pin_t encoder_pins;
    ESP_ERROR_CHECK(get_encoder_pins(&encoder_pins));
    memset(&re, 0, sizeof(rotary_encoder_t));
    re.pin_a = (gpio_num_t)encoder_pins.pin_a;
    re.pin_b = (gpio_num_t)encoder_pins.pin_b;
    // re.pin_btn = (gpio_num_t)encoder_pins.pin_btn;
    ESP_ERROR_CHECK(rotary_encoder_add(&re));

    rotary_encoder_event_t e;
    int32_t val = 0;

    ESP_LOGI(TAG, "Initial value: %" PRIi32, val);
    //clear
    while(xQueueReceive(event_queue, &e, 1) == pdTRUE) {}

    // 定时刷新相关变量
    TickType_t last_fsm_flush = 0;
    TickType_t last_ble_flush = 0;
    TickType_t last_lidar_flush = 0;
    TickType_t last_ota_flush = 0;
    TickType_t last_wifi_flush = 0;
    TickType_t last_alarm_check_flush = 0;
    TickType_t last_thread_heart_log_flush = 0;
    const TickType_t fsm_flush_interval = pdMS_TO_TICKS(100);    // 100ms
    const TickType_t ble_flush_interval = pdMS_TO_TICKS(10);      // 10ms
    const TickType_t lidar_flush_interval = pdMS_TO_TICKS(1000);      // 1000ms
    const TickType_t ota_flush_interval = pdMS_TO_TICKS(10);      // 10ms
    const TickType_t wifi_flush_interval = pdMS_TO_TICKS(200);    // 200ms
    const TickType_t alarm_check_interval = pdMS_TO_TICKS(1000 * 30); // 30秒检查一次即可
    const TickType_t thread_heart_log_interval = pdMS_TO_TICKS(2000); // 2s
    
    // WiFi状态检测（用于避免重复触发）
    static wifi_state_t last_wifi_state = WIFI_STATE_IDLE;

    while (1)
    {
        TickType_t current_tick = xTaskGetTickCount();

        if ((current_tick - last_thread_heart_log_flush) >= thread_heart_log_interval) {
            print_mem_info();
            last_thread_heart_log_flush = current_tick;
        }
        
        static TickType_t last_bs814_poll = 0;
        if (current_tick - last_bs814_poll >= pdMS_TO_TICKS(20)) {
            last_bs814_poll = current_tick;

            // KEY2 处理
            key_evt_t ev = bs814_key2_update();  // 刷新按键状态

            if (ev == KEY_EVT_CLICKED) {
                ESP_LOGI(TAG, "BS814 KEY2 CLICKED");
                fsm_main_event_trig(F_MAIN_E_BTN_CLICKED, NULL);
            }
            else if (ev == KEY_EVT_LONG) {
                ESP_LOGI(TAG, "BS814 KEY2 LONG");
                fsm_main_event_trig(F_MAIN_E_BTN_L_CLICKED, NULL);
            }

            // KEY1 处理（音量减，已在 btn.c 中处理）
            bs814_key1_update();

            // KEY3 处理（音量加，已在 btn.c 中处理）
            bs814_key3_update();
        }
        
        // 处理编码器事件
        if(xQueueReceive(event_queue, &e, 0) == pdTRUE) {
            switch (e.type)
            {
                case RE_ET_BTN_CLICKED:
                    ESP_LOGI(TAG, "F_MAIN_E_BTN_CLICKED");
                    fsm_main_event_trig(F_MAIN_E_BTN_CLICKED, nullptr);
                    break;
                case RE_ET_BTN_LONG_PRESSED:
                    ESP_LOGI(TAG, "F_MAIN_E_BTN_L_CLICKED");
                    fsm_main_event_trig(F_MAIN_E_BTN_L_CLICKED, nullptr);
                    break;
                case RE_ET_CHANGED:
                    // 传递旋钮变化量，正数=右旋(增加)，负数=左旋(减少)
                    ESP_LOGI(TAG, "F_MAIN_E_KNOB_CW, diff: %" PRId32 ", current state: %s", e.diff, fsm_main_get_current_state_str());
                    fsm_main_event_trig(F_MAIN_E_KNOB_CW, (void *)(&e.diff));
                    break; 
                default:
                    break;
            }
        }
        my_lidar_flush(s_lidar_handle, 100);

        // 定时刷新FSM超时（每100ms）
        if ((current_tick - last_fsm_flush) >= fsm_flush_interval) {
            fsm_main_timeout_flush();
            last_fsm_flush = current_tick;
        }

        // 定时刷新BLE发送（每10ms）
        if ((current_tick - last_ble_flush) >= ble_flush_interval) {
            ble_send_flush();
            ble_parse_flush();  // 同时处理BLE解析
            last_ble_flush = current_tick;
        }
        
        // 定时发送mqtt LIDAR数据（每1000ms）
        if ((current_tick - last_lidar_flush) >= lidar_flush_interval) {
            radar_latest_data_t data;
            my_lidar_handle_t lidar_handle = fsm_main_get_lidar_handle();
            if (lidar_handle != NULL && my_lidar_get_latest_data(lidar_handle, &data) == ESP_OK) {
                if(esp_log_timestamp() - data.heart_rate_system_timestamp < 10 * 1000) {
                    protocol_publish_radar_data(PROTOCOL_TYPE_MQTT, &data, t_radar);
                    protocol_publish_radar_data(PROTOCOL_TYPE_BLE, &data, t_radar);
                }
            }
            last_lidar_flush = current_tick;
        }
        
        // 定时刷新OTA（每10ms）
        if ((current_tick - last_ota_flush) >= ota_flush_interval) {
            my_ota_flush_v1();
            last_ota_flush = current_tick;
        }
        
        // 定时刷新RTC（每100ms）
        my_rtc_flush(s_rtc_handle, 100);

        //FIXME: 好像wifi没有定时。
        // 定时检查WiFi状态（每200ms）
        if ((current_tick - last_wifi_flush) >= wifi_flush_interval) {
            wifi_state_t current_wifi_state = my_wifi_get_state(s_wifi_handle);
            
            // 检测状态变化并触发相应事件
            if (current_wifi_state != last_wifi_state) {
                // 从非CONNECTED状态变为CONNECTED，触发成功事件
                if (current_wifi_state == WIFI_STATE_CONNECTED && 
                    last_wifi_state != WIFI_STATE_CONNECTED) {
                    fsm_main_event_trig(F_MAIN_E_WIFI_C_SUC, nullptr);
                    ESP_LOGI(TAG, "WiFi connected, triggering F_MAIN_E_WIFI_C_SUC event");
                }
                // 从CONNECTING/IDLE状态变为FAILED，触发失败事件
                else if (current_wifi_state == WIFI_STATE_FAILED && 
                         (last_wifi_state == WIFI_STATE_CONNECTING || 
                          last_wifi_state == WIFI_STATE_IDLE)) {
                    fsm_main_event_trig(F_MAIN_E_WIFI_C_FAIL, nullptr);
                    ESP_LOGI(TAG, "WiFi connection failed, triggering F_MAIN_E_WIFI_C_FAIL event");
                }
                
                last_wifi_state = current_wifi_state;
            }
            
            last_wifi_flush = current_tick;
        }

        MorningAnimationLyricsFlush(100);

        // 定时检查闹钟（每30s检查一次，内部有分钟变化检测）
        if ((current_tick - last_alarm_check_flush) >= alarm_check_interval) {
            check_alarms_and_trigger();
            last_alarm_check_flush = current_tick;
        }
        vTaskDelay(1);
    }
}

static void alarm_set_tips() {
    uint8_t alarm_hour, alarm_minute;
    my_ui_alarm_get_time(&alarm_hour ,&alarm_minute);
    struct tm time;
    memset(&time, 0, sizeof(time));
    bool valid = false;
    if (s_rtc_handle != NULL) {
        my_rtc_get_time(s_rtc_handle, &time, &valid);
    }
    if(alarm_hour < time.tm_hour ||
        (alarm_hour == time.tm_hour && alarm_minute <= time.tm_min)) {
        my_ui_alarm_set_title("TOMORROW");
    } else {
        my_ui_alarm_set_title("TODAY");
    }
}
