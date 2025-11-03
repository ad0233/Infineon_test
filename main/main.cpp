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

#include "fsm_main.h"

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

    my_ble_init();
    my_ui_generate_qr_code("https://www.volcengine.com/");

    if (fsm_main_init() != 0) {
        ESP_LOGE(TAG, "fsm_main_init failed");
        vTaskDelete(nullptr);
    }

    my_rtc_init();
    my_lcd_init();
    my_radar_init();
    // 设置雷达监测数据回调
    my_radar_set_human_presence_callback(human_presence_callback);
    // my_radar_set_human_movement_callback(human_movement_callback);  // 体动参数已不使用
    my_radar_set_respiratory_callback(respiratory_data_callback);
    my_radar_set_heart_rate_callback(heart_rate_data_callback);
    
    // 启动雷达监测（包含所有开关设置）
    my_radar_start();
    
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

    
    my_ui_in_start();
    for (;;) {
        if(esp_log_timestamp() > 3000) break;
        vTaskDelay(1);
    }

    my_ui_network_guide();

    //clear
    while(xQueueReceive(event_queue, &e, 1) == pdTRUE) {}

    uint32_t last_1s_flush_time = esp_log_timestamp();
    while (1)
    {
        // update clock
        if(esp_log_timestamp() - last_1s_flush_time > 1000) {
            last_1s_flush_time = esp_log_timestamp();
            struct tm time;
            memset(&time, 0, sizeof(time));
            bool valid = false;
            my_rtc_get_time(&time, &valid);
            if(valid) {
                my_ui_clock_set_now_time(time.tm_hour, time.tm_min);
                const struct device_config *cfg = my_nvs_get_config();
                if(cfg->alarm_enable) {
                    // show alarm time exp: 08:00 AM
                    char alarm_time_str[24] = {0};
                    // 转换为12小时制
                    uint8_t display_hour;
                    const char* am_pm;
                    if(cfg->alarm_hour == 0) {
                        display_hour = 12;
                        am_pm = "AM";
                    } else if(cfg->alarm_hour < 12) {
                        display_hour = cfg->alarm_hour;
                        am_pm = "AM";
                    } else if(cfg->alarm_hour == 12) {
                        display_hour = 12;
                        am_pm = "PM";
                    } else {
                        display_hour = cfg->alarm_hour - 12;
                        am_pm = "PM";
                    }
                    snprintf(alarm_time_str, sizeof(alarm_time_str), "%02d:%02d %s", display_hour, cfg->alarm_minute, am_pm);
                    my_ui_clock_set_tips(alarm_time_str);
                } else {
                    my_ui_clock_set_tips("No Alarm");
                }
            }
        }

        if(xQueueReceive(event_queue, &e, 10) == pdFALSE) {
            continue;
        }

        // 获取当前在哪个页面
        page_t current_page = (page_t)my_ui_get_current_page();

        switch (e.type)
        {
            case RE_ET_BTN_CLICKED:
                pwm_control_set(0, 150);  // 短按50%占空比
                // ESP_LOGI(TAG, "Current page: %d", current_page);
                if(current_page == PAGE_FUNCTION) {
                    // 获取当前功能菜单位置
                    menu_item_t current_menu_item = (menu_item_t)my_ui_function_get_index();
                    ESP_LOGI(TAG, "Current menu item: %d", current_menu_item);
                    switch(current_menu_item) {
                        case MENU_ALARM:{
                            my_ui_alarm_set_time(8, 0);
                            alarm_set_tips();
                            my_ui_in_alarm();
                        }
                            break;
                        case MENU_WAKE_MODE:{
                            // 从NVS加载唤醒模式配置
                            const struct device_config *cfg = my_nvs_get_config();
                            if(cfg->wake_mode < WAKE_MODE_MAX) {
                                my_ui_wake_mode_set((wake_mode_t)cfg->wake_mode);
                                // 同时设置为已保存的模式
                                my_ui_wake_mode_save_current();
                            }
                            my_ui_in_wake_mode();
                        }
                            break;
                        case MENU_WIFI:
                            my_ui_network_guide();
                            break;
                        case MENU_UNWIND:
                            my_ui_in_unwind_on();
                            break;
                        case MENU_VOLUME:
                            my_ui_in_volume();
                            break;
                        case MENU_SET_TIME:
                            my_ui_in_time_set();
                            break;
                        case MENU_BODY_DATA:{
                            // 从NVS加载亮度设置
                            const struct device_config *cfg = my_nvs_get_config();
                            if(cfg->light_duty >= 20 && cfg->light_duty <= 100 && (cfg->light_duty - 20) % 10 == 0) {
                                my_ui_light_set(cfg->light_duty);
                            }
                            my_ui_in_light();
                        }
                            break;
                        default:
                            break;
                    }
                } else if(current_page == PAGE_NETWORK_GUIDE ||
                          current_page == PAGE_NETWORK_CONN_FAIL) {
                    my_ui_network_offline();
                } else if(current_page == PAGE_NETWORK_OFFLINE) {
                    bool valid = false;
                    struct tm time;
                    memset(&time, 0, sizeof(time));
                    my_rtc_get_time(&time, &valid);
                    if(valid) {
                        my_ui_clock_set_now_time(time.tm_hour, time.tm_min);
                        my_ui_in_clock();
                    } else {
                        my_ui_in_time_set();
                    }
                } else if(current_page == PAGE_TIME_SET) {
                    uint8_t hour = 0, min = 0;
                    my_ui_time_get(&hour, &min);
                    struct tm time;
                    memset(&time, 0, sizeof(time));
                    time.tm_hour = hour;
                    time.tm_min = min;
                    my_rtc_set_time(&time);
                    my_ui_in_clock();
                } else if(current_page == PAGE_ALARM) {
                    uint8_t hour = 0, min = 0;
                    my_ui_alarm_get_time(&hour, &min);
                    const struct device_config *cfg = my_nvs_get_config();
                    bool last_alarm_enable = cfg->alarm_enable;
                    struct device_config new_cfg = *cfg;
                    new_cfg.alarm_hour = hour;
                    new_cfg.alarm_minute = min;
                    new_cfg.alarm_enable = true;
                    my_nvs_update_config(&new_cfg);
                    my_ui_in_clock();
                    if(last_alarm_enable != new_cfg.alarm_enable) {
                        // audio_tone_play("spiffs://spiffs/on.wav");
                    } else {
                        // audio_tone_play("spiffs://spiffs/updated.wav");
                    }
                } else if(current_page == PAGE_CLOCK) {
                    // 主界面单击进入雷达显示界面
                    my_ui_in_radar_display();
                } else if(current_page == PAGE_RADAR_DISPLAY) {
                    // 雷达显示界面单击进入睡眠数据界面
                    my_ui_in_sleep_data();
                } else if(current_page == PAGE_SLEEP_DATA) {
                    // 睡眠数据界面单击进入早安动画界面
                    my_ui_in_good_morning();
                    // audio_tone_play("spiffs://spiffs/demo-mira.wav");
                } else if(current_page == PAGE_GOOD_MORNING) {
                    // 早安动画界面单击返回主界面
                    my_ui_in_clock();
                } else if(current_page == PAGE_VOLUME) {
                    const struct device_config *cfg = my_nvs_get_config();
                    struct device_config new_cfg = *cfg;
                    new_cfg.volume = my_ui_volume_get();
                    my_nvs_update_config(&new_cfg);
                    my_ui_in_clock();
                    audio_hal_set_volume(board_handle->audio_hal, new_cfg.volume);
                } else if(current_page == PAGE_WAKE_MODE) {
                    // 保存当前浏览的模式为设置的模式
                    my_ui_wake_mode_save_current();
                    // 保存唤醒模式到NVS
                    const struct device_config *cfg = my_nvs_get_config();
                    struct device_config new_cfg = *cfg;
                    new_cfg.wake_mode = (uint8_t)my_ui_wake_mode_get();
                    my_nvs_update_config(&new_cfg);
                    my_ui_in_clock();
                } else if(current_page == PAGE_LIGHT) {
                    // 保存亮度设置到NVS
                    const struct device_config *cfg = my_nvs_get_config();
                    struct device_config new_cfg = *cfg;
                    new_cfg.light_duty = my_ui_light_get();
                    my_nvs_update_config(&new_cfg);
                    my_ui_in_clock();
                } else if(current_page == PAGE_UNWIND_ON) {
                    // UnwindOn页面单击进入确认页面
                    my_ui_in_unwind_select();
                } else if(current_page == PAGE_UNWIND_SELECT) {
                    // UnwindSelet页面单击进入时间设置页面并播放音频
                    my_ui_in_noise_time();
                    audio_tone_play("spiffs://spiffs/water-fountain.mp3");
                } else if(current_page == PAGE_NOISE_TIME) {
                    // NoiseTime页面单击返回主界面
                    my_ui_in_clock();
                } else if(current_page == PAGE_MIAN_YES_PERSON) {
                    // MianYesPerson页面单击返回主页面
                    my_ui_in_clock();
                } else {
                    my_ui_in_clock();
                }
                break;
            case RE_ET_BTN_LONG_PRESSED:
                pwm_control_set(0, 150);  // 长按100%占空比
                if(current_page == PAGE_ALARM) {
                    const struct device_config *cfg = my_nvs_get_config();
                    struct device_config new_cfg = *cfg;
                    new_cfg.alarm_enable = false;
                    my_nvs_update_config(&new_cfg);
                    my_ui_in_no_alarm();
                    // audio_tone_play("spiffs://spiffs/off.wav");
                    xTaskCreate([](void *arg){
                        vTaskDelay(3000 / portTICK_PERIOD_MS);
                        my_ui_in_clock();
                        vTaskDelete(NULL);
                    }, "no_alarm_task", 4096, NULL, 5, NULL);
                } else if(current_page == PAGE_SLEEP_DATA || 
                          current_page == PAGE_DETECTION ||
                          current_page == PAGE_DETECTION_N ||
                          current_page == PAGE_DETECTION_Y ||
                          current_page == PAGE_DETECTION_N1 ||
                          current_page == PAGE_MIAN_YES_PERSON) {
                    // 长按返回主页面
                    my_ui_in_clock();
                }
                break;
            case RE_ET_CHANGED:
                // ESP_LOGI(TAG, "Value = %" PRIi32, e.diff);
                // ESP_LOGI(TAG, "Current page: %d", current_page);
                if(current_page == PAGE_TIME_SET) {
                    uint8_t hour = 8, min = 0;
                    my_ui_time_get(&hour, &min);
                    
                    // 使用封装的时间调整函数
                    adjust_time_by_encoder(e.diff, &hour, &min);
                    
                    my_ui_time_set(hour, min);
                    // my_rtc_set_time(&time);
                    // my_ui_in_clock();
                } else if(current_page == PAGE_FUNCTION) {
                    if(e.diff < 0) {
                        my_ui_function_menu_up();
                    } else {
                        my_ui_function_menu_down();
                    }
                } else if(current_page == PAGE_CLOCK) {
                    my_ui_in_funtion();
                } else if(current_page == PAGE_ALARM) {
                    uint8_t hour = 0, min = 0;
                    my_ui_alarm_get_time(&hour, &min);
                    
                    // 使用封装的时间调整函数
                    adjust_time_by_encoder(e.diff * 5, &hour, &min);

                    my_ui_alarm_set_time(hour, min);
                    alarm_set_tips();
                } else if(current_page == PAGE_VOLUME) {
                    uint8_t volume = my_ui_volume_get();
                    my_ui_volume_set(volume + e.diff);
                    audio_hal_set_volume(board_handle->audio_hal, volume + e.diff);
                } else if(current_page == PAGE_WAKE_MODE) {
                    // 旋转编码器切换唤醒模式
                    if(e.diff > 0) {
                        my_ui_wake_mode_next();
                    } else {
                        my_ui_wake_mode_prev();
                    }
                } else if(current_page == PAGE_LIGHT) {
                    // 旋转编码器切换亮度档位（步进10%）
                    uint8_t light = my_ui_light_get();
                    int new_light = light + (e.diff * 10);
                    
                    // 处理循环逻辑
                    if(new_light > 100) {
                        new_light = 20;  // 超过100%回到20%
                    } else if(new_light < 20) {
                        new_light = 100;  // 低于20%回到100%
                    }
                    
                    my_ui_light_set((uint8_t)new_light);
                } else if(current_page == PAGE_UNWIND_ON) {
                    // UnwindOn页面旋转编码器切换动物
                    if(e.diff > 0) {
                        my_ui_unwind_next_animal();
                    } else {
                        my_ui_unwind_prev_animal();
                    }
                } else if(current_page == PAGE_NOISE_TIME) {
                    // NoiseTime页面旋转编码器调整时间选择
                    lvgl_port_lock(0);
                    uint16_t current_selected = lv_roller_get_selected(ui_NoiseTimeRoller);
                    uint16_t option_cnt = lv_roller_get_option_cnt(ui_NoiseTimeRoller);
                    
                    if(e.diff > 0) {
                        // 向下滚动
                        current_selected = (current_selected + 1) % option_cnt;
                    } else {
                        // 向上滚动
                        current_selected = (current_selected + option_cnt - 1) % option_cnt;
                    }
                    
                    lv_roller_set_selected(ui_NoiseTimeRoller, current_selected, LV_ANIM_ON);
                    lvgl_port_unlock();
                }
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