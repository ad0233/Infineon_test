#pragma once

#define JSON_NOEXCEPTION
#include <nlohmann/json.hpp>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 处理 WiFi 连接命令
 * 
 * @param params JSON params 对象
 * @return int 0成功，-1失败
 */
int cmd_handle_wifi_connect(const nlohmann::json &params);

/**
 * @brief 处理忘记 WiFi 命令
 * 
 * @param params JSON params 对象
 * @return int 0成功，-1失败
 */
int cmd_handle_forget_wifi(const nlohmann::json &params);

/**
 * @brief 处理 WiFi 配置命令（新格式）
 * 
 * @param data JSON data 对象
 * @return int 0成功，-1失败
 */
int cmd_handle_wifi_config(const nlohmann::json &data);

/**
 * @brief 处理 IoT 配置命令
 * 
 * @param data JSON data 对象
 * @return int 0成功，-1失败
 */
int cmd_handle_iot_config(const nlohmann::json &data);

/**
 * @brief 处理私钥配置命令
 * 
 * @param data JSON data 对象
 * @return int 0成功，-1失败
 */
int cmd_handle_private_key_config(const nlohmann::json &data);

/**
 * @brief 处理测试连接并OTA更新命令
 * 
 * @param params JSON params 对象
 * @return int 0成功，-1失败
 */
int cmd_handle_test_conn_ota(const nlohmann::json &params);

/**
 * @brief 处理设置绑定 JWT 命令
 * 
 * @param params JSON params 对象
 * @return int 0成功，-1失败
 */
int cmd_handle_set_binding_jwt(const nlohmann::json &params);

/**
 * @brief 处理设置时间命令
 * 
 * @param params JSON params 对象，包含 timestamp 和 timezone
 * @return int 0成功，-1失败
 */
int cmd_handle_test_set_time(const nlohmann::json &params);

/**
 * @brief 处理获取音乐命令
 * 
 * @param params JSON params 对象，包含 url
 * @return int 0成功，-1失败
 */
int cmd_handle_test_get_music(const nlohmann::json &params);

#ifdef __cplusplus
}
#endif

