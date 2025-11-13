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
    }, NULL);

    my_ui_network_guide();
    lvgl_port_stop();
    my_h264_start(MY_H264_ANIM_BRAND_MOTION2, 100);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    my_h264_start(MY_H264_ANIM_FAIL2, 100);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    my_h264_start(MY_H264_ANIM_GO_UP, 100);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    my_h264_start(MY_H264_ANIM_HUMAN_RECOGNIZED, 100);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    my_h264_start(MY_H264_ANIM_PROCESSING, 100);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    my_h264_start(MY_H264_ANIM_SUCCESS2, 100);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    lvgl_port_resume();
    my_lvgl_force_refresh();
    print_mem_info();
    return;

    if (fsm_main_init() != 0) {
        ESP_LOGE(TAG, "fsm_main_init failed");
        vTaskDelete(nullptr);
    }
    my_ble_init();
    my_wifi_init();
    // my_wifi_connect("303", "Qq13543826488.");
    
    // my_ota_start("https://lunawake.oss-cn-shenzhen.aliyuncs.com/Lunawake_main_1706a52_a8f0dcb6_20251108_164040.bin?x-oss-credential=LTAI5tKXsxzVgvKJaTE4Dgaa%2F20251108%2Fcn-shenzhen%2Foss%2Faliyun_v4_request&x-oss-date=20251108T084717Z&x-oss-expires=32400&x-oss-signature-version=OSS4-HMAC-SHA256&x-oss-signature=977c006b564eb20045ab5e3ae0c9337a2c50834c0de7cb6d9c24a80651c05ef6");
    return;
    
    // 自动连接已保存的WiFi
    xTaskCreate([](void *arg) {
        esp_err_t ret = my_wifi_auto_connect();
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGI(TAG, "未配置WiFi，等待通过蓝牙配置");
        } else if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi自动连接失败");
        }
        vTaskDelete(nullptr);
    }, "wifi_conn", 1024 * 8, NULL, 5, NULL);

    my_wifi_set_event_callback([](wifi_state_t event, void *arg){
        switch(event) {
            case WIFI_STATE_IDLE:
                ESP_LOGI("WIFI_EVT", "WIFI_STATE_IDLE");
                break;
            case WIFI_STATE_CONNECTING:
                ESP_LOGI("WIFI_EVT", "WIFI_STATE_CONNECTING");
                break;
            case WIFI_STATE_CONNECTED:
                fsm_main_event_trig(F_MAIN_E_WIFI_C_SUC, NULL);
                ESP_LOGI("WIFI_EVT", "WIFI_STATE_CONNECTED");
                break;
            case WIFI_STATE_DISCONNECTED:
                ESP_LOGI("WIFI_EVT", "WIFI_STATE_DISCONNECTED");
                break;
            case WIFI_STATE_FAILED:
                fsm_main_event_trig(F_MAIN_E_WIFI_C_FAIL, NULL);
                ESP_LOGI("WIFI_EVT", "WIFI_STATE_FAILED");
                break;
            default:
                break;
        }
    }, NULL);

    StreamBufferHandle_t ble_recv_stream = my_stream_buffer_create(1024 * 10, 1);
    if (ble_recv_stream == NULL) {
        ESP_LOGE("BLE_RX", "Failed to create BLE receive stream buffer");
        return;
    }
    my_ble_register_recv_callback([](const uint8_t *data, uint16_t len, void *context) {
        StreamBufferHandle_t stream = (StreamBufferHandle_t)context;
        // ESP_LOG_BUFFER_HEXDUMP("BLE_RX", data, len, ESP_LOG_INFO);
        size_t sent = xStreamBufferSend(stream, data, len, 0);
        if (sent != len) {
            ESP_LOGE("BLE_RX", "Stream buffer full, lost %d bytes", len - sent);
        }
    }, ble_recv_stream);
    my_thread_create([](void *arg) {
        StreamBufferHandle_t ble_recv_stream = (StreamBufferHandle_t)arg;
        // decode task
        single_parse_handle_t single_parser = rust_single_parse_new(100 * 1024);
        if (single_parser == nullptr) {
            ESP_LOGE("RUST", "Failed to init single parser");
            vTaskDelete(nullptr);
        }
        auto one_packet_len = 10 * 1024;
        auto one_packet_buf = (uint8_t *)heap_caps_malloc(one_packet_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        while(1) {
            auto len = xStreamBufferReceive(
                ble_recv_stream,
                one_packet_buf, one_packet_len, portMAX_DELAY);
            if (len > 0) {
                size_t out_len;
                for(auto i = 0; i < len; i++ ) {
                    const uint8_t *out_buf = rust_single_parse_unpack(
                        single_parser,
                        one_packet_buf[i],
                        &out_len);
                        if (out_len > 0) {
                            ESP_LOGI(TAG, "Parsed packet, len %d", out_len);
                            ESP_LOG_BUFFER_HEXDUMP(TAG, out_buf, out_len, ESP_LOG_INFO);
                            
                            // 解析命令
                            cmd_parse(out_buf, out_len);
                        }
                }
            }
        }
    }, "rust_task", 1024 * 16, ble_recv_stream, 5, NULL);
    xTaskCreate([](void *arg) {
        static uint8_t send_buf[128];
        while (1)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            int len = rust_single_parse_pack(
                (const uint8_t*)"Hello from ESP32 BLE!", 
                strlen("Hello from ESP32 BLE!"), 
                send_buf, 
                sizeof(send_buf));
            if (len > 0) {
                my_ble_send_data(send_buf, len, 1000);
            } else {
                ESP_LOGE("BLE_TX", "Pack failed");
            }
        }
    }, "rust_task", 8192, NULL, 5, NULL);
    my_rtc_init();
    my_radar_init();
    // 设置雷达监测数据回调
    my_radar_set_human_presence_callback(human_presence_callback);
    // my_radar_set_human_movement_callback(human_movement_callback);  // 体动参数已不使用
    my_radar_set_respiratory_callback(respiratory_data_callback);
    my_radar_set_heart_rate_callback(heart_rate_data_callback);
    
    // 启动雷达监测（包含所有开关设置）
    my_radar_start();
    
    // 获取蓝牙MAC地址（无冒号）
    std::string mac_str;
    const char *mac_no_colon = my_ble_get_mac(false);
    if (mac_no_colon) {
        mac_str = mac_no_colon;
    }
    
    my_ui_generate_qr_code("https://lunawake.ai", "lunawake", "test", mac_str.c_str());

    xTaskCreate([](void *arg) {
        // while(esp_log_timestamp() < 3000) {
        //     vTaskDelay(10 / portTICK_PERIOD_MS);
        // }
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        fsm_main_event_trig(F_MAIN_E_INIT, nullptr);
        vTaskDelete(nullptr);
    }, "init_task", 1024 * 4, NULL, 5, NULL);
    
