#include "cmd_handlers.h"
#include "esp_log.h"
#include <string.h>
#include <string>
#include <time.h>
#include <sys/time.h>
#define JSON_NOEXCEPTION
#include <nlohmann/json.hpp>

#include "my_wifi.h"
#include "my_ota.h"
#include "fsm_main.h"
#include "my_nvs.h"
#include "my_ble.h"
#include "ble_protocol.h"
#include "my_rtc.h"
#include "my_ui_behavior.h"
#include "fsm_main.h"
#include "my_download.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "cmd_handlers";

// 获取无冒号大写 MAC 地址
static const char *get_mac_no_colon() {
    return my_ble_get_mac(false);
}

// 下载回调：写入文件
static void download_chunk_cb(uint8_t *data, int len, void *user_ctx) {
    FILE *fp = (FILE *)user_ctx;
    if (fp) {
        fwrite(data, 1, len, fp);
        fflush(fp);
    }
}

// 处理 WiFi 连接命令
int cmd_handle_wifi_connect(const nlohmann::json &params) {
    if (params.is_null() || !params.contains("ssid") || !params["ssid"].is_string()) {
        ESP_LOGE(TAG, "wifi_connect: ssid not found or invalid");
        return -1;
    }
    
    std::string ssid = params["ssid"].get<std::string>();
    std::string password = params.value("password", std::string(""));
    
    ESP_LOGI(TAG, "WiFi connect: ssid=%s", ssid.c_str());
    
    my_wifi_handle_t wifi_handle = fsm_main_get_wifi_handle();
    if (!wifi_handle) {
        ESP_LOGE(TAG, "WiFi handle is NULL");
        return -1;
    }
    
    esp_err_t ret = my_wifi_connect(wifi_handle, ssid.c_str(), password.c_str());
    bool wifi_connected = false;
    char ip_str[16] = "";
    
    if (ret == ESP_OK) {
        my_wifi_save_credentials(wifi_handle, ssid.c_str(), password.c_str());
        ESP_LOGI(TAG, "WiFi connected and saved");
        
        // 等待连接完成并获取 IP
        int wait_count = 0;
        while (!my_wifi_is_connected(wifi_handle) && wait_count < 50) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }
        
        if (my_wifi_is_connected(wifi_handle)) {
            wifi_connected = true;
            if (my_wifi_get_ip(wifi_handle, ip_str, sizeof(ip_str)) != ESP_OK) {
                ip_str[0] = '\0';
            }
        }
    } else {
        ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(ret));
    }
    
    fsm_main_event_trig(F_MAIN_E_WIFI_CMD_TRIG, NULL);
    
    // 发送响应
    nlohmann::json response = {
        {"type", "provision_result"},
        {"data", {
            {"status", wifi_connected ? "success" : "failed"},
            {"wifi_connected", wifi_connected}
        }}
    };
    
    if (wifi_connected && ip_str[0] != '\0') {
        response["data"]["ip_address"] = ip_str;
    }
    
    std::string json_str = response.dump();
    ble_send_response(json_str.c_str());
    
    ESP_LOGI(TAG, "WiFi connect completed");
    return 0;
}

// 处理忘记WiFi命令
int cmd_handle_forget_wifi(const nlohmann::json &params) {
    ESP_LOGI(TAG, "Forget WiFi credentials");
    
    my_wifi_handle_t wifi_handle = fsm_main_get_wifi_handle();
    if (!wifi_handle) {
        ESP_LOGE(TAG, "WiFi handle is NULL");
        return -1;
    }
    
    esp_err_t ret = my_wifi_clear_credentials(wifi_handle);
    bool success = (ret == ESP_OK);
    
    if (success) {
        ESP_LOGI(TAG, "WiFi credentials cleared");
    } else {
        ESP_LOGE(TAG, "Failed to clear WiFi credentials");
    }
    
    // 发送响应
    nlohmann::json response = {
        {"type", "forget_wifi_result"},
        {"data", {
            {"status", success ? "success" : "failed"}
        }}
    };
    
    std::string json_str = response.dump();
    ble_send_response(json_str.c_str());
    
    ESP_LOGI(TAG, "WiFi forget completed");
    return 0;
}

