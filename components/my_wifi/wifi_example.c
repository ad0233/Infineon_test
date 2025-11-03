/**
 * WiFi自动连接使用示例
 * 
 * ESP32开机自动连接WiFi的实现方式：
 * 
 * 1. NVS存储机制：
 *    - WiFi凭据保存在NVS（非易失性存储）中
 *    - 掉电后数据不会丢失
 *    - NVS空间位于Flash分区
 * 
 * 2. 工作流程：
 *    a. 首次配置：通过蓝牙/串口/AP等方式获取WiFi凭据
 *    b. 保存凭据：调用 my_wifi_save_credentials() 保存到NVS
 *    c. 自动连接：开机时调用 my_wifi_auto_connect()
 * 
 * 3. 默认行为（简化设计）：
 *    - 只要保存了WiFi凭据，开机就自动连接
 *    - 无需额外开关，有凭据=自动连接
 *    - 不想连接就清除凭据
 */

#include "my_wifi.h"
#include "esp_log.h"

static const char *TAG = "wifi_example";

// WiFi事件回调
void wifi_event_handler(wifi_state_t state)
{
    switch (state) {
        case WIFI_STATE_CONNECTING:
            ESP_LOGI(TAG, "正在连接WiFi...");
            break;
        case WIFI_STATE_CONNECTED:
            ESP_LOGI(TAG, "WiFi已连接");
            // 获取IP地址
            char ip[16];
            if (my_wifi_get_ip(ip, sizeof(ip)) == ESP_OK) {
                ESP_LOGI(TAG, "IP地址: %s", ip);
            }
            break;
        case WIFI_STATE_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi已断开");
            break;
        case WIFI_STATE_FAILED:
            ESP_LOGE(TAG, "WiFi连接失败");
            break;
        default:
            break;
    }
}

/**
 * 示例1: 首次配置WiFi
 * 通过蓝牙/串口等方式接收用户输入的WiFi信息
 */
void example_first_time_setup(void)
{
    const char *ssid = "MyWiFi";
    const char *password = "MyPassword123";
    
    // 初始化WiFi
    my_wifi_init();
    my_wifi_set_event_callback(wifi_event_handler);
    
    // 连接WiFi
    esp_err_t ret = my_wifi_connect(ssid, password);
    if (ret == ESP_OK) {
        // 连接成功，保存凭据（自动启用开机连接）
        my_wifi_save_credentials(ssid, password);
        ESP_LOGI(TAG, "WiFi凭据已保存，下次开机自动连接");
    }
}

/**
 * 示例2: 开机自动连接（推荐用法）
 * 在 app_main() 中调用
 */
void example_auto_connect_on_boot(void)
{
    // 初始化WiFi
    my_wifi_init();
    my_wifi_set_event_callback(wifi_event_handler);
    
    // 尝试自动连接
    esp_err_t ret = my_wifi_auto_connect();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "自动连接成功");
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "未配置WiFi或自动连接未启用");
        // 可以引导用户进行配置
    } else {
        ESP_LOGE(TAG, "自动连接失败");
    }
}

/**
 * 示例3: 完整的app_main实现
 */
void example_app_main(void)
{
    // 1. 初始化NVS（必须在最开始）
    // nvs_flash_init(); // 在main.cpp中已初始化
    
    // 2. 初始化WiFi
    my_wifi_init();
    my_wifi_set_event_callback(wifi_event_handler);
    
    // 3. 尝试自动连接
    esp_err_t ret = my_wifi_auto_connect();
    
    if (ret == ESP_ERR_INVALID_STATE) {
        // 没有保存的WiFi凭据，首次使用
        ESP_LOGI(TAG, "首次使用，等待用户配置WiFi...");
        // 这里可以启动蓝牙配网或AP配网
    } else if (ret != ESP_OK) {
        // 连接失败，可能是密码错误或WiFi不可用
        ESP_LOGE(TAG, "自动连接失败，请重新配置");
    }
}

/**
 * 示例4: 清除WiFi配置（不再自动连接）
 */
void example_clear_wifi(void)
{
    // 清除保存的WiFi凭据
    my_wifi_clear_credentials();
    ESP_LOGI(TAG, "WiFi凭据已清除，下次开机不会自动连接");
}

/**
 * 示例5: 更换WiFi
 */
void example_change_wifi(void)
{
    // 1. 断开当前连接
    my_wifi_disconnect();
    
    // 2. 连接新WiFi并保存
    const char *new_ssid = "NewWiFi";
    const char *new_password = "NewPassword123";
    
    esp_err_t ret = my_wifi_connect(new_ssid, new_password);
    if (ret == ESP_OK) {
        // 保存新WiFi（自动覆盖旧凭据）
        my_wifi_save_credentials(new_ssid, new_password);
        ESP_LOGI(TAG, "已更换WiFi并保存");
    }
}

