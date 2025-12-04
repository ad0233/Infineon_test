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
            // 如果RTC时间有效，设置系统时间戳
            sync_system_time_from_rtc(&time);
        } else {
            ESP_LOGW(TAG, "RTC time is not valid, skipping system time sync");
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
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to sync RTC from NTP (err=%d)", ret);
        return ESP_FAIL;
    }
}

void my_rtc_set_ntp_synced(my_rtc_handle_t self, bool synced) {
    if (self == NULL) {
        return;
    }
    self->ntp_synced = synced;
    if (synced) {
        ESP_LOGI(TAG, "NTP sync status set to synced");
    } else {
        ESP_LOGI(TAG, "NTP sync status set to not synced");
    }
}

bool my_rtc_is_ntp_synced(my_rtc_handle_t self) {
    if (self == NULL) {
        return false;
    }
    return self->ntp_synced;
}