// 处理 WiFi 配置命令（新格式，复用 wifi_connect 逻辑）
int cmd_handle_wifi_config(const nlohmann::json &data) {
    if (data.is_null() || !data.contains("ssid") || !data["ssid"].is_string()) {
        ESP_LOGE(TAG, "wifi_config: ssid not found or invalid");
        return -1;
    }
    
    std::string ssid = data["ssid"].get<std::string>();
    std::string password = data.value("password", std::string(""));
    
    ESP_LOGI(TAG, "WiFi config: ssid=%s", ssid.c_str());
    
    my_wifi_handle_t wifi_handle = fsm_main_get_wifi_handle();
    if (!wifi_handle) {
        ESP_LOGE(TAG, "WiFi handle is NULL");
        return -1;
    }
    
    esp_err_t ret = my_wifi_connect(wifi_handle, ssid.c_str(), password.c_str());
    if (ret == ESP_OK) {
        my_wifi_save_credentials(wifi_handle, ssid.c_str(), password.c_str());
        ESP_LOGI(TAG, "WiFi connected and saved");
    } else {
        ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(ret));
    }
    
    fsm_main_event_trig(F_MAIN_E_WIFI_CMD_TRIG, NULL);
    
    ESP_LOGI(TAG, "WiFi config completed");
    return 0;
}

