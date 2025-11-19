#include "cmd_handlers.h"
#include "esp_log.h"
#include <string.h>

#include "my_wifi.h"
#include "my_ota.h"
#include "fsm_main.h"
#include "my_nvs.h"
#include "my_ble.h"

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
    if (ret == ESP_OK) {
        my_wifi_save_credentials(ssid, password);
        ESP_LOGI(TAG, "WiFi connected and saved");
    } else {
        ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(ret));
    }
    
    fsm_main_event_trig(F_MAIN_E_WIFI_CMD_TRIG, NULL);
    
    ESP_LOGI(TAG, "WiFi connect completed");
    return 0;
}

// 处理忘记WiFi命令
int cmd_handle_forget_wifi(cJSON *params) {
    ESP_LOGI(TAG, "Forget WiFi credentials");
    
    esp_err_t ret = my_wifi_clear_credentials();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials cleared");
    } else {
        ESP_LOGE(TAG, "Failed to clear WiFi credentials");
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
    char ota_url[256];
};

// 测试连接并OTA更新任务
static void test_conn_ota_task(void *arg) {
    struct test_conn_ota_params *params = (struct test_conn_ota_params *)arg;
    
    ESP_LOGI(TAG, "Test conn OTA task: ssid=%s, url=%s", params->ssid, params->ota_url);
    
    // 等待一下，确保 WiFi 初始化完成
    vTaskDelay(pdMS_TO_TICKS(100));
    
    if(my_wifi_is_connected()) {
        ESP_LOGI(TAG, "WiFi already connected");
    } else {
        // 连接 WiFi
        esp_err_t ret = my_wifi_connect(params->ssid, params->password);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(ret));
            free(params);
            vTaskDelete(NULL);
            return;
        }
    }
    
    // 启动 OTA 更新
    int ota_ret = my_ota_start(params->ota_url);
    if (ota_ret != 0) {
        ESP_LOGE(TAG, "OTA start failed: %d", ota_ret);
    } else {
        ESP_LOGI(TAG, "OTA update started");
    }
    
    free(params);
    vTaskDelete(NULL);
}

// 处理测试连接并OTA更新命令
int cmd_handle_test_conn_ota(cJSON *params) {
    if (!params) {
        ESP_LOGE(TAG, "test_conn_ota: params is NULL");
        return -1;
    }
    
    cJSON *ssid_item = cJSON_GetObjectItem(params, "ssid");
    cJSON *password_item = cJSON_GetObjectItem(params, "password");
    cJSON *ota_url_item = cJSON_GetObjectItem(params, "ota_url");
    
    if (!cJSON_IsString(ssid_item) || !cJSON_IsString(ota_url_item)) {
        ESP_LOGE(TAG, "test_conn_ota: missing required fields (ssid or ota_url)");
        return -1;
    }
    
    const char *ssid = ssid_item->valuestring;
    const char *password = cJSON_IsString(password_item) ? password_item->valuestring : "";
    const char *ota_url = ota_url_item->valuestring;
    
    // 分配参数并创建任务
    struct test_conn_ota_params *task_params = (struct test_conn_ota_params *)malloc(sizeof(struct test_conn_ota_params));
    if (!task_params) {
        ESP_LOGE(TAG, "Failed to allocate memory for test_conn_ota params");
        return -1;
    }
    
    snprintf(task_params->ssid, sizeof(task_params->ssid), "%s", ssid);
    snprintf(task_params->password, sizeof(task_params->password), "%s", password);
    snprintf(task_params->ota_url, sizeof(task_params->ota_url), "%s", ota_url);
    
    // 在独立任务中执行
    BaseType_t ret = xTaskCreate(test_conn_ota_task, "test_conn_ota", 1024 * 8, task_params, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create test_conn_ota task");
        free(task_params);
        return -1;
    }
    
    ESP_LOGI(TAG, "Test conn OTA task created");
    return 0;
}