#ifndef USE_AIRTOUCH_RADAR
    // R60ABD1 雷达特有的查询功能
    // 查询人体存在开关状态
    vTaskDelay(1000 / portTICK_PERIOD_MS);  // 等待1秒让雷达稳定
    my_radar_query_human_switch_status();
    
    // 查询当前人体存在状态
    vTaskDelay(500 / portTICK_PERIOD_MS);
    my_radar_query_human_presence();
    
    // 创建定时器，每5秒查询一次人体存在状态
    xTaskCreate([](void *arg) {
        while (1) {
            vTaskDelay(5000 / portTICK_PERIOD_MS);  // 每5秒查询一次
            my_radar_query_human_presence();
        }
    }, "human_presence_query", 4096, NULL, 5, NULL);
#else
    // 艾睿雷达自动上报数据，无需主动查询
    ESP_LOGI("MAIN", "艾睿雷达已启动，数据将自动上报");
#endif
    
    // 初始化PWM控制模块
    if (pwm_control_init()) {
        ESP_LOGI("MAIN", "PWM控制模块初始化成功");
    } else {
        ESP_LOGE("MAIN", "PWM控制模块初始化失败");
    }

    periph_spiffs_cfg_t spiffs_cfg = {
        .root = "/spiffs",
        .partition_label = "spiffs_data",
        .max_files = 5,
        .format_if_mount_failed = true};
    esp_periph_handle_t spiffs_handle = periph_spiffs_init(&spiffs_cfg);
    esp_periph_start(set, spiffs_handle);

    // Wait until spiffs is mounted
    while (!periph_spiffs_is_mounted(spiffs_handle)) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    // 检测spiffs/on.wav有没有烧录进去
    FILE* f = fopen("/spiffs/on.wav", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open /spiffs/on.wav");
    } else {
        ESP_LOGI(TAG, "/spiffs/on.wav exists");
        fclose(f);
    }

    gpio_set_direction(PA_ENABLE_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(PA_ENABLE_GPIO, 1); // Disable PA
    vTaskDelay(100 / portTICK_PERIOD_MS);

    board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_set_level(PA_ENABLE_GPIO, 0); // Enable PA

    // ui init
    {
        const struct device_config *cfg = my_nvs_get_config();
        my_ui_volume_set(cfg->volume);
        audio_hal_set_volume(board_handle->audio_hal, cfg->volume);
        
        // 初始化唤醒模式
        if(cfg->wake_mode < WAKE_MODE_MAX) {
            my_ui_wake_mode_set((wake_mode_t)cfg->wake_mode);
            // 同时设置为已保存的模式
            my_ui_wake_mode_save_current();
        } else {
            my_ui_wake_mode_set(WAKE_MODE_SMART);
            my_ui_wake_mode_save_current();
        }
        
        // 初始化亮度设置
        if(cfg->light_duty >= 20 && cfg->light_duty <= 100 && (cfg->light_duty - 20) % 10 == 0) {
            my_ui_light_set(cfg->light_duty);
        } else {
            my_ui_light_set(20);  // 默认20%
        }
    }

    void tone_play_callback(audio_element_status_t evt);
    audio_tone_init(tone_play_callback);

    print_mem_info();
    xTaskCreate(encoder_test, "encoder_test", 1024 * 4, NULL, 5, NULL);
    // xTaskCreate(rust_task, "rust_task", 8 * 1024, NULL, 5, NULL);

    print_mem_info();
    return;

    periph_wifi_cfg_t wifi_cfg;
    memset(&wifi_cfg, 0, sizeof(periph_wifi_cfg_t));
    memcpy(wifi_cfg.wifi_config.sta.ssid, CONFIG_WIFI_SSID, sizeof(CONFIG_WIFI_SSID));
    memcpy(wifi_cfg.wifi_config.sta.password, CONFIG_WIFI_PASSWORD, sizeof(CONFIG_WIFI_PASSWORD));
    
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
    print_mem_info();

    // wait for time to be set
    int retry = 0;
    const int retry_count = 5;
    while (esp_netif_sntp_sync_wait(1000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }
    // Set timezone to China Standard Time
    time_t now = 0;
    struct tm timeinfo;
    setenv("TZ", "CST-8", 1);
    tzset();
    localtime_r(&now, &timeinfo);

    print_mem_info();

#if defined(ENABLE_TASK_MONITOR)
    audio_thread_create(NULL, "monitor_task", monitor_task, NULL, 5 * 1024, 13, true, 0);
#endif



    // init byte rtc engine
    // void rust_main();
    // rust_main();
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
            case RE_ET_CHANGED:
                break;
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
