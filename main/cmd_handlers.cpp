#include "cmd_handlers.h"
#include "esp_log.h"
#include <string.h>

#include "my_wifi.h"
#include "my_ota.h"
#include "fsm_main.h"
#include "my_nvs.h"
#include "my_ble.h"
#include "ble_protocol.h"
#include "cJSON.h"

static const char *TAG = "cmd_handlers";

// 获取无冒号大写 MAC 地址
static const char *get_mac_no_colon() {
    return my_ble_get_mac(false);
}

// 处理 WiFi 连接命令
int cmd_handle_wifi_connect(cJSON *params) {
    if (!params) {
        ESP_LOGE(TAG, "wifi_connect: params is NULL");
        return -1;
    }
    
    cJSON *ssid_item = cJSON_GetObjectItem(params, "ssid");
    cJSON *password_item = cJSON_GetObjectItem(params, "password");
    
    if (!cJSON_IsString(ssid_item)) {
        ESP_LOGE(TAG, "wifi_connect: ssid not found or invalid");
        return -1;
    }
    
    const char *ssid = ssid_item->valuestring;
    const char *password = cJSON_IsString(password_item) ? password_item->valuestring : "";
    
    ESP_LOGI(TAG, "WiFi connect: ssid=%s", ssid);
    
    esp_err_t ret = my_wifi_connect(ssid, password);
    bool wifi_connected = false;
    char ip_str[16] = "";
    
    if (ret == ESP_OK) {
        my_wifi_save_credentials(ssid, password);
        ESP_LOGI(TAG, "WiFi connected and saved");
        
        // 等待连接完成并获取 IP
        int wait_count = 0;
        while (!my_wifi_is_connected() && wait_count < 50) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }
        
        if (my_wifi_is_connected()) {
            wifi_connected = true;
            if (my_wifi_get_ip(ip_str, sizeof(ip_str)) != ESP_OK) {
                ip_str[0] = '\0';
            }
        }
    } else {
        ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(ret));
    }
    
    fsm_main_event_trig(F_MAIN_E_WIFI_CMD_TRIG, NULL);
    
    // 发送响应
    cJSON *response = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    if (!response || !data) {
        ESP_LOGE(TAG, "Failed to create JSON objects for response");
        if (response) cJSON_Delete(response);
        if (data) cJSON_Delete(data);
    } else {
        cJSON_AddStringToObject(response, "type", "provision_result");
        cJSON_AddStringToObject(data, "status", wifi_connected ? "success" : "failed");
        cJSON_AddBoolToObject(data, "wifi_connected", wifi_connected);
        if (wifi_connected && ip_str[0] != '\0') {
            cJSON_AddStringToObject(data, "ip_address", ip_str);
        }
        cJSON_AddItemToObject(response, "data", data);
        
        char *json_str = cJSON_Print(response);
        if (json_str) {
            ble_send_response(json_str);
            free(json_str);
        }
        cJSON_Delete(response);
    }
    
    ESP_LOGI(TAG, "WiFi connect completed");
    return 0;
}

// 处理忘记WiFi命令
int cmd_handle_forget_wifi(cJSON *params) {
    ESP_LOGI(TAG, "Forget WiFi credentials");
    
    esp_err_t ret = my_wifi_clear_credentials();
    bool success = (ret == ESP_OK);
    
    if (success) {
        ESP_LOGI(TAG, "WiFi credentials cleared");
    } else {
        ESP_LOGE(TAG, "Failed to clear WiFi credentials");
    }
    
    // 发送响应
    cJSON *response = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    if (!response || !data) {
        ESP_LOGE(TAG, "Failed to create JSON objects for response");
        if (response) cJSON_Delete(response);
        if (data) cJSON_Delete(data);
    } else {
        cJSON_AddStringToObject(response, "type", "forget_wifi_result");
        cJSON_AddStringToObject(data, "status", success ? "success" : "failed");
        cJSON_AddItemToObject(response, "data", data);
        
        char *json_str = cJSON_Print(response);
        if (json_str) {
            ble_send_response(json_str);
            free(json_str);
        }
        cJSON_Delete(response);
    }
    
    ESP_LOGI(TAG, "WiFi forget completed");
    return 0;
}

