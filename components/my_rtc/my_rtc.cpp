#include "my_rtc.h"

// 标准库
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

// 环境库
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "i2c_bus.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// 内部库
#include "pcf8563.h"
#include "board_pins_config.h"
#include "audio_error.h"

static const char *TAG = "my_rtc";

struct my_rtc_impl {
    i2c_bus_handle_t i2c;
    bool valid;
    bool ntp_synced;
    // 回调函数
    my_rtc_ntp_sync_callback_t ntp_sync_cb;
    void *ntp_sync_context;
    my_rtc_second_callback_t second_cb;
    void *second_context;
    // flush 相关
    uint8_t last_second;  // 上次触发回调的秒数
    uint32_t last_flush_time;  // 上次flush执行时间（毫秒）
    // NTP同步相关
    bool ntp_syncing;      // 是否正在同步
    int ntp_retry_count;   // 重试次数
    uint32_t ntp_last_check_time;  // 上次检查时间（毫秒）
};

static int i2c_init(my_rtc_handle_t self)
{
    i2c_config_t es_i2c_cfg;
    memset(&es_i2c_cfg, 0, sizeof(i2c_config_t));
    
    es_i2c_cfg.mode = I2C_MODE_MASTER;
    es_i2c_cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    es_i2c_cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    es_i2c_cfg.master.clk_speed = 100000;
    
    int ret = get_i2c_pins(I2C_NUM_0, &es_i2c_cfg);
    if (ret != ESP_OK) {
        AUDIO_RET_ON_FALSE(TAG, ret, return ESP_FAIL, "getting i2c pins error");
    }
    
    self->i2c = i2c_bus_create(I2C_NUM_0, &es_i2c_cfg);
    return ret;
}

static void sync_system_time_from_rtc(const struct tm *time)
{
    time_t rtc_timestamp = mktime((struct tm*)time);
    if (rtc_timestamp == -1) {
        ESP_LOGW(TAG, "Failed to convert RTC time to timestamp");
        return;
    }
    
    struct timeval tv;
    tv.tv_sec = rtc_timestamp;
    tv.tv_usec = 0;
    
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGW(TAG, "Failed to set system time from RTC");
        return;
    } 
    
    ESP_LOGI(TAG, "System time set from RTC: %04d-%02d-%02d %02d:%02d:%02d",
             time->tm_year + 1900, time->tm_mon + 1, time->tm_mday,
             time->tm_hour, time->tm_min, time->tm_sec);
}

int my_rtc_init(my_rtc_handle_t *self_out) {
    if(self_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    auto* self = (struct my_rtc_impl*)malloc(sizeof(struct my_rtc_impl));
    if(self == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    memset(self, 0, sizeof(struct my_rtc_impl));
    
    // 初始化I2C总线
    int ret = i2c_init(self);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed");
        free(self);
        return ret;
    }

    ESP_LOGI(TAG, "I2C bus initialized successfully");

    // 检查PCF8563是否在预期地址
    if (i2c_bus_probe_addr(self->i2c, PCF8563_I2C_ADDR) != ESP_OK) {
        ESP_LOGE(TAG, "PCF8563 not found at address 0x%02X", PCF8563_I2C_ADDR);
        free(self);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "PCF8563 found at address 0x%02X", PCF8563_I2C_ADDR);

    // 读取RTC时间并验证
    vTaskDelay(pdMS_TO_TICKS(500));
    struct tm time;
    esp_err_t r = pcf8563_get_time(self->i2c, &time, &self->valid);
    
    if (r != ESP_OK) {
        printf("Error %d: %s\n", r, esp_err_to_name(r));
    } else {
        printf("%04d-%02d-%02d %02d:%02d:%02d, %s\n", time.tm_year + 1900, time.tm_mon + 1,
               time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec, self->valid ? "VALID" : "NOT VALID");
        
        if (self->valid) {
            // 设置默认时区UTC+8
            setenv("TZ", "CST-8", 1);
            tzset();
            // 如果RTC时间有效，设置系统时间戳
            sync_system_time_from_rtc(&time);
        } else {
            ESP_LOGW(TAG, "RTC time is not valid, skipping system time sync");
            // 即使RTC无效，也设置默认时区UTC+8
            setenv("TZ", "CST-8", 1);
            tzset();
        }
    }
    
    *self_out = self;
    return ESP_OK;
}

int my_rtc_get_time(my_rtc_handle_t self, struct tm *time, bool *valid) {
    if (self == NULL || time == NULL || valid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return pcf8563_get_time(self->i2c, time, valid);
}

int my_rtc_set_time(my_rtc_handle_t self, struct tm *time) {
    if (self == NULL || time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int ret = pcf8563_set_time(self->i2c, time);
    if (ret == ESP_OK) {
        // 设置时间成功，清除VL位，标记为有效
        self->valid = true;
    }
    return ret;
}

bool my_rtc_is_time_valid(my_rtc_handle_t self) {
    if (self == NULL) {
        return false;
    }
    return self->valid;
}

int my_rtc_sync_from_ntp(my_rtc_handle_t self) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "Failed to get system time");
        return ESP_FAIL;
    }
    
    // 转换为本地时间
    struct tm timeinfo;
    time_t now = tv.tv_sec;
    localtime_r(&now, &timeinfo);
    
    ESP_LOGI(TAG, "NTP time: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    // 写入RTC
    int ret = my_rtc_set_time(self, &timeinfo);
    if (ret == ESP_OK) {
        self->ntp_synced = true;
        ESP_LOGI(TAG, "RTC synced from NTP successfully");
        // 触发NTP同步成功回调
        if (self->ntp_sync_cb != NULL) {
            self->ntp_sync_cb(self->ntp_sync_context, self);
        }
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to sync RTC from NTP (err=%d)", ret);
        return ESP_FAIL;
    }
}

int my_rtc_start_ntp_sync(my_rtc_handle_t self) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 如果已经在同步，直接返回
    if (self->ntp_syncing) {
        return ESP_OK;
    }
    
    // 初始化NTP（在WiFi连接成功后初始化，确保网络就绪）
    // 使用中国的NTP服务器，通常响应更快
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("cn.pool.ntp.org");
    esp_err_t init_ret = esp_netif_sntp_init(&config);
    if (init_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SNTP: %s", esp_err_to_name(init_ret));
        self->ntp_synced = false;
        return init_ret;
    }
    
    ESP_LOGI(TAG, "SNTP initialized with cn.pool.ntp.org");
    
    // 标记开始同步
    self->ntp_syncing = true;
    self->ntp_retry_count = 0;
    self->ntp_last_check_time = 0;
    
    return ESP_OK;
}

