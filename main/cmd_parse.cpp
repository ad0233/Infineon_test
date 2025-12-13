#include "cmd_parse.h"
#include "cmd_handlers.h"
#include "esp_log.h"
#include <string.h>
#include <string>
#define JSON_NOEXCEPTION
#include <nlohmann/json.hpp>

static const char *TAG = "cmd_parse";

// JSON 命令解析
static int parse_json_cmd(const char *json_str) {
    if (!nlohmann::json::accept(json_str)) {
        ESP_LOGE(TAG, "JSON parse failed: invalid JSON");
        return -1;
    }
    
    nlohmann::json root = nlohmann::json::parse(json_str);
    
    int ret = -1;
    
    // 尝试新格式: {"type": "xxx", "data": {...}}
    if (root.contains("type") && root["type"].is_string()) {
        std::string type = root["type"].get<std::string>();
        nlohmann::json data = root.value("data", nlohmann::json::object());
        
        ESP_LOGI(TAG, "Type: %s", type.c_str());
        
        if (type == "wifi_config") {
            ret = cmd_handle_wifi_config(data);
        } else if (type == "iot_config") {
            ret = cmd_handle_iot_config(data);
        } else if (type == "private_key_config") {
            ret = cmd_handle_private_key_config(data);
        } else {
            ESP_LOGW(TAG, "Unknown type: %s", type.c_str());
            ret = -1;
        }
        return ret;
    }
    
    // 旧格式: {"cmd": "xxx", "params": {...}}
    if (root.contains("cmd") && root["cmd"].is_string()) {
        std::string cmd = root["cmd"].get<std::string>();
        nlohmann::json params = root.value("params", nlohmann::json::object());
        
        ESP_LOGI(TAG, "Command: %s", cmd.c_str());
        
        if (cmd == "wifi_connect") {
            ret = cmd_handle_wifi_connect(params);
        } else if (cmd == "test_forget_wifi") {
            ret = cmd_handle_forget_wifi(params);
        } else if (cmd == "test_conn_ota") {
            ret = cmd_handle_test_conn_ota(params);
        } else if (cmd == "set_binding_jwt") {
            ret = cmd_handle_set_binding_jwt(params);
        } else if (cmd == "test_set_time") {
            ret = cmd_handle_test_set_time(params);
        } else {
            ESP_LOGW(TAG, "Unknown command: %s", cmd.c_str());
            ret = -1;
        }
        return ret;
    }
    
    ESP_LOGE(TAG, "Invalid JSON format (no 'type' or 'cmd' field)");
    return -1;
}

int cmd_parse(const uint8_t *data, int len) {
    if (!data || len <= 1) {
        return -1;
    }
    
    // 原始字节命令
    if (data[0] == 0) {
        ESP_LOGD(TAG, "Raw byte command (not implemented)");
        const uint8_t *raw_data = (const uint8_t*)&data[1];
        return -1;
    }
    // JSON 命令
    else if (data[0] == 1) {
        const char *json_str = (const char*)&data[1];
        int json_len = len - 1;
        
        // 检查是否以 null 结尾，否则创建临时缓冲区
        if (json_str[json_len - 1] != '\0') {
            char *temp = (char*)malloc(json_len + 1);
            if (!temp) {
                ESP_LOGE(TAG, "malloc failed");
                return -1;
            }
            memcpy(temp, json_str, json_len);
            temp[json_len] = '\0';
            
            int ret = parse_json_cmd(temp);
            free(temp);
            return ret;
        } else {
            return parse_json_cmd(json_str);
        }
    } else {
        ESP_LOGE(TAG, "Unknown command type: %d", data[0]);
        return -1;
    }
}