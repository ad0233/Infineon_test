/* MQTT Mutual Authentication (DS) */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_secure_cert_read.h"
#include "esp_crt_bundle.h"
#include "my_mqtt.h"

static const char *TAG = "mqtts_example";

// 主题缓存（拷贝入内部缓冲，避免外部生命周期问题）
#define MY_MQTT_MAX_TOPICS     10
#define MY_MQTT_MAX_TOPIC_LEN  192
static char s_topics[MY_MQTT_MAX_TOPICS][MY_MQTT_MAX_TOPIC_LEN];
static int s_topic_count = 0;

// 回调与上下文
static struct my_mqtt_callbacks s_cbs = {0};
static void *s_user_ctx = NULL;

static void my_mqtt_load_topics(const char *const *topics, int topic_count)
{
    s_topic_count = 0;
    if (!topics || topic_count <= 0) return;
    for (int i = 0; i < topic_count && i < MY_MQTT_MAX_TOPICS; ++i) {
        if (!topics[i] || topics[i][0] == '\0') continue;
        snprintf(s_topics[s_topic_count], MY_MQTT_MAX_TOPIC_LEN, "%s", topics[i]);
        s_topic_count++;
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        for (int i = 0; i < s_topic_count; ++i) {
            int msg_id = esp_mqtt_client_subscribe(client, s_topics[i], 0);
            ESP_LOGI(TAG, "subscribe %s, msg_id=%d", s_topics[i], msg_id);
        }
        if (s_cbs.on_connected) s_cbs.on_connected(s_user_ctx);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        if (s_cbs.on_disconnected) s_cbs.on_disconnected(s_user_ctx);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        if (s_cbs.on_data) {
            s_cbs.on_data(event->topic, event->topic_len, event->data, event->data_len, s_user_ctx);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (s_cbs.on_error) s_cbs.on_error(0, s_user_ctx);
        break;

    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(const char *broker_uri, const char *client_id,
                           const char *const *topics, int topic_count,
                           const struct my_mqtt_callbacks *cbs, void *user_ctx)
{
    if (!broker_uri || broker_uri[0] == '\0') {
        ESP_LOGE(TAG, "broker_uri is null or empty");
        vTaskDelete(NULL);
    }

    // 回调
    if (cbs) s_cbs = *cbs; else memset(&s_cbs, 0, sizeof(s_cbs));
    s_user_ctx = user_ctx;

    // 主题
    my_mqtt_load_topics(topics, topic_count);

    // DS 与证书
    esp_ds_data_ctx_t *ds_data = esp_secure_cert_get_ds_ctx();
    if (ds_data == NULL) {
        ESP_LOGE(TAG, "Error in reading DS data from NVS");
        vTaskDelete(NULL);
    }
    char *device_cert = NULL;
    uint32_t len = 0;
    if (esp_secure_cert_get_device_cert(&device_cert, &len) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to obtain the device certificate");
        vTaskDelete(NULL);
    }

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = broker_uri,
            .verification.crt_bundle_attach = esp_crt_bundle_attach,
        },
        .credentials = {
            .client_id = client_id,
            .authentication = {
                .certificate = (const char *)device_cert,
                .key = NULL,
                .ds_data = (void *)ds_data,
            },
        },
        .session = {
            .disable_clean_session = false,
            .keepalive = 60,
        },
    };

    ESP_LOGI(TAG, "broker_uri: %s", broker_uri);
    if (client_id && client_id[0] != '\0') {
        ESP_LOGI(TAG, "client_id: %s", client_id);
    }

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

int my_mqtt_init(const char *broker_uri,
                    const char *client_id,
                    const char *const *topics,
                    int topic_count,
                    const struct my_mqtt_callbacks *cbs,
                    void *user_ctx)
{
    mqtt_app_start(broker_uri, client_id, topics, topic_count, cbs, user_ctx);
    return 0;
}