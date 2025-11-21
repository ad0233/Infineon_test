#include "mqtt_protocol.h"
#include "my_mqtt.h"
#include "my_utils.h"
#include "ble_protocol.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "mqtt_protocol";

int protocol_publish_radar_data(protocol_type_t protocol, const radar_latest_data_t *data, const char *topic)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "protocol_publish_radar_data: data 参数为空");
        return -1;
    }

    if (topic == NULL) {
        ESP_LOGE(TAG, "protocol_publish_radar_data: topic 参数为空");
        return -1;
    }

    char *json_str = nullptr;
    uint32_t utc_timestamp = get_utc_timestamp_s();
    
    int json_len = my_radar_data_to_json(data, utc_timestamp, &json_str);
    if (json_len < 0 || json_str == NULL) {
        ESP_LOGE(TAG, "protocol_publish_radar_data: JSON 转换失败");
        return -1;
    }

    int ret = -1;
    if (protocol == PROTOCOL_TYPE_MQTT) {
        // MQTT 直接发送原始 JSON
        ret = my_mqtt_publish(topic, json_str, strlen(json_str), 0, 0);
        if (ret != 0) {
            // ESP_LOGE(TAG, "protocol_publish_radar_data: MQTT 发布失败");
        }
        free(json_str);
    } else if (protocol == PROTOCOL_TYPE_BLE) {
        // 蓝牙需要包装为 {"topic":"...","data":{...}} 格式
        cJSON *root = cJSON_CreateObject();
        if (root == NULL) {
            ESP_LOGE(TAG, "protocol_publish_radar_data: 创建 JSON 对象失败");
            free(json_str);
            return -1;
        }

        // 添加 topic
        cJSON_AddStringToObject(root, "topic", topic);

        // 解析原始 JSON 并添加到 data 字段
        cJSON *data_json = cJSON_Parse(json_str);
        if (data_json == NULL) {
            ESP_LOGE(TAG, "protocol_publish_radar_data: 解析 JSON 失败");
            cJSON_Delete(root);
            free(json_str);
            return -1;
        }
        cJSON_AddItemToObject(root, "data", data_json);

        // 转换为字符串
        char *wrapped_json = cJSON_Print(root);
        cJSON_Delete(root);
        free(json_str);

        if (wrapped_json == NULL) {
            ESP_LOGE(TAG, "protocol_publish_radar_data: JSON 打印失败");
            return -1;
        }

        ret = ble_send_response(wrapped_json);
        free(wrapped_json);
        
        if (ret != 0) {
            ESP_LOGE(TAG, "protocol_publish_radar_data: BLE 发送失败");
        }
    } else {
        ESP_LOGE(TAG, "protocol_publish_radar_data: 未知协议类型 %d", protocol);
        free(json_str);
        ret = -1;
    }

    return ret;
}