bool my_rtc_is_ntp_synced(my_rtc_handle_t self) {
    if (self == NULL) {
        return false;
    }
    return self->ntp_synced;
}

int my_rtc_reg_cb_ntp_sync(my_rtc_handle_t self, my_rtc_ntp_sync_callback_t func, void *context) {
    if (self == NULL || func == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // 建议只允许被注册一次
    if (self->ntp_sync_cb != NULL) {
        ESP_LOGW(TAG, "NTP sync callback already registered");
        return ESP_ERR_INVALID_STATE;
    }
    self->ntp_sync_cb = func;
    self->ntp_sync_context = context;
    return ESP_OK;
}

int my_rtc_reg_cb_second(my_rtc_handle_t self, my_rtc_second_callback_t func, void *context) {
    if (self == NULL || func == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // 建议只允许被注册一次
    if (self->second_cb != NULL) {
        ESP_LOGW(TAG, "Second callback already registered");
        return ESP_ERR_INVALID_STATE;
    }
    self->second_cb = func;
    self->second_context = context;
    return ESP_OK;
}

int my_rtc_flush(my_rtc_handle_t self, uint32_t interval_ms) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查是否到了执行间隔
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (self->last_flush_time != 0 && 
        (current_time - self->last_flush_time) < interval_ms) {
        // 还没到执行间隔，直接退出
        return ESP_OK;
    }
    self->last_flush_time = current_time;
    
    // 如果正在NTP同步，检查同步状态
    if (self->ntp_syncing) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        const uint32_t wait_interval = 3000;  // 每3秒检查一次
        
        // 检查是否到了等待时间
        if (self->ntp_last_check_time == 0 || 
            (current_time - self->ntp_last_check_time) >= wait_interval) {
            
            self->ntp_last_check_time = current_time;
            
            // 非阻塞检查同步状态
            esp_err_t ret = esp_netif_sntp_sync_wait(0);
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "NTP sync successful");
                self->ntp_syncing = false;
                self->ntp_synced = true;
                // 同步成功后写入RTC
                if (my_rtc_sync_from_ntp(self) == ESP_OK) {
                    ESP_LOGI(TAG, "RTC synced from NTP");
                } else {
                    ESP_LOGE(TAG, "Failed to sync RTC from NTP");
                }
            } else if (ret == ESP_ERR_TIMEOUT) {
                // 继续等待，不限制重试次数
                self->ntp_retry_count++;
                ESP_LOGI(TAG, "Waiting for NTP sync... (retry %d)", self->ntp_retry_count);
            } else {
                // 其他错误
                ESP_LOGE(TAG, "NTP sync error: %s", esp_err_to_name(ret));
                self->ntp_syncing = false;
                self->ntp_synced = false;
            }
        }
    }
    
    // 如果注册了秒回调，检查是否需要触发
    if (self->second_cb != NULL && self->valid) {
        // 使用系统时间而不是RTC时间
        struct timeval tv;
        if (gettimeofday(&tv, NULL) == 0) {
            struct tm timeinfo;
            time_t now = tv.tv_sec;
            localtime_r(&now, &timeinfo);
            
            // 检查是否到了新的一秒
            if (timeinfo.tm_sec != self->last_second) {
                self->last_second = timeinfo.tm_sec;
                // 触发回调，传入时分
                self->second_cb(timeinfo.tm_hour, timeinfo.tm_min, self->second_context, self);
            }
        }
    }
    
    return ESP_OK;
}
