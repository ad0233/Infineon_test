#ifndef MY_WIFI_H
#define MY_WIFI_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_FAILED
} wifi_state_t;

typedef void (*wifi_event_callback_t)(wifi_state_t state, void *context);

/**
 * @brief 初始化 WiFi
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t my_wifi_init(void);

/**
 * @brief 连接到 WiFi 网络
 * 
 * @param ssid WiFi SSID
 * @param password WiFi 密码
 * @return esp_err_t ESP_OK on success
 */
esp_err_t my_wifi_connect(const char *ssid, const char *password);

/**
 * @brief 断开 WiFi 连接
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t my_wifi_disconnect(void);

/**
 * @brief 获取当前 WiFi 状态
 * 
 * @return wifi_state_t 当前状态
 */
wifi_state_t my_wifi_get_state(void);

/**
 * @brief 检查是否已连接
 * 
 * @return true 已连接
 * @return false 未连接
 */
bool my_wifi_is_connected(void);

/**
 * @brief 设置 WiFi 事件回调
 * 
 * @param callback 回调函数
 * @param context 用户上下文，会传递给回调函数
 */
void my_wifi_set_event_callback(wifi_event_callback_t callback, void *context);

/**
 * @brief 获取 IP 地址
 * 
 * @param ip_str 输出的 IP 字符串缓冲区
 * @param len 缓冲区长度
 * @return esp_err_t ESP_OK on success
 */
esp_err_t my_wifi_get_ip(char *ip_str, size_t len);

/**
 * @brief 保存WiFi凭据到NVS (保存后自动连接)
 * 
 * @param ssid WiFi SSID
 * @param password WiFi 密码
 * @return esp_err_t ESP_OK on success
 */
esp_err_t my_wifi_save_credentials(const char *ssid, const char *password);

/**
 * @brief 开机自动连接WiFi (从NVS读取凭据)
 *        只要保存过WiFi凭据就会自动连接
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t my_wifi_auto_connect(void);

/**
 * @brief 清除保存的WiFi凭据
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t my_wifi_clear_credentials(void);

#ifdef __cplusplus
}
#endif

#endif // MY_WIFI_H


