#include "my_rtc.h"

#include "i2c_bus.h"
#include <esp_log.h>
#include <pcf8563.h>
#include "board_pins_config.h"
#include "audio_error.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

i2c_bus_handle_t my_rtc_i2c = NULL; 
static const char *TAG = "my_rtc";

static int i2c_init()
{
    int ret = 0;
    i2c_config_t es_i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ret = get_i2c_pins(I2C_NUM_0, &es_i2c_cfg);
    AUDIO_RET_ON_FALSE(TAG, ret, return ESP_FAIL, "getting i2c pins error");
    my_rtc_i2c = i2c_bus_create(I2C_NUM_0, &es_i2c_cfg);
    return ret;
}
static bool s_valid;
int my_rtc_init() {
    // 初始化I2C总线
    int ret = i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed");
        return ret;
    }

    ESP_LOGI(TAG, "I2C bus initialized successfully");

    // 检查PCF8563是否在预期地址
    if (i2c_bus_probe_addr(my_rtc_i2c, PCF8563_I2C_ADDR) == ESP_OK) {
        ESP_LOGI(TAG, "PCF8563 found at address 0x%02X", PCF8563_I2C_ADDR);
    } else {
        ESP_LOGE(TAG, "PCF8563 not found at address 0x%02X", PCF8563_I2C_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    // setup datetime: 2020-04-03 12:35, Sunday
    struct tm time =
    {
        .tm_year = 120, // years since 1900
        .tm_mon  = 3,   // months since January
        .tm_mday = 3,
        .tm_hour = 12,
        .tm_min  = 35,
        .tm_sec  = 10,
        .tm_wday = 0    // days since Sunday
    };
    // ESP_ERROR_CHECK(pcf8563_set_time(my_rtc_i2c, &time));

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_err_t r = pcf8563_get_time(my_rtc_i2c, &time, &s_valid);
    if (r == ESP_OK)
        printf("%04d-%02d-%02d %02d:%02d:%02d, %s\n", time.tm_year + 1900, time.tm_mon + 1,
                time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec, s_valid ? "VALID" : "NOT VALID");
    else
        printf("Error %d: %s\n", r, esp_err_to_name(r));
    
    return ESP_OK;
}

esp_err_t pcf8563_get_time(i2c_bus_handle_t dev, struct tm *time, bool *valid);
int my_rtc_get_time(struct tm *time, bool *valid) {
    return pcf8563_get_time(my_rtc_i2c, time, valid);
}

int my_rtc_set_time(struct tm *time) {
    int ret = pcf8563_set_time(my_rtc_i2c, time);
    if (ret == 0) {
        // 设置时间成功，清除VL位，标记为有效
        s_valid = true;
    }
    return ret;
}

bool my_rtc_is_time_valid() {
    return s_valid;
}

int my_rtc_sync_from_ntp(void) {
    struct timeval tv;
    struct tm timeinfo;
    
    // 获取系统时间（NTP同步后的时间）
    if (gettimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "Failed to get system time");
        return ESP_FAIL;
    }
    
    // 转换为本地时间
    time_t now = tv.tv_sec;
    localtime_r(&now, &timeinfo);
    
    ESP_LOGI(TAG, "NTP time: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    // 写入RTC
    int ret = my_rtc_set_time(&timeinfo);
    if (ret == 0) {
        ESP_LOGI(TAG, "RTC synced from NTP successfully");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to sync RTC from NTP (err=%d)", ret);
        return ESP_FAIL;
    }
}