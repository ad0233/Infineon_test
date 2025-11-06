#include "cmd_handlers.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "my_wifi.h"
#include "fsm_main.h"
#include "my_nvs.h"
#include "my_ble.h"

static const char *TAG = "cmd_handlers";

// 获取无冒号大写 MAC 地址
static const char *get_mac_no_colon() {
    return my_ble_get_mac(false);
}

// WiFi 连接任务参数
struct wifi_connect_params {
    char ssid[32];
    char password[64];
};

// WiFi 连接任务（在独立任务中执行）
static void wifi_connect_task(void *arg) {
    struct wifi_connect_params *params = (struct wifi_connect_params *)arg;
    
    // 等待一下，确保 WiFi 初始化完成
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "WiFi connect task: ssid=%s", params->ssid);
    
    esp_err_t ret = my_wifi_connect(params->ssid, params->password);
    if (ret == ESP_OK) {
        my_wifi_save_credentials(params->ssid, params->password);
        ESP_LOGI(TAG, "WiFi connected and saved");
    } else {
        ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(ret));
    }
    
    free(params);
    vTaskDelete(NULL);
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
    
    // 分配参数并创建任务
    struct wifi_connect_params *task_params = (struct wifi_connect_params *)malloc(sizeof(struct wifi_connect_params));
    if (!task_params) {
        ESP_LOGE(TAG, "Failed to allocate memory for wifi params");
        return -1;
    }
    
    snprintf(task_params->ssid, sizeof(task_params->ssid), "%s", ssid);
    snprintf(task_params->password, sizeof(task_params->password), "%s", password);
    
    // 在独立任务中执行 WiFi 连接
    BaseType_t ret = xTaskCreate(wifi_connect_task, "wifi_conn", 1024 * 8, task_params, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create wifi connect task");
        free(task_params);
        return -1;
    }
    fsm_main_event_trig(F_MAIN_E_WIFI_CMD_TRIG, NULL);
    
    ESP_LOGI(TAG, "WiFi connect task created");
    return 0;
}

// WiFi 忘记任务（在独立任务中执行）
static void wifi_forget_task(void *arg) {
    ESP_LOGI(TAG, "Forget WiFi credentials");
    
    esp_err_t ret = my_wifi_clear_credentials();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials cleared");
    } else {
        ESP_LOGE(TAG, "Failed to clear WiFi credentials");
    }
    
    vTaskDelete(NULL);
}

// 处理忘记WiFi命令
int cmd_handle_forget_wifi(cJSON *params) {
    // 在独立任务中执行（NVS 写需要内部 RAM 栈）
    BaseType_t ret = xTaskCreate(wifi_forget_task, "wifi_forget", 1024 * 4, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create wifi forget task");
        return -1;
    }
    
    ESP_LOGI(TAG, "WiFi forget task created");
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
    
    struct wifi_connect_params *task_params = (struct wifi_connect_params *)malloc(sizeof(struct wifi_connect_params));
    if (!task_params) {
        ESP_LOGE(TAG, "Failed to allocate memory for wifi params");
        return -1;
    }
    
    snprintf(task_params->ssid, sizeof(task_params->ssid), "%s", ssid);
    snprintf(task_params->password, sizeof(task_params->password), "%s", password);
    
    BaseType_t ret = xTaskCreate(wifi_connect_task, "wifi_conn", 1024 * 8, task_params, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create wifi connect task");
        free(task_params);
        return -1;
    }
    fsm_main_event_trig(F_MAIN_E_WIFI_CMD_TRIG, NULL);
    
    ESP_LOGI(TAG, "WiFi config task created");
    return 0;
}

// IoT 配置任务参数
struct iot_config_params {
    char iot_endpoint[128];
    uint16_t iot_port;
    char thing_name[64];
    char certificate_id[128];
    char *root_ca;
    char *certificate_pem;
};

// IoT 配置任务
static void iot_config_task(void *arg) {
    struct iot_config_params *params = (struct iot_config_params *)arg;
    
    ESP_LOGI(TAG, "IoT config task: endpoint=%s, port=%d, thing=%s", 
             params->iot_endpoint, params->iot_port, params->thing_name);
    
    // 创建配置结构体
    struct iot_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    cfg.version = 1;
    cfg.magic = IOT_CONFIG_MAGIC;
    cfg.iot_port = params->iot_port;
    cfg.enable = 1;  // 默认启用
    
    snprintf(cfg.iot_endpoint, sizeof(cfg.iot_endpoint), "%s", params->iot_endpoint);
    snprintf(cfg.thing_name, sizeof(cfg.thing_name), "%s", params->thing_name);
    snprintf(cfg.certificate_id, sizeof(cfg.certificate_id), "%s", params->certificate_id);
    snprintf(cfg.root_ca, sizeof(cfg.root_ca), "%s", params->root_ca);
    snprintf(cfg.certificate_pem, sizeof(cfg.certificate_pem), "%s", params->certificate_pem);
    
    // 保存到 NVS
    if (my_nvs_update_iot_config(&cfg)) {
        ESP_LOGI(TAG, "IoT config saved successfully");
    } else {
        ESP_LOGE(TAG, "Failed to save IoT config");
    }
    
    free(params->root_ca);
    free(params->certificate_pem);
    free(params);
    vTaskDelete(NULL);
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
    
    // 分配参数
    struct iot_config_params *task_params = (struct iot_config_params *)malloc(sizeof(struct iot_config_params));
    if (!task_params) {
        ESP_LOGE(TAG, "Failed to allocate memory for iot params");
        return -1;
    }
    
    task_params->root_ca = strdup(root_ca);
    task_params->certificate_pem = strdup(cert_pem);
    
    if (!task_params->root_ca || !task_params->certificate_pem) {
        ESP_LOGE(TAG, "Failed to allocate memory for certificates");
        free(task_params->root_ca);
        free(task_params->certificate_pem);
        free(task_params);
        return -1;
    }
    
    snprintf(task_params->iot_endpoint, sizeof(task_params->iot_endpoint), "%s", endpoint);
    task_params->iot_port = port;
    snprintf(task_params->thing_name, sizeof(task_params->thing_name), "%s", thing_name);
    snprintf(task_params->certificate_id, sizeof(task_params->certificate_id), "%s", cert_id);
    
    BaseType_t ret = xTaskCreate(iot_config_task, "iot_cfg", 1024 * 8, task_params, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create iot config task");
        free(task_params->root_ca);
        free(task_params->certificate_pem);
        free(task_params);
        return -1;
    }
    
    ESP_LOGI(TAG, "IoT config task created");
    return 0;
}

// 私钥配置任务
static void private_key_config_task(void *arg) {
    char *private_key = (char *)arg;
    
    ESP_LOGI(TAG, "Private key config task");
    
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
    
    free(private_key);
    vTaskDelete(NULL);
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
    
    // 分配内存并复制私钥
    char *private_key_copy = strdup(private_key);
    if (!private_key_copy) {
        ESP_LOGE(TAG, "Failed to allocate memory for private key");
        return -1;
    }
    
    BaseType_t ret = xTaskCreate(private_key_config_task, "priv_key", 1024 * 8, private_key_copy, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create private key config task");
        free(private_key_copy);
        return -1;
    }
    
    ESP_LOGI(TAG, "Private key config task created");
    return 0;
}

