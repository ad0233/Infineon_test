#include "ble_protocol.h"
#include "my_ble.h"
#include "my_utils.h"
#include "rust_lunawake.h"
#include "cmd_parse.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "ble_protocol";

static StreamBufferHandle_t s_ble_recv_stream = NULL;
static single_parse_handle_t s_single_parser = NULL;
static TaskHandle_t s_parse_task_handle = NULL;
static TaskHandle_t s_tx_task_handle = NULL;

// BLE 接收回调，将数据写入流缓冲区
static void ble_recv_callback(const uint8_t *data, uint16_t len, void *context)
{
    StreamBufferHandle_t stream = (StreamBufferHandle_t)context;
    // ESP_LOG_BUFFER_HEXDUMP("BLE_RX", data, len, ESP_LOG_INFO);
    size_t sent = xStreamBufferSend(stream, data, len, 0);
    if (sent != len) {
        ESP_LOGE("BLE_RX", "Stream buffer full, lost %d bytes", len - sent);
    }
}

// 解析任务：从流缓冲区读取数据并解析
static void ble_parse_task(void *arg)
{
    StreamBufferHandle_t ble_recv_stream = (StreamBufferHandle_t)arg;
    
    // 创建解析器
    single_parse_handle_t single_parser = rust_single_parse_new(100 * 1024);
    if (single_parser == nullptr) {
        ESP_LOGE(TAG, "Failed to init single parser");
        vTaskDelete(nullptr);
        return;
    }
    s_single_parser = single_parser;
    
    // 分配接收缓冲区（使用 SPIRAM）
    auto one_packet_len = 10 * 1024;
    auto one_packet_buf = (uint8_t *)heap_caps_malloc(one_packet_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (one_packet_buf == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate packet buffer");
        rust_single_parse_free(single_parser);
        vTaskDelete(nullptr);
        return;
    }
    
    ESP_LOGI(TAG, "BLE parse task started");
    
    while(1) {
        // 从流缓冲区接收数据
        auto len = xStreamBufferReceive(
            ble_recv_stream,
            one_packet_buf, one_packet_len, portMAX_DELAY);
            
        if (len > 0) {
            // 逐字节解析
            for(auto i = 0; i < len; i++) {
                size_t out_len;
                const uint8_t *out_buf = rust_single_parse_unpack(
                    single_parser,
                    one_packet_buf[i],
                    &out_len);
                    
                if (out_len > 0) {
                    ESP_LOGI(TAG, "Parsed packet, len %d", out_len);
                    // ESP_LOG_BUFFER_HEXDUMP(TAG, out_buf, out_len, ESP_LOG_INFO);
                    
                    // 解析命令
                    cmd_parse(out_buf, out_len);
                }
            }
        }
    }
    
    // 清理资源（理论上不会执行到这里）
    heap_caps_free(one_packet_buf);
    rust_single_parse_free(single_parser);
    vTaskDelete(nullptr);
}

// 发送任务：定期发送测试数据
static void ble_tx_task(void *arg)
{
    static uint8_t send_buf[128];
    
    ESP_LOGI(TAG, "BLE TX task started");
    
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        
        int len = rust_single_parse_pack(
            (const uint8_t*)"Hello from ESP32 BLE!", 
            strlen("Hello from ESP32 BLE!"), 
            send_buf, 
            sizeof(send_buf));
            
        if (len > 0) {
            my_ble_send_data(send_buf, len, 1000);
        } else {
            ESP_LOGE("BLE_TX", "Pack failed");
        }
    }
}

int ble_protocol_init()
{
    // 创建接收流缓冲区
    s_ble_recv_stream = my_stream_buffer_create(1024 * 10, 1);
    if (s_ble_recv_stream == NULL) {
        ESP_LOGE(TAG, "Failed to create BLE receive stream buffer");
        return -1;
    }
    
    // 注册 BLE 接收回调
    my_ble_register_recv_callback(ble_recv_callback, s_ble_recv_stream);
    
    // 创建解析任务
    esp_err_t ret = my_thread_create(
        ble_parse_task,
        "ble_parse_task",
        1024 * 6,
        s_ble_recv_stream,
        5,
        &s_parse_task_handle);
        
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create parse task");
        return -1;
    }
    
    ret = xTaskCreate(
        ble_tx_task,
        "ble_tx_task",
        1024 * 6,
        NULL,
        5,
        &s_tx_task_handle);
        
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TX task");
        // 发送任务失败不影响整体功能，只记录错误
    }
    
    ESP_LOGI(TAG, "BLE protocol initialized");
    return 0;
}

int ble_send_response(const char *json_str)
{
    if (!json_str) {
        ESP_LOGE(TAG, "ble_send_response: json_str is NULL");
        return -1;
    }
    
    size_t json_len = strlen(json_str);
    if (json_len == 0) {
        ESP_LOGE(TAG, "ble_send_response: json_str is empty");
        return -1;
    }
    
    // 分配发送缓冲区（使用 SPIRAM）
    size_t send_buf_size = json_len + 1024;  // 预留打包空间
    uint8_t *send_buf = (uint8_t *)heap_caps_malloc(send_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (send_buf == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate send buffer");
        return -1;
    }
    
    // 打包数据
    int packed_len = rust_single_parse_pack(
        (const uint8_t *)json_str,
        json_len,
        send_buf,
        send_buf_size);
    
    if (packed_len > 0) {
        // 发送数据
        int ret = my_ble_send_data(send_buf, packed_len, 1000);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to send BLE response");
            heap_caps_free(send_buf);
            return -1;
        }
    } else {
        ESP_LOGE(TAG, "Failed to pack response data");
        heap_caps_free(send_buf);
        return -1;
    }
    
    heap_caps_free(send_buf);
    return 0;
}

