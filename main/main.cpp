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

#include "rust_lunawake.h"
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

// 人体存在检测数据回调函数
void human_presence_callback(const radar_human_data_t *data, void *context, my_lidar_handle_t self)
{
    (void)context;
    (void)self;
    // 有人没人 - 状态变化时上报
    // ESP_LOGI("HUMAN", "有人: %s", data->presence ? "是" : "否");
}

// 体动参数数据回调函数（已不使用，保留接口）
void human_movement_callback(const radar_human_data_t *data, void *context, my_lidar_handle_t self)
{
    (void)context;
    (void)self;
    // ESP_LOGI("HUMAN", "体动参数: %d", data->movement_param);
    // 体动参数可用于更新图表曲线
    if (ui_RadarInfoChart != NULL && lvgl_port_lock(0)) {
        lv_chart_series_t *series = lv_chart_get_series_next(ui_RadarInfoChart, NULL);
        if (series != NULL) {
            lv_chart_set_next_value(ui_RadarInfoChart, series, data->movement_param);
        }
        lvgl_port_unlock();
    }
}

// 呼吸监测数据回调函数
void respiratory_data_callback(const radar_respiratory_data_t *data, void *context, my_lidar_handle_t self)
{
    (void)context;
    (void)self;
    // ESP_LOGI("RESPIRATORY", "呼吸: %d 次/min", data->respiratory_value);
    if (ui_RadarInfoBreathingX != NULL && lvgl_port_lock(0)) {
        char text[16];
        snprintf(text, sizeof(text), "%d", data->respiratory_value);
        lv_label_set_text(ui_RadarInfoBreathingX, text);
        lvgl_port_unlock();
    }
}

