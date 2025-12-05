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
// #include "periph_sdcard.h"
#include "audio_mem.h"
#include "portmacro.h"
#include "pwm_control.h"
// #include "board.h"

// MKDV4GCL-ABB 驱动相关头文件
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

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

#include "my_ui_canvas.h"
#include "my_ui_lottie.h"
#include "btn.h"
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
void respiratory_data_callback(const radar_respiratory_data_t *data)
{
    // ESP_LOGI("RESPIRATORY", "呼吸: %d 次/min", data->respiratory_value);
    if (ui_RadarInfoBreathingX != NULL && lvgl_port_lock(0)) {
        char text[16];
        snprintf(text, sizeof(text), "%d", data->respiratory_value);
        lv_label_set_text(ui_RadarInfoBreathingX, text);
        lvgl_port_unlock();
    }
}

// 心率监测数据回调函数
void heart_rate_data_callback(const radar_heart_rate_data_t *data)
{
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

static const char *TAG = "main";
// static audio_board_handle_t board_handle; // 已注释，音频功能已禁用

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
    // 基础初始化
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_LOGI(TAG, "Initialize board peripherals");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    // ========== LCD 初始化 ==========
    ESP_LOGI(TAG, "Initializing LCD...");
    my_lcd_init();
    ESP_LOGI(TAG, "LCD initialized successfully");

    // ========== 以下代码已注释，仅保留音频相关 ==========
    /*
    auto iot_config_view = my_nvs_get_iot_config_view();
    if (iot_config_view == nullptr) {
        ESP_LOGE(TAG, "Failed to get iot config view");
        vTaskDelete(nullptr);
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

    if (fsm_main_init() != 0) {
        ESP_LOGE(TAG, "fsm_main_init failed");
        vTaskDelete(nullptr);
    }
    print_mem_info();
    my_ui_generate_qr_code("https://lunawake.ai", iot_config_view->thing_name);
    my_ble_init(iot_config_view->thing_name);
    print_mem_info();
    ble_protocol_init();
    print_mem_info();
    my_wifi_init();
    my_wifi_auto_connect();
    my_wifi_set_event_callback([](wifi_state_t state, void *context){
        if (state == WIFI_STATE_CONNECTED) {
            xTaskCreate([](void *arg) {
                vTaskDelay(pdMS_TO_TICKS(500));
                setenv("TZ", "CST-8", 1);
                tzset();
                esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("cn.pool.ntp.org");
                esp_err_t init_ret = esp_netif_sntp_init(&config);
                if (init_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to init SNTP: %s", esp_err_to_name(init_ret));
                    my_rtc_set_ntp_synced(false);
                    vTaskDelete(nullptr);
                    return;
                }
                ESP_LOGI(TAG, "SNTP initialized with cn.pool.ntp.org");
                int retry = 0;
                const int retry_count = 5;
                esp_err_t ret = ESP_ERR_TIMEOUT;
                ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(8000));
                if (ret == ESP_ERR_TIMEOUT) {
                    retry++;
                    ESP_LOGI(TAG, "Waiting for NTP sync... (%d/%d)", retry, retry_count);
                    while (ret == ESP_ERR_TIMEOUT && retry < retry_count) {
                        ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(3000));
                        if (ret == ESP_ERR_TIMEOUT) {
                            retry++;
                            ESP_LOGI(TAG, "Waiting for NTP sync... (%d/%d)", retry, retry_count);
                        }
                    }
                }
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "NTP sync successful");
                    my_rtc_set_ntp_synced(true);
                    if (my_rtc_sync_from_ntp() == ESP_OK) {
                        ESP_LOGI(TAG, "RTC synced from NTP");
                    } else {
                        ESP_LOGE(TAG, "Failed to sync RTC from NTP");
                    }
                } else {
                    ESP_LOGE(TAG, "NTP sync failed: %s", esp_err_to_name(ret));
                    my_rtc_set_ntp_synced(false);
                }
                vTaskDelete(nullptr);
            }, "ntp_sync_task", 4096, nullptr, 5, nullptr);
        }
    }, nullptr);

    my_ota_register_progress_callback(
        [](int bytes_read, int total_bytes, void *user_ctx) {
            int ota_progress = (total_bytes > 0) ? (bytes_read * 100 / total_bytes) : 0;
            fsm_main_event_trig(F_MAIN_E_OTA_UPDATE, (void *)(size_t)(ota_progress));
        },
        nullptr
    );
    
    print_mem_info();
    my_rtc_init();
    my_radar_init();
    my_radar_set_human_presence_callback(human_presence_callback);
    my_radar_set_human_movement_callback(human_movement_callback);
    my_radar_set_respiratory_callback(respiratory_data_callback);
    my_radar_set_heart_rate_callback(heart_rate_data_callback);
    */

    // ========== 音频初始化（已注释）==========
    /*
    board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    audio_hal_set_volume(board_handle->audio_hal, 25);

    // SPIFFS 初始化（音频文件存储位置）
    periph_spiffs_cfg_t spiffs_cfg = {
        .root = "/spiffs",
        .partition_label = "spiffs_data",
        .max_files = 5,
        .format_if_mount_failed = true};
    esp_periph_handle_t spiffs_handle = periph_spiffs_init(&spiffs_cfg);
    esp_periph_start(set, spiffs_handle);

    // 等待 SPIFFS 挂载完成
    while (!periph_spiffs_is_mounted(spiffs_handle)) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    // 初始化音频播放器
    void tone_play_callback(audio_element_status_t evt);
    audio_tone_init(tone_play_callback);
    
    // 播放音频
    vTaskDelay(500 / portTICK_PERIOD_MS); // 等待音频系统完全初始化
    audio_tone_play("spiffs://spiffs/water-fountain.mp3");
    */

    // ========== MKDV4GCL-ABB 驱动测试 ==========
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "MKDV4GCL-ABB Storage Chip Test");
    ESP_LOGI(TAG, "Pins: CLK=GPIO20, CMD=GPIO21");
    ESP_LOGI(TAG, "      D0=GPIO19, D1=GPIO17");
    ESP_LOGI(TAG, "      D2=GPIO16, D3=GPIO18");
    ESP_LOGI(TAG, "========================================");
    
    // 配置 SDMMC 主机
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    
    // 配置 SDMMC 插槽
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = GPIO_NUM_20;  // CLK
    slot_config.cmd = GPIO_NUM_21;  // CMD
    slot_config.d0 = GPIO_NUM_19;   // DATA0
    slot_config.d1 = GPIO_NUM_17;   // DATA1
    slot_config.d2 = GPIO_NUM_16;   // DATA2
    slot_config.d3 = GPIO_NUM_18;   // DATA3
    
    // 挂载 FAT 文件系统
    const char mount_point[] = "/sdcard";
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false
    };
    
    sdmmc_card_t* card = NULL;
    esp_err_t ret;
    
    // 先尝试4线模式（更快）
    ESP_LOGI(TAG, "Trying 4-line mode...");
    host.flags = SDMMC_HOST_FLAG_4BIT;
    slot_config.width = 4;
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);
    
    // 如果4线模式失败，尝试1线模式
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "4-line mode failed, trying 1-line mode...");
        host.flags = SDMMC_HOST_FLAG_1BIT;
        slot_config.width = 1;
        ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);
    }
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. Format the card?");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s).", esp_err_to_name(ret));
            ESP_LOGE(TAG, "Make sure SD card lines have pull-up resistors in place.");
        }
        return;
    }
    
    // 打印卡信息
    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "SD card mounted successfully!");
    
    // ========== 读写测试 ==========
    const char* test_file = "/sdcard/test.txt";
    const char* test_data = "MKDV4GCL-ABB Test - Hello World! 1234567890 ABCDEF";
    const size_t test_data_len = strlen(test_data);
    
    // 写入测试
    ESP_LOGI(TAG, "Writing test data to %s...", test_file);
    FILE* f = fopen(test_file, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing!");
        esp_vfs_fat_sdcard_unmount(mount_point, card);
        return;
    }
    size_t written = fwrite(test_data, 1, test_data_len, f);
    fclose(f);
    
    if (written != test_data_len) {
        ESP_LOGE(TAG, "Write failed! Expected %zu bytes, wrote %zu bytes", test_data_len, written);
        esp_vfs_fat_sdcard_unmount(mount_point, card);
        return;
    }
    ESP_LOGI(TAG, "Successfully wrote %zu bytes", written);
    
    // 读取测试
    ESP_LOGI(TAG, "Reading test data from %s...", test_file);
    char read_buffer[256] = {0};
    f = fopen(test_file, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading!");
        esp_vfs_fat_sdcard_unmount(mount_point, card);
        return;
    }
    size_t read_len = fread(read_buffer, 1, sizeof(read_buffer) - 1, f);
    fclose(f);
    
    ESP_LOGI(TAG, "Read %zu bytes: %s", read_len, read_buffer);
    
    // 验证数据
    if (read_len == test_data_len && memcmp(test_data, read_buffer, test_data_len) == 0) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "MKDV4GCL-ABB Test PASSED!");
        ESP_LOGI(TAG, "Written: %s", test_data);
        ESP_LOGI(TAG, "Read:    %s", read_buffer);
        ESP_LOGI(TAG, "Data matches perfectly!");
        ESP_LOGI(TAG, "========================================");
    } else {
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "MKDV4GCL-ABB Test FAILED!");
        ESP_LOGE(TAG, "Expected length: %zu, Read length: %zu", test_data_len, read_len);
        ESP_LOGE(TAG, "Written: %s", test_data);
        ESP_LOGE(TAG, "Read:    %s", read_buffer);
        ESP_LOGE(TAG, "========================================");
    }
    
    // 保持挂载，不卸载（测试用）
    // esp_vfs_fat_sdcard_unmount(mount_point, card);

    // ========== 以下代码已注释 ==========
    /*
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
    
    print_mem_info();

    fsm_main_event_trig(F_MAIN_E_INIT, nullptr);
    xTaskCreate(encoder_test, "encoder_test", 1024 * 6, nullptr, 10, nullptr);
    my_radar_start();

    print_mem_info();
    */
    
    ESP_LOGI(TAG, "Audio test started");
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

