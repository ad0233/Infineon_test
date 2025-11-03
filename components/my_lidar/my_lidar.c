#include "my_lidar.h"

#ifdef USE_AIRTOUCH_RADAR
#include "my_lidar_airtouch.h"
#else
#include "my_lidar_r60abd1.h"
#endif

#include "esp_log.h"

static const char *TAG = "my_radar";

// ============================================================================
// 通用雷达接口实现（统一API，根据宏定义调用不同实现）
// ============================================================================

void my_radar_init(void)
{
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGI(TAG, "初始化艾睿雷达（BhrDetInfo自动上报模式）");
    airtouch_init();
#else
    ESP_LOGI(TAG, "初始化R60ABD1雷达");
    r60abd1_init();
#endif
}

void my_radar_start(void)
{
#ifdef USE_AIRTOUCH_RADAR
    airtouch_start();
#else
    r60abd1_start();
#endif
}

void my_radar_stop(void)
{
#ifdef USE_AIRTOUCH_RADAR
    airtouch_stop();
#else
    r60abd1_stop();
#endif
}

void my_radar_set_human_presence_callback(radar_human_callback_t callback)
{
#ifdef USE_AIRTOUCH_RADAR
    // 艾睿雷达暂不支持回调，直接打印数据
    ESP_LOGW(TAG, "艾睿雷达暂不支持回调，数据会直接打印到日志");
#else
    r60abd1_set_human_presence_callback(callback);
#endif
}

void my_radar_set_human_movement_callback(radar_human_callback_t callback)
{
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGW(TAG, "艾睿雷达暂不支持回调");
#else
    r60abd1_set_human_movement_callback(callback);
#endif
}

void my_radar_set_respiratory_callback(radar_respiratory_callback_t callback)
{
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGW(TAG, "艾睿雷达暂不支持回调");
#else
    r60abd1_set_respiratory_callback(callback);
#endif
}

void my_radar_set_heart_rate_callback(radar_heart_rate_callback_t callback)
{
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGW(TAG, "艾睿雷达暂不支持回调");
#else
    r60abd1_set_heart_rate_callback(callback);
#endif
}

bool my_radar_is_connected(void)
{
#ifdef USE_AIRTOUCH_RADAR
    return airtouch_is_connected();
#else
    return r60abd1_is_connected();
#endif
}

bool my_radar_get_human_data(radar_human_data_t *data)
{
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGW(TAG, "艾睿雷达暂不支持数据获取接口");
    return false;
#else
    return r60abd1_get_human_data(data);
#endif
}

bool my_radar_get_respiratory_data(radar_respiratory_data_t *data)
{
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGW(TAG, "艾睿雷达暂不支持数据获取接口");
    return false;
#else
    return r60abd1_get_respiratory_data(data);
#endif
}

bool my_radar_get_heart_rate_data(radar_heart_rate_data_t *data)
{
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGW(TAG, "艾睿雷达暂不支持数据获取接口");
    return false;
#else
    return r60abd1_get_heart_rate_data(data);
#endif
}

bool my_radar_get_product_info(radar_product_info_t *info)
{
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGW(TAG, "艾睿雷达不支持产品信息查询");
    return false;
#else
    return r60abd1_get_product_info(info);
#endif
}

// ============================================================================
// R60ABD1特有的命令接口（仅当使用R60ABD1雷达时可用）
// ============================================================================

#ifndef USE_AIRTOUCH_RADAR

bool my_radar_send_command(uint8_t control, uint8_t command, const uint8_t *data, uint16_t length)
{
    return r60abd1_send_command(control, command, data, length);
}

bool my_radar_query_product_info(void)
{
    return r60abd1_query_product_info();
}

bool my_radar_query_human_presence(void)
{
    return r60abd1_query_human_presence();
}

bool my_radar_set_human_switch(bool enable)
{
    return r60abd1_set_human_switch(enable);
}

bool my_radar_set_respiratory_switch(bool enable)
{
    return r60abd1_set_respiratory_switch(enable);
}

bool my_radar_set_heart_rate_switch(bool enable)
{
    return r60abd1_set_heart_rate_switch(enable);
}

bool my_radar_query_human_switch_status(void)
{
    ESP_LOGW(TAG, "该功能需要在R60ABD1雷达实现中补充");
    return false;
}

#endif // USE_AIRTOUCH_RADAR
