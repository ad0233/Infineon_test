#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 回调集合（均可为 NULL）
struct my_mqtt_callbacks {
    void (*on_connected)(void *user_ctx);
    void (*on_disconnected)(void *user_ctx);
    void (*on_error)(int err, void *user_ctx); // err 为内部错误码/提示，可能为 0
    void (*on_data)(const char *topic, int topic_len,
                    const char *payload, int payload_len,
                    void *user_ctx);
};

// 扩展初始化：传入 broker_uri、client_id、订阅主题列表与回调集合
int my_mqtt_init(const char *broker_uri,
                    const char *client_id,
                    const char *const *topics,
                    int topic_count,
                    const struct my_mqtt_callbacks *cbs,
                    void *user_ctx);

/**
 * @brief 发布 MQTT 消息
 * @param topic 主题
 * @param payload 消息内容
 * @param payload_len 消息长度（若为 -1，则按字符串长度计算）
 * @param qos QoS 等级（0 或 1）
 * @param retain 是否保留消息
 * @return 成功返回消息 ID（>=0），失败返回 -1
 */
int my_mqtt_publish(const char *topic, const char *payload, int payload_len, int qos, int retain);

#ifdef __cplusplus
}
#endif