// 雷达检测使能标志位已移至 fsm_main.cpp，通过 fsm_main_get_radar_detect_enabled() 访问

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
    TickType_t last_lidar_flush = 0;
    TickType_t last_ota_flush = 0;
    TickType_t last_wifi_flush = 0;
    TickType_t last_time_update = 0;
    const TickType_t radar_flush_interval = pdMS_TO_TICKS(100);  // 100ms
    const TickType_t fsm_flush_interval = pdMS_TO_TICKS(100);    // 100ms
    const TickType_t ble_flush_interval = pdMS_TO_TICKS(10);      // 10ms
    const TickType_t lidar_flush_interval = pdMS_TO_TICKS(1000);      // 1000ms
    const TickType_t ota_flush_interval = pdMS_TO_TICKS(10);      // 10ms
    const TickType_t wifi_flush_interval = pdMS_TO_TICKS(200);    // 200ms
    const TickType_t time_update_interval = pdMS_TO_TICKS(1000); // 1s
    
    // 雷达检测状态（用于避免重复触发）
    static bool last_radar_found = false;
    
    // WiFi状态检测（用于避免重复触发）
    static wifi_state_t last_wifi_state = WIFI_STATE_IDLE;

    while (1)
    {
        TickType_t current_tick = xTaskGetTickCount();
        
        // static TickType_t last_bs814_poll = 0;
        // if (current_tick - last_bs814_poll >= pdMS_TO_TICKS(20)) {
        //     last_bs814_poll = current_tick;

        //     // KEY2 处理
        //     key_evt_t ev = bs814_key2_update();  // 刷新按键状态

        //     if (ev == KEY_EVT_CLICKED) {
        //         ESP_LOGI(TAG, "BS814 KEY2 CLICKED");
        //         fsm_main_event_trig(F_MAIN_E_BTN_CLICKED, NULL);
        //     }
        //     else if (ev == KEY_EVT_LONG) {
        //         ESP_LOGI(TAG, "BS814 KEY2 LONG");
        //         fsm_main_event_trig(F_MAIN_E_BTN_L_CLICKED, NULL);
        //     }

        //     // KEY1 处理（音量减，已在 btn.c 中处理）
        //     bs814_key1_update();

        //     // KEY3 处理（音量加，已在 btn.c 中处理）
        //     bs814_key3_update();
        // }

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

        // 定时刷新时钟
        if ((current_tick - last_time_update) >= time_update_interval) {
            struct tm timeinfo;
            bool valid = false;
            my_rtc_get_time(&timeinfo, &valid);
            if (valid) {
                my_ui_clock_set_now_time(timeinfo.tm_hour, timeinfo.tm_min);
            }
            last_time_update = current_tick;
        }
        
        // 定时刷新雷达数据（每100ms）
        if ((current_tick - last_radar_flush) >= radar_flush_interval) {
            my_radar_flush();
            
            // 只在 MENU_UNWIND 状态且雷达检测使能时检查雷达并触发事件
            uint8_t current_state = fsm_main_get_current_state();
            if(current_state == F_MAIN_S_MENU_UNWIND && fsm_main_get_radar_detect_enabled()) {
                // 检查雷达是否找到人，如果找到则触发事件
                radar_latest_data_t radar_data;
                if(my_radar_get_latest_data(&radar_data)) {
                    bool current_radar_found = false;
                    // 挥挥手就识别成功了
                    if(radar_data.movement_param > 20) {
                        current_radar_found = true;
                    }                    
                    // 如果从没找到变为找到，再次确认状态后触发事件
                    if(current_radar_found && !last_radar_found) {
                        // 再次确认当前状态，避免状态在检测和触发之间发生变化
                        uint8_t verify_state = fsm_main_get_current_state();
                        if(verify_state == F_MAIN_S_MENU_UNWIND && fsm_main_get_radar_detect_enabled()) {
                            fsm_main_event_trig(F_MAIN_E_LIDAR_FIND, nullptr);
                            ESP_LOGI(TAG, "Radar found person, triggering F_MAIN_E_LIDAR_FIND event");
                        } else {
                            ESP_LOGW(TAG, "State changed during radar detection, state=%d, enabled=%d", verify_state, fsm_main_get_radar_detect_enabled());
                        }
                    }
                    last_radar_found = current_radar_found;
                }
            } else {
                // 不在 MENU_UNWIND 状态或雷达检测被禁用时，重置雷达检测状态
                last_radar_found = false;
            }
            
            // 在找人动画状态时，定时检测是否找到人
            if(current_state == F_MAIN_S_FINDPERSONC_ANIM) {
                radar_latest_data_t radar_data;
                if(my_radar_get_latest_data(&radar_data)) {
                    // 挥挥手就识别成功了
                    if(radar_data.movement_param > 15) {
                        fsm_main_set_person_found_during_anim(true);
                        ESP_LOGI(TAG, "Person found during find animation (movement_param: %d)", radar_data.movement_param);
                    }
                    // 心率检测：如果最近5秒内有心率更新，说明有人
                    else if(esp_log_timestamp() - radar_data.heart_rate_system_timestamp < 5) {
                        fsm_main_set_person_found_during_anim(true);
                        ESP_LOGI(TAG, "Person found during find animation (heart rate detected)");
                    }
                }
            }
            
            last_radar_flush = current_tick;
        }
        
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
            my_radar_get_latest_data(&data);
            if(esp_log_timestamp() - data.heart_rate_system_timestamp < 10 * 1000) {
                protocol_publish_radar_data(PROTOCOL_TYPE_MQTT, &data, t_radar);
                protocol_publish_radar_data(PROTOCOL_TYPE_BLE, &data, t_radar);
            }
            last_lidar_flush = current_tick;
        }

        // 定时刷新OTA（每10ms）
        if ((current_tick - last_ota_flush) >= ota_flush_interval) {
            my_ota_flush_v1();
            last_ota_flush = current_tick;
        }
        //FIXME: 好像wifi没有定时。
        // 定时检查WiFi状态（每200ms）
        if ((current_tick - last_wifi_flush) >= wifi_flush_interval) {
            wifi_state_t current_wifi_state = my_wifi_get_state();
            
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
