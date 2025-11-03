#include "my_wifi.h"
#include "my_nvs.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

static const char *TAG = "my_wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          5

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static wifi_state_t s_wifi_state = WIFI_STATE_IDLE;
static wifi_event_callback_t s_event_callback = NULL;
static esp_netif_t *s_sta_netif = NULL;

static void set_wifi_state(wifi_state_t state)
{
    s_wifi_state = state;
    if (s_event_callback) {
        s_event_callback(state);
    }
}

static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        set_wifi_state(WIFI_STATE_CONNECTING);
        ESP_LOGI(TAG, "WiFi started, connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to WiFi (%d/%d)", s_retry_num, MAX_RETRY);
            set_wifi_state(WIFI_STATE_CONNECTING);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            set_wifi_state(WIFI_STATE_FAILED);
            ESP_LOGE(TAG, "Failed to connect to WiFi");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        set_wifi_state(WIFI_STATE_CONNECTED);
    }
}

esp_err_t my_wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    ESP_LOGI(TAG, "WiFi initialized");
    return ESP_OK;
}

esp_err_t my_wifi_connect(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "SSID is empty");
        return ESP_ERR_INVALID_ARG;
    }
    
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
    
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID: %s", ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Unexpected event");
        return ESP_FAIL;
    }
}

esp_err_t my_wifi_disconnect(void)
{
    set_wifi_state(WIFI_STATE_DISCONNECTED);
    return esp_wifi_disconnect();
}

wifi_state_t my_wifi_get_state(void)
{
    return s_wifi_state;
}

bool my_wifi_is_connected(void)
{
    return s_wifi_state == WIFI_STATE_CONNECTED;
}

void my_wifi_set_event_callback(wifi_event_callback_t callback)
{
    s_event_callback = callback;
}

esp_err_t my_wifi_get_ip(char *ip_str, size_t len)
{
    if (!s_sta_netif) {
        return ESP_FAIL;
    }
    
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_sta_netif, &ip_info);
    if (ret == ESP_OK) {
        snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.ip));
    }
    return ret;
}

esp_err_t my_wifi_save_credentials(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) >= 32) {
        ESP_LOGE(TAG, "Invalid SSID");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (password && strlen(password) >= 64) {
        ESP_LOGE(TAG, "Password too long");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 获取当前配置
    const struct device_config *cfg = my_nvs_get_config();
    if (!cfg) {
        ESP_LOGE(TAG, "Failed to get NVS config");
        return ESP_FAIL;
    }
    
    // 创建新配置
    struct device_config new_cfg = *cfg;
    
    // 保存WiFi凭据
    memset(new_cfg.wifi_ssid, 0, sizeof(new_cfg.wifi_ssid));
    memset(new_cfg.wifi_password, 0, sizeof(new_cfg.wifi_password));
    strncpy(new_cfg.wifi_ssid, ssid, sizeof(new_cfg.wifi_ssid) - 1);
    if (password) {
        strncpy(new_cfg.wifi_password, password, sizeof(new_cfg.wifi_password) - 1);
    }
    new_cfg.wifi_enable = 1;  // 默认启用WiFi
    
    // 保存到NVS
    if (!my_nvs_update_config(&new_cfg)) {
        ESP_LOGE(TAG, "Failed to save WiFi credentials to NVS");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "WiFi credentials saved");
    return ESP_OK;
}

esp_err_t my_wifi_auto_connect(void)
{
    // 从NVS读取WiFi配置
    const struct device_config *cfg = my_nvs_get_config();
    if (!cfg) {
        ESP_LOGE(TAG, "Failed to get NVS config");
        return ESP_FAIL;
    }
    
    // 检查SSID是否有效 (有SSID就自动连接)
    if (strlen(cfg->wifi_ssid) == 0) {
        ESP_LOGI(TAG, "No saved WiFi credentials");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 检查WiFi功能是否启用
    if (!cfg->wifi_enable) {
        ESP_LOGI(TAG, "WiFi disabled");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Auto-connecting to SSID: %s", cfg->wifi_ssid);
    return my_wifi_connect(cfg->wifi_ssid, cfg->wifi_password);
}

esp_err_t my_wifi_clear_credentials(void)
{
    // 获取当前配置
    const struct device_config *cfg = my_nvs_get_config();
    if (!cfg) {
        ESP_LOGE(TAG, "Failed to get NVS config");
        return ESP_FAIL;
    }
    
    // 创建新配置
    struct device_config new_cfg = *cfg;
    
    // 清除WiFi凭据
    memset(new_cfg.wifi_ssid, 0, sizeof(new_cfg.wifi_ssid));
    memset(new_cfg.wifi_password, 0, sizeof(new_cfg.wifi_password));
    new_cfg.wifi_enable = 0;
    
    // 保存到NVS
    if (!my_nvs_update_config(&new_cfg)) {
        ESP_LOGE(TAG, "Failed to clear WiFi credentials");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "WiFi credentials cleared");
    return ESP_OK;
}


