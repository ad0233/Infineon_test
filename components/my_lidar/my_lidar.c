#include "my_lidar.h"

#ifdef USE_AIRTOUCH_RADAR
#include "my_lidar_airtouch.h"
#else
#include "my_lidar_r60abd1.h"
#endif

#include "esp_log.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>

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

bool my_radar_get_latest_data(radar_latest_data_t *data)
{
    if (data == NULL) {
        return false;
    }
    
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGW(TAG, "艾睿雷达暂不支持该接口");
    return false;
#else
    return r60abd1_get_latest_data(data);
#endif
}

void my_radar_flush(void)
{
#ifdef USE_AIRTOUCH_RADAR
    // 艾睿雷达使用线程模式，不需要 flush
#else
    r60abd1_flush();
#endif
}

int my_radar_data_to_json(const radar_latest_data_t *data, uint32_t timestamp, char **json_str)
{
    if (data == NULL || json_str == NULL) {
        return -1;
    }
    
    // 创建 JSON 对象
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return -1;
    }
    
    // 添加时间戳
    cJSON_AddNumberToObject(root, "timestamp", timestamp);
    
    // 添加体动数据
    cJSON *movement = cJSON_CreateObject();
    cJSON_AddNumberToObject(movement, "value", data->movement_param);
    cJSON_AddNumberToObject(movement, "timestamp", data->movement_timestamp);
    cJSON_AddItemToObject(root, "movement", movement);
    
    // 添加呼吸数据
    cJSON *respiratory = cJSON_CreateObject();
    cJSON_AddNumberToObject(respiratory, "value", data->respiratory_value);
    cJSON_AddNumberToObject(respiratory, "timestamp", data->respiratory_timestamp);
    cJSON_AddItemToObject(root, "respiratory", respiratory);
    
    // 添加心率数据
    cJSON *heart_rate = cJSON_CreateObject();
    cJSON_AddNumberToObject(heart_rate, "value", data->heart_rate_value);
    cJSON_AddNumberToObject(heart_rate, "timestamp", data->heart_rate_timestamp);
    cJSON_AddItemToObject(root, "heart_rate", heart_rate);
    
    // 转换为字符串
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (json_string == NULL) {
        return -1;
    }
    
    *json_str = json_string;
    return strlen(json_string);
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