// 处理 WiFi 配置命令（新格式，复用 wifi_connect 逻辑）
int cmd_handle_wifi_config(cJSON *data) {
    if (!data) {
        ESP_LOGE(TAG, "wifi_config: data is NULL");
        return -1;
    }
    
    cJSON *ssid_item = cJSON_GetObjectItem(data, "ssid");
    cJSON *password_item = cJSON_GetObjectItem(data, "password");
    
    if (!cJSON_IsString(ssid_item)) {
        ESP_LOGE(TAG, "wifi_config: ssid not found or invalid");
        return -1;
    }
    
    const char *ssid = ssid_item->valuestring;
    const char *password = cJSON_IsString(password_item) ? password_item->valuestring : "";
    
    ESP_LOGI(TAG, "WiFi config: ssid=%s", ssid);
    
    esp_err_t ret = my_wifi_connect(ssid, password);
    if (ret == ESP_OK) {
        my_wifi_save_credentials(ssid, password);
        ESP_LOGI(TAG, "WiFi connected and saved");
    } else {
        ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(ret));
    }
    
    fsm_main_event_trig(F_MAIN_E_WIFI_CMD_TRIG, NULL);
    
    ESP_LOGI(TAG, "WiFi config completed");
    return 0;
}

// 处理 IoT 配置命令
int cmd_handle_iot_config(cJSON *data) {
    if (!data) {
        ESP_LOGE(TAG, "iot_config: data is NULL");
        return -1;
    }
    
    cJSON *endpoint_item = cJSON_GetObjectItem(data, "iot_endpoint");
    cJSON *port_item = cJSON_GetObjectItem(data, "iot_port");
    cJSON *thing_name_item = cJSON_GetObjectItem(data, "thing_name");
    cJSON *root_ca_item = cJSON_GetObjectItem(data, "root_ca");
    cJSON *cert_pem_item = cJSON_GetObjectItem(data, "certificate_pem");
    cJSON *cert_id_item = cJSON_GetObjectItem(data, "certificate_id");
    
    if (!cJSON_IsString(endpoint_item) || !cJSON_IsString(root_ca_item) || 
        !cJSON_IsString(cert_pem_item)) {
        ESP_LOGE(TAG, "iot_config: missing required fields");
        return -1;
    }
    
    const char *endpoint = endpoint_item->valuestring;
    uint16_t port = cJSON_IsNumber(port_item) ? port_item->valueint : 8883;
    const char *root_ca = root_ca_item->valuestring;
    const char *cert_pem = cert_pem_item->valuestring;
    const char *cert_id = cJSON_IsString(cert_id_item) ? cert_id_item->valuestring : "";
    
    // thing_name: 优先用 JSON 提供的，否则自动用 MAC 地址
    const char *thing_name;
    if (cJSON_IsString(thing_name_item) && strlen(thing_name_item->valuestring) > 0) {
        thing_name = thing_name_item->valuestring;
    } else {
        thing_name = get_mac_no_colon();
        if (!thing_name) {
            ESP_LOGE(TAG, "Failed to get MAC address");
            return -1;
        }
        ESP_LOGI(TAG, "thing_name auto-filled with MAC: %s", thing_name);
    }
    
    ESP_LOGI(TAG, "IoT config: endpoint=%s, port=%d, thing=%s", endpoint, port, thing_name);
    
    // 创建配置结构体
    struct iot_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    cfg.version = 1;
    cfg.magic = IOT_CONFIG_MAGIC;
    cfg.iot_port = port;
    cfg.enable = 1;  // 默认启用
    
    snprintf(cfg.iot_endpoint, sizeof(cfg.iot_endpoint), "%s", endpoint);
    snprintf(cfg.thing_name, sizeof(cfg.thing_name), "%s", thing_name);
    snprintf(cfg.certificate_id, sizeof(cfg.certificate_id), "%s", cert_id);
    snprintf(cfg.root_ca, sizeof(cfg.root_ca), "%s", root_ca);
    snprintf(cfg.certificate_pem, sizeof(cfg.certificate_pem), "%s", cert_pem);
    
    // 保存到 NVS
    if (my_nvs_update_iot_config(&cfg)) {
        ESP_LOGI(TAG, "IoT config saved successfully");
    } else {
        ESP_LOGE(TAG, "Failed to save IoT config");
    }
    
    ESP_LOGI(TAG, "IoT config completed");
    return 0;
}

