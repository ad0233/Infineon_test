#include "my_lidar_airtouch.h"
#include "my_lidar.h"
#include "my_ui_behavior.h"  // 添加UI行为头文件

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "string.h"

// ============================================================================
// Rust FFI 函数声明
// ============================================================================

// Rust 的 BhrDetInfo 结构（C 兼容）
typedef struct {
    uint8_t det_result;
    uint8_t br_val;
    uint8_t hr_val;
    int8_t angle_val;
    uint16_t range_val;
    uint16_t padding;
} rust_bhr_det_info_t;

// Rust FFI 函数
extern int32_t rust_radar_parse_bhr_det_info(const uint8_t *data, size_t len, rust_bhr_det_info_t *out);
extern int32_t rust_radar_get_detection_status(uint8_t det_result, uint8_t *buf, size_t buf_len);

// ============================================================================
// 静态变量
// ============================================================================

static const char *TAG = "airtouch_radar";
static QueueHandle_t uart_queue;
static int uart_num = UART_NUM_1;
#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)
#define TXD_PIN (GPIO_NUM_13)
#define RXD_PIN (GPIO_NUM_12)

// 全局变量
static TaskHandle_t rx_task_handle = NULL;
static bool radar_running = false;
static bool radar_connected = false;
static SemaphoreHandle_t data_mutex = NULL;

// 内部函数声明
static void rx_task(void *arg);
static void print_hex_dump(const char *prefix, const uint8_t *data, uint16_t length);

// ============================================================================
// 公共接口实现
// ============================================================================

