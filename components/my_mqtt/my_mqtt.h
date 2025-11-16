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

#ifdef __cplusplus
}
#endif