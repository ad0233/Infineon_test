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
#include "my_mqtt.h"

#include "rust_lunawake.h"
#include "cmd_parse.h"

#include "fsm_main.h"
#include "my_h264.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_protocol.h"
#include "ble_protocol.h"

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
}

// 体动参数数据回调函数（已不使用，保留接口）
void human_movement_callback(const radar_human_data_t *data)
{
    // ESP_LOGI("HUMAN", "体动参数: %d", data->movement_param);
}

// 呼吸监测数据回调函数
void respiratory_data_callback(const radar_respiratory_data_t *data)
{
    // ESP_LOGI("RESPIRATORY", "呼吸: %d 次/min", data->respiratory_value);
}

// 心率监测数据回调函数
void heart_rate_data_callback(const radar_heart_rate_data_t *data)
{
    // ESP_LOGI("HEART_RATE", "心率: %d 次/min", data->heart_rate_value);
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

    auto iot_config_view = my_nvs_get_iot_config_view();
    if (iot_config_view == nullptr) {
        ESP_LOGE(TAG, "Failed to get iot config view");
        vTaskDelete(nullptr);
    }
    
    rust_lib_init();
    my_lcd_init();
    my_h264_init([](const uint8_t *rgb565_buf, uint32_t rgb565_buf_len, void *context) {
        my_lcd_draw_rgb565(reinterpret_cast<const uint16_t *>(rgb565_buf), rgb565_buf_len / 2);
    }, NULL, [](void *context) {
        ESP_LOGI(TAG, "my_h264_playback_done, current state: %s", fsm_main_get_current_state_str());
        lvgl_port_resume();
        my_lvgl_force_refresh();
        print_mem_info();
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
    print_mem_info();
    my_ui_generate_qr_code("https://lunawake.ai", iot_config_view->thing_name);
    my_ble_init(iot_config_view->thing_name);  // 使用默认名称，或传入自定义名称
    print_mem_info();
    ble_protocol_init();  // 初始化蓝牙协议解析（不启用发送任务）
    print_mem_info();
    my_wifi_init();
    my_wifi_auto_connect();
    my_wifi_set_event_callback([](wifi_state_t state, void *context){
        switch (state)
        {
        case WIFI_STATE_IDLE: {

        }break;
        case WIFI_STATE_CONNECTING: {

        }break;
        case WIFI_STATE_CONNECTED: {
            fsm_main_event_trig(F_MAIN_E_WIFI_C_SUC, nullptr);
        }break;
        case WIFI_STATE_DISCONNECTED: {

        }break;
        case WIFI_STATE_FAILED: {
            fsm_main_event_trig(F_MAIN_E_WIFI_C_FAIL, nullptr);
        }break;
        default:
            break;
        }
    }, nullptr);
    
    print_mem_info();
    my_rtc_init();
    my_radar_init();

    gpio_set_direction(PA_ENABLE_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(PA_ENABLE_GPIO, 1); // Disable PA
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // board_handle = audio_board_init();
    // audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    
    // vTaskDelay(100 / portTICK_PERIOD_MS);
    // gpio_set_level(PA_ENABLE_GPIO, 0); // Enable PA

    print_mem_info();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
    print_mem_info();

    // // wait for time to be set
    // int retry = 0;
    // const int retry_count = 5;
    // while (esp_netif_sntp_sync_wait(3000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
    //     ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    // }
    // // Set timezone to China Standard Time
    // time_t now = 0;
    // struct tm timeinfo;
    // setenv("TZ", "CST-8", 1);
    // tzset();
    // localtime_r(&now, &timeinfo);

    // 按固定模板由 thing_name 生成订阅主题
    static char t_req[128];
    static char t_resp[128];
    static char t_cmd[128];
    static char t_shadow_upd[160];
    static char t_shadow_upd_delta[200];
    static char t_shadow_upd_acc[200];
    static char t_shadow_upd_rej[200];
    static char t_shadow_get[160];
    static char t_shadow_get_acc[200];
    static char t_shadow_get_rej[200];
    static char t_radar[128];

    snprintf(t_req, sizeof(t_req), "lunawake/%s/request", iot_config_view->thing_name);
    snprintf(t_resp, sizeof(t_resp), "lunawake/%s/response", iot_config_view->thing_name);
    snprintf(t_cmd, sizeof(t_cmd), "lunawake/%s/command", iot_config_view->thing_name);

    snprintf(t_shadow_upd, sizeof(t_shadow_upd), "$aws/things/%s/shadow/update", iot_config_view->thing_name);
    snprintf(t_shadow_upd_delta, sizeof(t_shadow_upd_delta), "$aws/things/%s/shadow/update/delta", iot_config_view->thing_name);
    snprintf(t_shadow_upd_acc, sizeof(t_shadow_upd_acc), "$aws/things/%s/shadow/update/accepted", iot_config_view->thing_name);
    snprintf(t_shadow_upd_rej, sizeof(t_shadow_upd_rej), "$aws/things/%s/shadow/update/rejected", iot_config_view->thing_name);
    snprintf(t_shadow_get, sizeof(t_shadow_get), "$aws/things/%s/shadow/get", iot_config_view->thing_name);
    snprintf(t_shadow_get_acc, sizeof(t_shadow_get_acc), "$aws/things/%s/shadow/get/accepted", iot_config_view->thing_name);
    snprintf(t_shadow_get_rej, sizeof(t_shadow_get_rej), "$aws/things/%s/shadow/get/rejected", iot_config_view->thing_name);

    snprintf(t_radar, sizeof(t_radar), "lunawake/%s/body_signal_tick", iot_config_view->thing_name);
    const char* topics[] = {
        t_req,
        t_resp,
        t_cmd,
        t_shadow_upd,
        t_shadow_upd_delta,
        t_shadow_upd_acc,
        t_shadow_upd_rej,
        t_shadow_get,
        t_shadow_get_acc,
        t_shadow_get_rej,
    };
    //my_mqtt_init(iot_config_view->mqtt_uri, iot_config_view->thing_name, topics, (int)(sizeof(topics)/sizeof(topics[0])), NULL, NULL);
    
    print_mem_info();

    // while(1) {
    //     radar_latest_data_t data;
    //     my_radar_get_latest_data(&data);
    //     mqtt_publish_radar_data(&data, t_radar);
    //     vTaskDelay(1000);
    // }

    fsm_main_event_trig(F_MAIN_E_INIT, nullptr);
    xTaskCreate(encoder_test, "encoder_test", 1024 * 6, nullptr, 10, nullptr);
    my_radar_start();

    print_mem_info();

    my_wifi_connect("axiarz", "axiarz8888");

    print_mem_info();
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

    // 定时刷新相关变量
    TickType_t last_radar_flush = 0;
    TickType_t last_fsm_flush = 0;
    TickType_t last_ble_flush = 0;
    TickType_t last_wifi_flush = 0;
    const TickType_t radar_flush_interval = pdMS_TO_TICKS(100);  // 100ms
    const TickType_t fsm_flush_interval = pdMS_TO_TICKS(100);    // 100ms
    const TickType_t ble_flush_interval = pdMS_TO_TICKS(10);      // 10ms
    const TickType_t wifi_flush_interval = pdMS_TO_TICKS(100);      // 100ms

    while (1)
    {
        TickType_t current_tick = xTaskGetTickCount();
        
        // 处理编码器事件
        if(xQueueReceive(event_queue, &e, 0) == pdTRUE) {
            switch (e.type)
            {
                case RE_ET_BTN_CLICKED:
                    fsm_main_event_trig(F_MAIN_E_BTN_CLICKED, nullptr);
                    ESP_LOGI(TAG, "RE_ET_BTN_CLICKED");
                    break;
                case RE_ET_BTN_LONG_PRESSED:
                    fsm_main_event_trig(F_MAIN_E_BTN_L_CLICKED, nullptr);
                    ESP_LOGI(TAG, "F_MAIN_E_BTN_L_CLICKED");
                    break;
                case RE_ET_CHANGED:
                    // 传递旋钮变化量，正数=右旋(增加)，负数=左旋(减少)
                    ESP_LOGI(TAG, "RE_ET_CHANGED, diff: %" PRId32 ", current state: %s", e.diff, fsm_main_get_current_state_str());
                    fsm_main_event_trig(F_MAIN_E_KNOB_CW, (void *)(&e.diff));
                    break; 
                default:
                    break;
            }
        }
        
        // 定时刷新雷达数据（每100ms）
        if ((current_tick - last_radar_flush) >= radar_flush_interval) {
            my_radar_flush();
            last_radar_flush = current_tick;
        }
        // //定时刷新wifi（每100ms）
        // if ((current_tick - last_wifi_flush) >= wifi_flush_interval) {
        //     my_wifi_flush();
        //     last_wifi_flush = current_tick;
        // }
        
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
        vTaskDelay(1);
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
