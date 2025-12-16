#include "my_wifi.h"
#include "my_nvs.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#define MAX_RETRY          5
static const char *TAG = "my_wifi";

//结构体 
struct my_wifi_impl{
    int retry_num;                  //失败次数
    wifi_state_t state;             //wifi状态
    wifi_event_callback_t event_cb; 
    void *event_cb_context;
    esp_netif_t *sta_netif;
};

//改wifi状态/去回调
static void set_wifi_state(my_wifi_handle_t self, wifi_state_t state)
{
    if(!self) return;
    self -> state = state;
    if(self -> event_cb ) {
        self -> event_cb(state, self -> event_cb_context, self);
    }
}

static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    my_wifi_handle_t self = (my_wifi_handle_t)arg;
    if (!self) return;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        set_wifi_state(self, WIFI_STATE_CONNECTING);
        ESP_LOGI(TAG, "WiFi started, connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (self->retry_num < MAX_RETRY) {
            esp_wifi_connect();
            self->retry_num++;
            ESP_LOGI(TAG, "Retry connecting to WiFi (%d/%d)", self->retry_num, MAX_RETRY);
            set_wifi_state(self, WIFI_STATE_CONNECTING);
        } else {
            set_wifi_state(self, WIFI_STATE_FAILED);
            ESP_LOGE(TAG, "Failed to connect to WiFi");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        self->retry_num = 0;
        set_wifi_state(self, WIFI_STATE_CONNECTED);
    }
}

int my_wifi_init(my_wifi_handle_t *self_out)
{
    if (!self_out) return ESP_ERR_INVALID_ARG;

    my_wifi_handle_t self = (my_wifi_handle_t)malloc (sizeof(struct my_wifi_impl));
    if (!self) return ESP_ERR_NO_MEM;
    memset(self, 0, sizeof(struct my_wifi_impl));


    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    self->sta_netif = esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        self,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        self,
                                                        &instance_got_ip));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    ESP_LOGI(TAG, "WiFi initialized");
    *self_out = self;
    return ESP_OK;
}

// 把wifi账号密码放入nvs
int my_wifi_connect(my_wifi_handle_t self, const char *ssid, const char *password)
{
    if(!self || !ssid || strlen(ssid) == 0 ) return ESP_ERR_INVALID_ARG;
    
    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", ssid);
    if (password) {
        snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", password);
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
    self -> retry_num = 0;
    set_wifi_state(self, WIFI_STATE_CONNECTING);
    return ESP_OK;
}

int my_wifi_disconnect(my_wifi_handle_t self)
{
    if(!self) return ESP_ERR_INVALID_ARG;
    set_wifi_state(self, WIFI_STATE_DISCONNECTED);
    return esp_wifi_disconnect();
}

wifi_state_t my_wifi_get_state(my_wifi_handle_t self)
{
    if (!self) return WIFI_STATE_IDLE;
    return self->state;
}

bool my_wifi_is_connected(my_wifi_handle_t self)
{
    if(!self) return false;
    return self -> state == WIFI_STATE_CONNECTED;
}

int my_wifi_set_event_callback(my_wifi_handle_t self, wifi_event_callback_t callback, void *context)
{
    if(!self) return ESP_ERR_INVALID_ARG;
    self -> event_cb = callback;
    self -> event_cb_context = context;
    return ESP_OK;
}

int my_wifi_get_ip(my_wifi_handle_t self, char *ip_str, size_t len)
{
    if(!self || !self -> sta_netif) return ESP_FAIL;
    
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(self -> sta_netif, &ip_info);
    if (ret != ESP_OK) {
        return ret;
    }
    
    snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

int my_wifi_save_credentials(my_wifi_handle_t self, const char *ssid, const char *password)
{
    if (!self ||!ssid || strlen(ssid) == 0 || strlen(ssid) >= 32) return ESP_ERR_INVALID_ARG; 
    
    if (password && strlen(password) >= 64) return ESP_ERR_INVALID_ARG;
    
    // 获取当前配置
    const struct device_config *cfg = my_nvs_get_config();
    if (!cfg) return ESP_FAIL;
    
    // 创建新配置
    struct device_config new_cfg = *cfg;
    
    // 保存WiFi凭据
    memset(new_cfg.wifi_ssid, 0, sizeof(new_cfg.wifi_ssid));
    memset(new_cfg.wifi_password, 0, sizeof(new_cfg.wifi_password));
    snprintf(new_cfg.wifi_ssid, sizeof(new_cfg.wifi_ssid), "%s", ssid);
    if (password) {
        snprintf(new_cfg.wifi_password, sizeof(new_cfg.wifi_password), "%s", password);
    }
    new_cfg.wifi_enable = 1;  // 默认启用WiFi
    
    // 保存到NVS
    if (!my_nvs_update_config(&new_cfg)) return ESP_FAIL;
    
    ESP_LOGI(TAG, "WiFi credentials saved");
    return ESP_OK;
}

int my_wifi_auto_connect(my_wifi_handle_t self)
{
    if(!self) return ESP_ERR_INVALID_ARG;

    const struct device_config *cfg = my_nvs_get_config();
    if(!cfg) return ESP_FAIL;
    if (strlen(cfg->wifi_ssid) == 0 || !cfg->wifi_enable) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "Auto connecting to SSID: %s", cfg -> wifi_ssid);
    return my_wifi_connect(self, cfg -> wifi_ssid, cfg -> wifi_password);
}   

int my_wifi_clear_credentials(my_wifi_handle_t self)
{
    if(!self) return ESP_ERR_INVALID_ARG;

    const struct device_config *cfg = my_nvs_get_config();
    if(!cfg) return ESP_FAIL;

    struct device_config new_cfg = *cfg;

    memset(new_cfg.wifi_ssid, 0, sizeof(new_cfg.wifi_ssid));
    memset(new_cfg.wifi_password, 0, sizeof(new_cfg.wifi_password));
    new_cfg.wifi_enable = 0;

    if (!my_nvs_update_config(&new_cfg)) return ESP_FAIL;

    ESP_LOGI(TAG, "WiFi credentials cleared");
    return ESP_OK;
}