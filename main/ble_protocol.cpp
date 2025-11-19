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
static uint8_t *s_one_packet_buf = NULL;
static const size_t s_one_packet_len = 10 * 1024;

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

// BLE解析flush函数：从流缓冲区读取数据并解析
void ble_parse_flush(void)
{
    if (s_ble_recv_stream == NULL || s_single_parser == NULL || s_one_packet_buf == NULL) {
        return;
    }
    
    // 非阻塞读取数据
    size_t len = xStreamBufferReceive(
        s_ble_recv_stream,
        s_one_packet_buf, s_one_packet_len, 0);
        
    if (len > 0) {
        // 逐字节解析
        for(size_t i = 0; i < len; i++) {
            size_t out_len;
            const uint8_t *out_buf = rust_single_parse_unpack(
                s_single_parser,
                s_one_packet_buf[i],
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

int ble_protocol_init()
{
    // 创建接收流缓冲区
    s_ble_recv_stream = my_stream_buffer_create(1024 * 10, 1);
    if (s_ble_recv_stream == NULL) {
        ESP_LOGE(TAG, "Failed to create BLE receive stream buffer");
        return -1;
    }
    
    // 创建解析器
    s_single_parser = rust_single_parse_new(100 * 1024);
    if (s_single_parser == nullptr) {
        ESP_LOGE(TAG, "Failed to init single parser");
        return -1;
    }
    
    // 分配接收缓冲区（使用 SPIRAM）
    s_one_packet_buf = (uint8_t *)heap_caps_malloc(s_one_packet_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_one_packet_buf == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate packet buffer");
        rust_single_parse_free(s_single_parser);
        s_single_parser = NULL;
        return -1;
    }
    
    // 注册 BLE 接收回调
    my_ble_register_recv_callback(ble_recv_callback, s_ble_recv_stream);
    
    ESP_LOGI(TAG, "BLE protocol initialized (flush mode)");
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

