#include "mqtt_protocol.h"
#include "my_mqtt.h"
#include "my_utils.h"
#include "ble_protocol.h"
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

    // MQTT 协议需要 topic
    if (protocol == PROTOCOL_TYPE_MQTT && topic == NULL) {
        ESP_LOGE(TAG, "protocol_publish_radar_data: MQTT 协议需要 topic 参数");
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
        ret = my_mqtt_publish(topic, json_str, strlen(json_str), 0, 0);
        if (ret != 0) {
            ESP_LOGE(TAG, "protocol_publish_radar_data: MQTT 发布失败");
        }
    } else if (protocol == PROTOCOL_TYPE_BLE) {
        ret = ble_send_response(json_str);
        if (ret != 0) {
            ESP_LOGE(TAG, "protocol_publish_radar_data: BLE 发送失败");
        }
    } else {
        ESP_LOGE(TAG, "protocol_publish_radar_data: 未知协议类型 %d", protocol);
        ret = -1;
    }

    free(json_str);
    return ret;
}