// 心率监测数据回调函数
void heart_rate_data_callback(const radar_heart_rate_data_t *data, void *context, my_lidar_handle_t self)
{
    (void)context;
    (void)self;
    // ESP_LOGI("HEART_RATE", "心率: %d 次/min", data->heart_rate_value);
    if (ui_RadarInfoHeartX != NULL && lvgl_port_lock(0)) {
        char text[16];
        snprintf(text, sizeof(text), "%d", data->heart_rate_value);
        lv_label_set_text(ui_RadarInfoHeartX, text);
        lvgl_port_unlock();
    }
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

extern "C" void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());

    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI("BOOT", "Reset reason: %d", reason);
    ESP_LOGI(TAG, "Initialize board peripherals");
    
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    periph_cfg.extern_stack = true;
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    esp_err_t ret = audio_board_sdcard_init(set, SD_MODE_4_LINE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed! Error: 0x%x", ret);
    } else {
        ESP_LOGI(TAG, "SD card mounted successfully!");
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to list SD card files! Error: 0x%x", ret);
        }
    }  
    
    auto iot_config_view = my_nvs_get_iot_config_view();
    if (iot_config_view == nullptr) {
        ESP_LOGE(TAG, "Failed to get iot config view");
        ESP_LOGE(TAG, "https://axialtech.feishu.cn/wiki/BInkwDDtYiI0kdkMfh5cZKSEn4e?from=from_copylink");
        return;
    }
    
    // 打印 iot_config 分区中的密钥
    my_nvs_print_iot_config_keys();
    
    rust_lib_init();
    my_lcd_init();
    my_h264_init([](const uint8_t *rgb565_buf, uint32_t rgb565_buf_len, void *context) {
        my_ui_canvas_update(rgb565_buf, rgb565_buf_len);
    }, NULL, [](void *context) {
        uint8_t current_state = fsm_main_get_current_state();
        ESP_LOGI(TAG, "my_h264_playback_done, current state: %s (state_id=%d, expected=%d)", 
                 fsm_main_get_current_state_str(), current_state, F_MAIN_S_MENU_UNWIND_PLAYING);
        print_mem_info();
        ESP_LOGI(TAG, "Triggering F_MAIN_E_ANIM_PLAY_SUC event");
        fsm_main_event_trig(F_MAIN_E_ANIM_PLAY_SUC, nullptr);
        ESP_LOGI(TAG, "After trigger, current state: %s (state_id=%d)", 
                 fsm_main_get_current_state_str(), fsm_main_get_current_state());
    }, nullptr);
    my_h264_set_fps(15);

    print_mem_info();

    // 初始化RTC
    if (my_rtc_init(&s_rtc_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init RTC");
    }

    my_rtc_reg_cb_second(s_rtc_handle, 
        [](uint8_t hour, uint8_t minute, void *context, my_rtc_handle_t self) {
            my_ui_clock_set_now_time(hour, minute);
        },
        nullptr
    );
//雷达的初始化  
    my_lidar_init(&s_lidar_handle);
    my_lidar_set_sensitivity(s_lidar_handle, 20, 5 * 1000);
    my_lidar_reg_cb_human_presence(s_lidar_handle, human_presence_callback, NULL);
    my_lidar_reg_cb_human_movement(s_lidar_handle, human_movement_callback, NULL);
    my_lidar_reg_cb_respiratory(s_lidar_handle, respiratory_data_callback, NULL);
    my_lidar_reg_cb_heart_rate(s_lidar_handle, heart_rate_data_callback, NULL);
    my_lidar_reg_cb_move_trig(s_lidar_handle, [](void *context, my_lidar_handle_t self){
        (void)context;
        (void)self;
        fsm_main_event_trig(F_MAIN_E_LIDAR_MOVE_TRIG, nullptr);
    }, nullptr);
    
    // 二维码 ble  wifi
    print_mem_info();
    my_ui_generate_qr_code("https://lunawake.ai", iot_config_view->thing_name);
    my_ble_init(iot_config_view->thing_name);  // 使用默认名称，或传入自定义名称
    print_mem_info();
    ble_protocol_init();  // 初始化蓝牙协议解析（不启用发送任务）
    print_mem_info();
    if (my_wifi_init(&s_wifi_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi");
    }
    
    // 初始化FSM，传入上下文
    fsm_main_context_t fsm_ctx = {
        .rtc_handle = s_rtc_handle,
        .lidar_handle = s_lidar_handle,
        .wifi_handle = s_wifi_handle
    };
    if (fsm_main_init(&fsm_ctx) != 0) {
        ESP_LOGE(TAG, "fsm_main_init failed");
        vTaskDelete(nullptr);
    }
    bs814_init();
    my_wifi_auto_connect(s_wifi_handle);
    // WiFi事件改为轮询方式，在encoder_test中处理
    my_wifi_set_event_callback(s_wifi_handle, [](wifi_state_t state, void *context, my_wifi_handle_t self){
        // WiFi连接成功后，开始NTP同步
        if (state == WIFI_STATE_CONNECTED) {
            if (s_rtc_handle != NULL) {
                my_rtc_start_ntp_sync(s_rtc_handle);
            }
            auto iot_config_view = my_nvs_get_iot_config_view();
            if (iot_config_view == nullptr) {
                ESP_LOGE(TAG, "Failed to get iot config view");
                return;
            }
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
            my_mqtt_init(iot_config_view->mqtt_uri, iot_config_view->thing_name, topics, (int)(sizeof(topics)/sizeof(topics[0])), NULL, NULL);
            
        }
    }, nullptr);

    my_ota_register_progress_callback(
        [](int bytes_read, int total_bytes, void *user_ctx) {
            int ota_progress = (total_bytes > 0) ? (bytes_read * 100 / total_bytes) : 0;
            lvgl_port_lock(0);
            lv_disp_load_scr(ui_OTA);
            char progress_str[10];
            snprintf(progress_str, sizeof(progress_str), "%d%%", ota_progress);
            lv_label_set_text(ui_OTALabel2, progress_str);
            lv_slider_set_range(ui_OTASlider, 0, 100);
            lv_slider_set_value(ui_OTASlider, ota_progress, LV_ANIM_OFF);
            lvgl_port_unlock();
        },
        nullptr
    );
    
    print_mem_info();

    //老硬件 功放的初始化 ，看到了觉得没用删掉就好了
    // gpio_set_direction(PA_ENABLE_GPIO, GPIO_MODE_OUTPUT);
    // gpio_set_level(PA_ENABLE_GPIO, 1); // Disable PA
    vTaskDelay(100 / portTICK_PERIOD_MS);

    board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    audio_hal_set_volume(board_handle->audio_hal, 25);

    vTaskDelay(100 / portTICK_PERIOD_MS);
    // gpio_set_level(PA_ENABLE_GPIO, 0); // Enable PA

    print_mem_info();

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
    
// 初始化音频播放器
    void tone_play_callback(audio_element_status_t evt);
    audio_tone_init(tone_play_callback);
    
    print_mem_info();

    // while(1) {
    //     radar_latest_data_t data;
    //     my_radar_get_latest_data(&data);
    //     mqtt_publish_radar_data(&data, t_radar);
    //     vTaskDelay(1000);
    // }

    fsm_main_event_trig(F_MAIN_E_INIT, nullptr);
    xTaskCreate(encoder_test, "encoder_test", 1024 * 6, nullptr, 10, nullptr);
    my_lidar_start(s_lidar_handle); //TOTD: 雷达好像不需要启动命令，默认启动，确认好就删除这个代码

    print_mem_info();
    return;
}

void tone_play_callback(audio_element_status_t evt) {
    ESP_LOGI(__func__, "%d", evt);
    if(AEL_STATUS_STATE_FINISHED == evt) {
        fsm_main_event_trig(F_MAIN_E_ANIM_PLAY_SUC, NULL);
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
    const TickType_t fsm_flush_interval = pdMS_TO_TICKS(100);    // 100ms
    const TickType_t ble_flush_interval = pdMS_TO_TICKS(10);      // 10ms
    const TickType_t lidar_flush_interval = pdMS_TO_TICKS(1000);      // 1000ms
    const TickType_t ota_flush_interval = pdMS_TO_TICKS(10);      // 10ms
    const TickType_t wifi_flush_interval = pdMS_TO_TICKS(200);    // 200ms
    const TickType_t alarm_check_interval = pdMS_TO_TICKS(1000 * 30); // 30秒检查一次即可
    
    // WiFi状态检测（用于避免重复触发）
    static wifi_state_t last_wifi_state = WIFI_STATE_IDLE;

    while (1)
    {
        TickType_t current_tick = xTaskGetTickCount();
        
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
