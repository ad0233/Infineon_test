#include "mqtt_protocol.h"
#include "fsm_main.h"
#include "my_mqtt.h"
#include "my_utils.h"
#include "ble_protocol.h"
#include "esp_log.h"
#include "my_wifi.h"
#include <string>
#include <sstream>

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

    uint32_t utc_timestamp = get_utc_timestamp_s();
    
    // 使用 C++ 字符串流构建 JSON
    std::ostringstream oss;
    oss << "{\"timestamp\":" << utc_timestamp
        << ",\"movement\":{\"value\":" << static_cast<unsigned int>(data->movement_param)
        << ",\"timestamp\":" << data->movement_timestamp
        << ",\"system_timestamp\":" << data->movement_system_timestamp << "}"
        << ",\"respiratory\":{\"value\":" << static_cast<unsigned int>(data->respiratory_value)
        << ",\"timestamp\":" << data->respiratory_timestamp
        << ",\"system_timestamp\":" << data->respiratory_system_timestamp << "}"
        << ",\"heart_rate\":{\"value\":" << static_cast<unsigned int>(data->heart_rate_value)
        << ",\"timestamp\":" << data->heart_rate_timestamp
        << ",\"system_timestamp\":" << data->heart_rate_system_timestamp << "}}";
    
    std::string json_string = oss.str();

    int ret = -1;
    if (protocol == PROTOCOL_TYPE_MQTT) {
        my_wifi_handle_t wifi_handle = fsm_main_get_wifi_handle();
        if(wifi_handle != NULL && my_wifi_get_state(wifi_handle) != WIFI_STATE_CONNECTED) {
            ESP_LOGD(TAG, "protocol_publish_radar_data: WiFi 未连接");
            return -1;
        }
        // MQTT 直接发送原始 JSON
        ret = my_mqtt_publish(topic, json_string.c_str(), json_string.length(), 0, 0);
        if (ret != 0) {
            // ESP_LOGE(TAG, "protocol_publish_radar_data: MQTT 发布失败");
        }
    } else if (protocol == PROTOCOL_TYPE_BLE) {
        // 蓝牙需要包装为 {"topic":"...","data":{...}} 格式
        // 使用 C++ 字符串流直接构建，避免使用 cJSON
        std::ostringstream ble_oss;
        ble_oss << "{\"topic\":\"" << topic << "\",\"data\":" << json_string << "}";
        std::string ble_json = ble_oss.str();
        
        ret = ble_send_response(ble_json.c_str());
        if (ret != 0) {
            ESP_LOGE(TAG, "protocol_publish_radar_data: BLE 发送失败");
        }
    } else {
        ESP_LOGE(TAG, "protocol_publish_radar_data: 未知协议类型 %d", protocol);
        ret = -1;
    }

    return ret;
}

