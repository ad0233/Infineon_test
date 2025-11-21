#include "cmd_parse.h"
#include "cmd_handlers.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "cmd_parse";

// JSON 命令解析
static int parse_json_cmd(const char *json_str) {
    cJSON *cmd_item = NULL;
    cJSON *type_item = NULL;
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return -1;
    }
    
    int ret = -1;
    
    // 尝试新格式: {"type": "xxx", "data": {...}}
    type_item = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type_item)) {
        const char *type = type_item->valuestring;
        cJSON *data = cJSON_GetObjectItem(root, "data");
        
        ESP_LOGI(TAG, "Type: %s", type);
        
        if (strcmp(type, "wifi_config") == 0) {
            ret = cmd_handle_wifi_config(data);
        } else if (strcmp(type, "iot_config") == 0) {
            ret = cmd_handle_iot_config(data);
        } else if (strcmp(type, "private_key_config") == 0) {
            ret = cmd_handle_private_key_config(data);
        } else {
            ESP_LOGW(TAG, "Unknown type: %s", type);
            ret = -1;
        }
        goto cleanup;
    }
    
    // 旧格式: {"cmd": "xxx", "params": {...}}
    cmd_item = cJSON_GetObjectItem(root, "cmd");
    if (cJSON_IsString(cmd_item)) {
        const char *cmd = cmd_item->valuestring;
        cJSON *params = cJSON_GetObjectItem(root, "params");
        
        ESP_LOGI(TAG, "Command: %s", cmd);
        
        if (strcmp(cmd, "wifi_connect") == 0) {
            ret = cmd_handle_wifi_connect(params);
        } else if (strcmp(cmd, "test_forget_wifi") == 0) {
            ret = cmd_handle_forget_wifi(params);
        } else if (strcmp(cmd, "test_conn_ota") == 0) {
            ret = cmd_handle_test_conn_ota(params);
        } else if (strcmp(cmd, "set_binding_jwt") == 0) {
            ret = cmd_handle_set_binding_jwt(params);
        } else if (strcmp(cmd, "test_set_time") == 0) {
            ret = cmd_handle_test_set_time(params);
        } else {
            ESP_LOGW(TAG, "Unknown command: %s", cmd);
            ret = -1;
        }
        goto cleanup;
    }
    
    ESP_LOGE(TAG, "Invalid JSON format (no 'type' or 'cmd' field)");
    
cleanup:
    cJSON_Delete(root);
    return ret;
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