void airtouch_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 921600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    
    ESP_LOGI(TAG, "初始化艾睿雷达UART...");
    
    // 创建互斥锁
    data_mutex = xSemaphoreCreateMutex();
    if (data_mutex == NULL) {
        ESP_LOGE(TAG, "创建互斥锁失败");
        return;
    }
    
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    // Set UART pins
    ESP_ERROR_CHECK(uart_set_pin(uart_num, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // Install UART driver
    ESP_ERROR_CHECK(uart_driver_install(uart_num, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart_queue, 0));
    
    ESP_LOGI(TAG, "艾睿雷达UART初始化完成");
}

void airtouch_start(void)
{
    if (rx_task_handle != NULL) {
        ESP_LOGW(TAG, "艾睿雷达接收任务已在运行");
        return;
    }
    
    radar_running = true;
    xTaskCreate(rx_task, "airtouch_rx_task", 4096, NULL, 5, &rx_task_handle);
    ESP_LOGI(TAG, "艾睿雷达接收任务已启动");
    ESP_LOGI(TAG, "使用艾睿雷达协议（BhrDetInfo自动上报模式）");
    ESP_LOGI(TAG, "雷达将自动发送呼吸心率检测信息，无需手动开启");
    ESP_LOGI(TAG, "协议解析使用 Rust 实现");
}

void airtouch_stop(void)
{
    radar_running = false;
    if (rx_task_handle != NULL) {
        vTaskDelete(rx_task_handle);
        rx_task_handle = NULL;
    }
    ESP_LOGI(TAG, "艾睿雷达接收任务已停止");
}

bool airtouch_is_connected(void)
{
    return radar_connected;
}

// ============================================================================
// 内部实现
// ============================================================================

static void rx_task(void *arg) {
    uint8_t data[RD_BUF_SIZE];
    uint32_t last_data_time = 0;
    uint8_t frame_buffer[256];  // 帧缓冲区
    uint16_t frame_index = 0;
    bool header_found = false;
    uint8_t payload_len = 0;
    uint16_t expected_total_len = 0;
    
    ESP_LOGI(TAG, "艾睿雷达接收任务开始运行（主动上报协议，帧头0x5A）");
    
    while (radar_running) {
        int len = uart_read_bytes(uart_num, data, RD_BUF_SIZE, 100 / portTICK_PERIOD_MS);
        
        if (len > 0) {
            last_data_time = esp_log_timestamp();
            radar_connected = true;
            
            // 打印原始接收数据（调试用）
            // print_hex_dump("艾睿雷达原始数据", data, len);
            
            // 逐字节处理
            for (int i = 0; i < len; i++) {
                uint8_t byte = data[i];
                
                if (!header_found) {
                    // 查找帧头 0x5A
                    if (byte == AIRTOUCH_HEADER) {
                        frame_buffer[0] = byte;
                        frame_index = 1;
                        header_found = true;
                    }
                } else {
                    // 收集帧数据
                    if (frame_index < sizeof(frame_buffer)) {
                        frame_buffer[frame_index++] = byte;
                    }
                    
                    // 读取长度字段
                    if (frame_index == 2) {
                        payload_len = frame_buffer[1];
                        expected_total_len = 3 + payload_len;  // HEAD + LEN + PAYLOAD + CHECK
                    }
                    
                    // 收集到完整帧
                    if (frame_index >= 3 && frame_index >= expected_total_len) {
                        // ========================================
                        // 验证并解析帧
                        // ========================================
                        
                        // 计算校验和 (HEAD + LEN + PAYLOAD)
                        uint32_t sum = 0;
                        for (int j = 0; j < expected_total_len - 1; j++) {
                            sum += frame_buffer[j];
                        }
                        uint8_t calculated_checksum = (uint8_t)(sum & 0xFF);
                        uint8_t received_checksum = frame_buffer[expected_total_len - 1];
                        
                        if (calculated_checksum == received_checksum) {
                            // 校验通过，解析载荷
                            if (payload_len > 0) {
                                uint8_t msg_type = frame_buffer[2];  // 第一个载荷字节是msg_type
                                
                                // 只处理TYPE=4的呼吸心率检测信息，其他类型跳过
                                if (msg_type == AIRTOUCH_MSG_TYPE_BHR_DET && payload_len == AIRTOUCH_BHR_PAYLOAD_SIZE) {
                                    // 使用 Rust FFI 解析 BhrDetInfo 数据（跳过msg_type字节）
                                    rust_bhr_det_info_t bhr_info;
                                    int32_t parse_result = rust_radar_parse_bhr_det_info(
                                        &frame_buffer[3],  // 跳过 HEAD, LEN, MSG_TYPE
                                        8,  // BhrDetInfo固定8字节
                                        &bhr_info
                                    );
                                    
                                    if (parse_result == 0) {
                                        // 获取检测状态描述（使用 Rust 函数）
                                        char status_buf[64];
                                        rust_radar_get_detection_status(
                                            bhr_info.det_result, 
                                            (uint8_t*)status_buf, 
                                            sizeof(status_buf)
                                        );
                                        
                                        // 打印 BhrDetInfo 数据
                                        ESP_LOGI(TAG, "========================================");
                                        ESP_LOGI(TAG, "呼吸心率检测 [TYPE=0x04]:");
                                        ESP_LOGI(TAG, "  检测状态: %s (0x%02X)", status_buf, bhr_info.det_result);
                                        ESP_LOGI(TAG, "  呼吸频率: %d 次/分", bhr_info.br_val);
                                        ESP_LOGI(TAG, "  心率: %d 次/分", bhr_info.hr_val);
                                        ESP_LOGI(TAG, "  角度值: %d (0x%02X)", bhr_info.angle_val, bhr_info.angle_val);
                                        ESP_LOGI(TAG, "  检测距离: %d mm", bhr_info.range_val);
                                        ESP_LOGI(TAG, "========================================");
                                        
                                        // 更新UI显示（如果当前在雷达显示页面）
                                        if (my_ui_get_current_page() == PAGE_RADAR_DISPLAY) {
                                            // 将 angle_val 转换为角度（需要确认映射关系）
                                            // 假设 angle_val 范围 0-255 映射到 -60° ~ 60°
                                            // 或者 0-120 映射到 -60° ~ 60°（128为中心）
                                            float angle = (bhr_info.angle_val) * (120.0f / 255.0f);  // 以128为中心
                                            int distance_cm = bhr_info.range_val / 10;  // mm转cm
                                            
                                            my_ui_radar_display_update(angle, distance_cm, 
                                                                       bhr_info.hr_val, bhr_info.br_val);
                                        }
                                    } else {
                                        ESP_LOGW(TAG, "Rust 解析 BhrDetInfo 失败，错误码: %ld", parse_result);
                                    }
                                } else {
                                    // 其他消息类型，跳过不处理
                                    // ESP_LOGD(TAG, "跳过消息类型: 0x%02X, 载荷长度=%d", msg_type, payload_len);
                                }
                            }
                        } else {
                            ESP_LOGW(TAG, "校验和错误: 计算=0x%02X, 接收=0x%02X", calculated_checksum, received_checksum);
                        }
                        
                        // 重置状态，准备处理下一个帧（处理粘包）
                        header_found = false;
                        frame_index = 0;
                        payload_len = 0;
                        expected_total_len = 0;
                    }
                }
            }
        } else {
            // 检查连接状态
            if (radar_connected && (esp_log_timestamp() - last_data_time > 2000)) {
                radar_connected = false;
                ESP_LOGW(TAG, "艾睿雷达连接丢失");
            }
        }
    }
    
    ESP_LOGI(TAG, "艾睿雷达接收任务结束");
    vTaskDelete(NULL);
}

static void print_hex_dump(const char *prefix, const uint8_t *data, uint16_t length)
{
    if (length == 0) return;
    
    printf("%s (%d bytes): ", prefix, length);
    for (uint16_t i = 0; i < length; i++) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0 && i < length - 1) {
            printf("\n                     ");
        }
    }
    printf("\n");
}
