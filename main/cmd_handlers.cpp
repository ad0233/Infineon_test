#include "cmd_handlers.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "my_wifi.h"
#include "fsm_main.h"

static const char *TAG = "cmd_handlers";

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