// 处理 IoT 配置命令
int cmd_handle_iot_config(const nlohmann::json &data) {
    if (data.is_null() || !data.contains("iot_endpoint") || !data["iot_endpoint"].is_string() ||
        !data.contains("root_ca") || !data["root_ca"].is_string() ||
        !data.contains("certificate_pem") || !data["certificate_pem"].is_string()) {
        ESP_LOGE(TAG, "iot_config: missing required fields");
        return -1;
    }
    
    std::string endpoint = data["iot_endpoint"].get<std::string>();
    uint16_t port = data.value("iot_port", 8883);
    std::string root_ca = data["root_ca"].get<std::string>();
    std::string cert_pem = data["certificate_pem"].get<std::string>();
    std::string cert_id = data.value("certificate_id", std::string(""));
    
    // thing_name: 优先用 JSON 提供的，否则自动用 MAC 地址
    std::string thing_name;
    if (data.contains("thing_name") && data["thing_name"].is_string() && 
        !data["thing_name"].get<std::string>().empty()) {
        thing_name = data["thing_name"].get<std::string>();
    } else {
        const char *mac = get_mac_no_colon();
        if (!mac) {
            ESP_LOGE(TAG, "Failed to get MAC address");
            return -1;
        }
        thing_name = mac;
        ESP_LOGI(TAG, "thing_name auto-filled with MAC: %s", thing_name.c_str());
    }
    
    ESP_LOGI(TAG, "IoT config: endpoint=%s, port=%d, thing=%s", endpoint.c_str(), port, thing_name.c_str());
    
    // 创建配置结构体
    struct iot_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    cfg.version = 1;
    cfg.magic = IOT_CONFIG_MAGIC;
    cfg.iot_port = port;
    cfg.enable = 1;  // 默认启用
    
    snprintf(cfg.iot_endpoint, sizeof(cfg.iot_endpoint), "%s", endpoint.c_str());
    snprintf(cfg.thing_name, sizeof(cfg.thing_name), "%s", thing_name.c_str());
    snprintf(cfg.certificate_id, sizeof(cfg.certificate_id), "%s", cert_id.c_str());
    snprintf(cfg.root_ca, sizeof(cfg.root_ca), "%s", root_ca.c_str());
    snprintf(cfg.certificate_pem, sizeof(cfg.certificate_pem), "%s", cert_pem.c_str());
    
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
int cmd_handle_private_key_config(const nlohmann::json &data) {
    if (data.is_null() || !data.contains("private_key_pem") || !data["private_key_pem"].is_string()) {
        ESP_LOGE(TAG, "private_key_config: private_key_pem not found or invalid");
        return -1;
    }
    
    std::string private_key = data["private_key_pem"].get<std::string>();
    
    ESP_LOGI(TAG, "Private key config");
    
    // 创建私钥配置结构体
    struct private_key_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    cfg.version = 1;
    cfg.magic = PRIVATE_KEY_CONFIG_MAGIC;
    cfg.enable = 1;
    
    snprintf(cfg.private_key_pem, sizeof(cfg.private_key_pem), "%s", private_key.c_str());
    
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
int cmd_handle_test_conn_ota(const nlohmann::json &params) {
    if (params.is_null() || !params.contains("ota_url") || !params["ota_url"].is_string()) {
        ESP_LOGE(TAG, "test_conn_ota: missing required fields (ota_url)");
        return -1;
    }
    
    std::string ota_url = params["ota_url"].get<std::string>();
    int ota_size = params.value("ota_size", 0);
    
    my_wifi_handle_t wifi_handle = fsm_main_get_wifi_handle();
    if (wifi_handle && my_wifi_is_connected(wifi_handle)) {
        ESP_LOGI(TAG, "WiFi already connected");
    }
    
    // 启动 OTA 更新
    int ota_ret = my_ota_begin_v1(ota_url.c_str(), ota_size);
    if (ota_ret != 0) {
        ESP_LOGE(TAG, "OTA start failed: %d", ota_ret);
    } else {
        ESP_LOGI(TAG, "OTA update started");
    }
    
    ESP_LOGI(TAG, "Test conn OTA task created");
    
    // 发送响应
    nlohmann::json response = {
        {"type", "test_conn_ota_result"},
        {"data", {
            {"status", "started"}
        }}
    };
    
    std::string json_str = response.dump();
    ble_send_response(json_str.c_str());
    
    return 0;
}

// 处理设置绑定 JWT 命令
int cmd_handle_set_binding_jwt(const nlohmann::json &params) {
    if (params.is_null() || !params.contains("binding_jwt") || !params["binding_jwt"].is_string()) {
        ESP_LOGE(TAG, "set_binding_jwt: binding_jwt not found or invalid");
        return -1;
    }
    
    std::string binding_jwt = params["binding_jwt"].get<std::string>();
    
    ESP_LOGI(TAG, "Set binding JWT (length: %d)", binding_jwt.length());
    
    // TODO: 保存 binding_jwt 到 NVS 或其他存储
    // 目前只返回成功
    bool success = true;
    
    // 发送响应
    nlohmann::json response = {
        {"type", "set_binding_jwt"},
        {"data", {
            {"status", success ? "success" : "failed"}
        }}
    };
    
    std::string json_str = response.dump();
    ble_send_response(json_str.c_str());
    
    ESP_LOGI(TAG, "Set binding JWT completed");
    return 0;
}

// 处理设置时间命令
int cmd_handle_test_set_time(const nlohmann::json &params) {
    if (params.is_null() || !params.contains("timestamp") || !params["timestamp"].is_number()) {
        ESP_LOGE(TAG, "test_set_time: timestamp not found or invalid");
        return -1;
    }
    
    time_t timestamp = params["timestamp"].get<time_t>();
    std::string timezone = params.value("timezone", std::string("CST-8"));
    
    ESP_LOGI(TAG, "Set time: timestamp=%lld, timezone=%s", timestamp, timezone.c_str());
    
    // 设置时区
    setenv("TZ", timezone.c_str(), 1);
    tzset();
    
    // 设置系统时间
    struct timeval tv;
    tv.tv_sec = timestamp;
    tv.tv_usec = 0;
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "Failed to set system time");
        
        // 发送失败响应
        nlohmann::json response = {
            {"type", "test_set_time_result"},
            {"data", {
                {"status", "failed"},
                {"error", "settimeofday failed"}
            }}
        };
        
        std::string json_str = response.dump();
        ble_send_response(json_str.c_str());
        return -1;
    }
    
    // 转换为本地时间并写入RTC
    struct tm timeinfo;
    localtime_r(&timestamp, &timeinfo);
    
    my_rtc_handle_t rtc_handle = fsm_main_get_rtc_handle();
    int rtc_ret = (rtc_handle != NULL) ? my_rtc_set_time(rtc_handle, &timeinfo) : ESP_ERR_INVALID_STATE;
    if (rtc_ret != 0) {
        ESP_LOGE(TAG, "Failed to set RTC time (err=%d)", rtc_ret);
        
        // 发送部分成功响应（系统时间已设置，但RTC失败）
        nlohmann::json response = {
            {"type", "test_set_time_result"},
            {"data", {
                {"status", "partial"},
                {"error", "RTC write failed"}
            }}
        };
        
        std::string json_str = response.dump();
        ble_send_response(json_str.c_str());
        return -1;
    }
    
    ESP_LOGI(TAG, "Time set successfully: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    // 如果当前在时钟页面，更新LVGL显示
    if (my_ui_get_current_page() == PAGE_CLOCK) {
        my_ui_clock_set_now_time(timeinfo.tm_hour, timeinfo.tm_min);
        ESP_LOGI(TAG, "LVGL clock display updated");
    }
    
    // 发送成功响应
    nlohmann::json response = {
        {"type", "test_set_time_result"},
        {"data", {
            {"status", "success"}
        }}
    };
    
    std::string json_str = response.dump();
    ble_send_response(json_str.c_str());
    
    ESP_LOGI(TAG, "Set time completed");
    return 0;
}

// 处理获取音乐命令
int cmd_handle_test_get_music(const nlohmann::json &params) {
    if (params.is_null() || !params.contains("url") || !params["url"].is_string()) {
        ESP_LOGE(TAG, "test_get_music: url not found or invalid");
        return -1;
    }
    
    std::string url = params["url"].get<std::string>();
    ESP_LOGI(TAG, "Get music from URL: %s", url.c_str());
    
    // // 检查 WiFi 连接
    // my_wifi_handle_t wifi_handle = fsm_main_get_wifi_handle();
    // if (!wifi_handle || !my_wifi_is_connected(wifi_handle)) {
    //     ESP_LOGE(TAG, "WiFi not connected");
        
    //     nlohmann::json response = {
    //         {"type", "test_get_music_result"},
    //         {"data", {
    //             {"status", "failed"},
    //             {"error", "WiFi not connected"}
    //         }}
    //     };
    //     std::string json_str = response.dump();
    //     ble_send_response(json_str.c_str());
    //     return -1;
    // }
    
    // 保存文件路径（固定文件名，会覆盖）
    const char *save_path = "/sdcard/V001-morning.wav";
    FILE *fp = fopen(save_path, "wb");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", save_path);
        
        nlohmann::json response = {
            {"type", "test_get_music_result"},
            {"data", {
                {"status", "failed"},
                {"error", "Failed to open file"}
            }}
        };
        std::string json_str = response.dump();
        ble_send_response(json_str.c_str());
        return -1;
    }
    
    // 下载配置
    my_download_handle_t download_handle = NULL;
    if (my_download_init(&download_handle) != 0) {
        ESP_LOGE(TAG, "Failed to init download");
        fclose(fp);
        
        nlohmann::json response = {
            {"type", "test_get_music_result"},
            {"data", {
                {"status", "failed"},
                {"error", "Failed to init download"}
            }}
        };
        std::string json_str = response.dump();
        ble_send_response(json_str.c_str());
        return -1;
    }
    
    my_download_config_t cfg = {
        .url = url.c_str(),
        .timeout_ms = 30000,
        .on_chunk = download_chunk_cb,
        .on_progress = nullptr,
        .user_ctx = fp
    };
    
    if (my_download_begin(download_handle, &cfg) != 0) {
        ESP_LOGE(TAG, "Failed to begin download");
        fclose(fp);
        my_download_destroy(download_handle);
        
        nlohmann::json response = {
            {"type", "test_get_music_result"},
            {"data", {
                {"status", "failed"},
                {"error", "Failed to begin download"}
            }}
        };
        std::string json_str = response.dump();
        ble_send_response(json_str.c_str());
        return -1;
    }
    
    // 下载数据
    int ret = 0;
    while (true) {
        ret = my_download_flush(download_handle);
        if (ret < 0) {
            ESP_LOGE(TAG, "Download error");
            break;
        } else if (ret > 0) {
            ESP_LOGI(TAG, "Download completed");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    fclose(fp);
    my_download_destroy(download_handle);
    
    if (ret < 0) {
        ESP_LOGE(TAG, "Download failed");
        
        nlohmann::json response = {
            {"type", "test_get_music_result"},
            {"data", {
                {"status", "failed"},
                {"error", "Download failed"}
            }}
        };
        std::string json_str = response.dump();
        ble_send_response(json_str.c_str());
        return -1;
    }
    
    ESP_LOGI(TAG, "Music downloaded successfully to %s", save_path);
    
    // 发送成功响应
    nlohmann::json response = {
        {"type", "test_get_music_result"},
        {"data", {
            {"status", "success"},
            {"file_path", save_path}
        }}
    };
    std::string json_str = response.dump();
    ble_send_response(json_str.c_str());
    
    return 0;
}

