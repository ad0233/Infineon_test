#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct my_wifi_impl* my_wifi_handle_t;

typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_FAILED
} wifi_state_t;

typedef void (*wifi_event_callback_t)(wifi_state_t state, void *context, my_wifi_handle_t self);

/// 初始化 WiFi，返回实例句柄
int my_wifi_init(my_wifi_handle_t *self_out);

/// 连接到 WiFi 网络
int my_wifi_connect(my_wifi_handle_t self, const char *ssid, const char *password);

/// 断开 WiFi 连接
int my_wifi_disconnect(my_wifi_handle_t self);

/// 获取当前 WiFi 状态
wifi_state_t my_wifi_get_state(my_wifi_handle_t self);

/// 是否已连接
bool my_wifi_is_connected(my_wifi_handle_t self);

/// 设置 WiFi 事件回调
int my_wifi_set_event_callback(my_wifi_handle_t self, wifi_event_callback_t callback, void *context);

/// 获取 IP 地址
int my_wifi_get_ip(my_wifi_handle_t self, char *ip_str, size_t len);

/// 保存 WiFi 凭据到 NVS
int my_wifi_save_credentials(my_wifi_handle_t self, const char *ssid, const char *password);

/// 开机自动连接 WiFi（从 NVS 读取凭据）
int my_wifi_auto_connect(my_wifi_handle_t self);

/// 清除保存的 WiFi 凭据
int my_wifi_clear_credentials(my_wifi_handle_t self);

#ifdef __cplusplus
}
#endif