// 处理私钥配置命令
int cmd_handle_private_key_config(cJSON *data) {
    if (!data) {
        ESP_LOGE(TAG, "private_key_config: data is NULL");
        return -1;
    }
    
    cJSON *private_key_item = cJSON_GetObjectItem(data, "private_key_pem");
    
    if (!cJSON_IsString(private_key_item)) {
        ESP_LOGE(TAG, "private_key_config: private_key_pem not found or invalid");
        return -1;
    }
    
    const char *private_key = private_key_item->valuestring;
    
    ESP_LOGI(TAG, "Private key config");
    
    // 创建私钥配置结构体
    struct private_key_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    cfg.version = 1;
    cfg.magic = PRIVATE_KEY_CONFIG_MAGIC;
    cfg.enable = 1;
    
    snprintf(cfg.private_key_pem, sizeof(cfg.private_key_pem), "%s", private_key);
    
    // 保存到 NVS
    if (my_nvs_update_private_key_config(&cfg)) {
        ESP_LOGI(TAG, "Private key saved successfully");
    } else {
        ESP_LOGE(TAG, "Failed to save private key");
    }
    
    ESP_LOGI(TAG, "Private key config completed");
    return 0;
}

// 测试连接并OTA更新任务参数
struct test_conn_ota_params {
    char ssid[32];
    char password[64];
    char ota_url[512];
    int ota_size;
};


// 处理测试连接并OTA更新命令
int cmd_handle_test_conn_ota(cJSON *params) {
    if (!params) {
        ESP_LOGE(TAG, "test_conn_ota: params is NULL");
        return -1;
    }
    
    cJSON *ota_url_item = cJSON_GetObjectItem(params, "ota_url");
    cJSON *ota_size_item = cJSON_GetObjectItem(params, "ota_size");
    
    if (!cJSON_IsString(ota_url_item)) {
        ESP_LOGE(TAG, "test_conn_ota: missing required fields (ota_url)");
        return -1;
    }
    
    const char *ota_url = ota_url_item->valuestring;
    int ota_size = cJSON_IsNumber(ota_size_item) ? ota_size_item->valueint : 0;
    
    if(my_wifi_is_connected()) {
        ESP_LOGI(TAG, "WiFi already connected");
    }
    
    // 启动 OTA 更新
    int ota_ret = my_ota_begin_v1(ota_url, ota_size);
    if (ota_ret != 0) {
        ESP_LOGE(TAG, "OTA start failed: %d", ota_ret);
    } else {
        ESP_LOGI(TAG, "OTA update started");
    }
    
    ESP_LOGI(TAG, "Test conn OTA task created");
    
    // 发送响应
    cJSON *response = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    if (!response || !data) {
        ESP_LOGE(TAG, "Failed to create JSON objects for response");
        if (response) cJSON_Delete(response);
        if (data) cJSON_Delete(data);
    } else {
        cJSON_AddStringToObject(response, "type", "test_conn_ota_result");
        cJSON_AddStringToObject(data, "status", "started");
        cJSON_AddItemToObject(response, "data", data);
        
        char *json_str = cJSON_Print(response);
        if (json_str) {
            ble_send_response(json_str);
            free(json_str);
        }
        cJSON_Delete(response);
    }
    
    return 0;
}

// 处理设置绑定 JWT 命令
int cmd_handle_set_binding_jwt(cJSON *params) {
    if (!params) {
        ESP_LOGE(TAG, "set_binding_jwt: params is NULL");
        return -1;
    }
    
    cJSON *binding_jwt_item = cJSON_GetObjectItem(params, "binding_jwt");
    
    if (!cJSON_IsString(binding_jwt_item)) {
        ESP_LOGE(TAG, "set_binding_jwt: binding_jwt not found or invalid");
        return -1;
    }
    
    const char *binding_jwt = binding_jwt_item->valuestring;
    
    ESP_LOGI(TAG, "Set binding JWT (length: %d)", strlen(binding_jwt));
    
    // TODO: 保存 binding_jwt 到 NVS 或其他存储
    // 目前只返回成功
    bool success = true;
    
    // 发送响应
    cJSON *response = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    if (!response || !data) {
        ESP_LOGE(TAG, "Failed to create JSON objects for response");
        if (response) cJSON_Delete(response);
        if (data) cJSON_Delete(data);
    } else {
        cJSON_AddStringToObject(response, "type", "set_binding_jwt");
        cJSON_AddStringToObject(data, "status", success ? "success" : "failed");
        cJSON_AddItemToObject(response, "data", data);
        
        char *json_str = cJSON_Print(response);
        if (json_str) {
            ble_send_response(json_str);
            free(json_str);
        }
        cJSON_Delete(response);
    }
    
    ESP_LOGI(TAG, "Set binding JWT completed");
    return 0;
}

