#include "mqtt_protocol.h"
#include "my_mqtt.h"
#include "my_utils.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "mqtt_protocol";

int mqtt_publish_radar_data(const radar_latest_data_t *data, const char *topic)
{
    if (data == NULL || topic == NULL) {
        ESP_LOGE(TAG, "mqtt_publish_radar_data: 参数为空");
        return -1;
    }

    char *json_str = nullptr;
    uint32_t utc_timestamp = get_utc_timestamp_s();
    
    int json_len = my_radar_data_to_json(data, utc_timestamp, &json_str);
    if (json_len < 0 || json_str == NULL) {
        ESP_LOGE(TAG, "mqtt_publish_radar_data: JSON 转换失败");
        return -1;
    }

    int ret = my_mqtt_publish(topic, json_str, strlen(json_str), 0, 0);
    free(json_str);

    if (ret != 0) {
        ESP_LOGE(TAG, "mqtt_publish_radar_data: MQTT 发布失败");
        return -1;
    }

    return 0;
}

