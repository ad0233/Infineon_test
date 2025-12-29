#include "my_lidar_r60abd1.h"
#include "my_lidar.h"
#include "my_ui_behavior.h"
#include "my_utils.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "string.h"

// ============================================================================
// 静态变量
// ============================================================================

static const char *TAG = "r60abd1_radar";
static QueueHandle_t uart_queue;
static int uart_num = UART_NUM_1;
#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)
#define TXD_PIN (GPIO_NUM_22)
#define RXD_PIN (GPIO_NUM_23)

// 全局变量
static bool radar_running = false;
static bool radar_connected = false;
static SemaphoreHandle_t data_mutex = NULL;

// 帧解析状态结构体
typedef struct {
    uint8_t frame_buffer[256];
    uint16_t frame_index;
    bool header_found;
    uint16_t expected_length;
    uint32_t last_data_time;
} frame_parse_state_t;

static frame_parse_state_t parse_state = {0};

// 回调函数
static radar_human_callback_t human_presence_callback = NULL;
static radar_human_callback_t human_movement_callback = NULL;
static radar_respiratory_callback_t respiratory_callback = NULL;
static radar_heart_rate_callback_t heart_rate_callback = NULL;

// 睡眠监测回调函数
static radar_sleep_bed_callback_t sleep_bed_callback = NULL;
static radar_sleep_status_callback_t sleep_status_callback = NULL;
static radar_sleep_duration_callback_t sleep_duration_callback = NULL;
static radar_sleep_quality_score_callback_t sleep_quality_score_callback = NULL;
static radar_sleep_comprehensive_callback_t sleep_comprehensive_callback = NULL;
static radar_sleep_quality_analysis_callback_t sleep_quality_analysis_callback = NULL;
static radar_sleep_abnormal_callback_t sleep_abnormal_callback = NULL;
static radar_sleep_quality_rating_callback_t sleep_quality_rating_callback = NULL;
static radar_sleep_struggle_callback_t sleep_struggle_callback = NULL;
static radar_sleep_no_person_callback_t sleep_no_person_callback = NULL;

// 数据缓存
static radar_human_data_t cached_human_data = {0};
static radar_respiratory_data_t cached_respiratory_data = {0};
static radar_heart_rate_data_t cached_heart_rate_data = {0};
static radar_product_info_t cached_product_info = {0};

// 睡眠监测数据缓存
static radar_sleep_bed_data_t cached_sleep_bed_data = {0};
static radar_sleep_status_data_t cached_sleep_status_data = {0};
static radar_sleep_duration_data_t cached_sleep_duration_data = {0};
static radar_sleep_quality_score_data_t cached_sleep_quality_score_data = {0};
static radar_sleep_comprehensive_data_t cached_sleep_comprehensive_data = {0};
static radar_sleep_quality_analysis_data_t cached_sleep_quality_analysis_data = {0};
static radar_sleep_abnormal_data_t cached_sleep_abnormal_data = {0};
static radar_sleep_quality_rating_data_t cached_sleep_quality_rating_data = {0};
static radar_sleep_struggle_data_t cached_sleep_struggle_data = {0};
static radar_sleep_no_person_data_t cached_sleep_no_person_data = {0};
static bool sleep_switch = false;  // 睡眠监测开关

// 数据更新时间戳
static uint32_t movement_timestamp = 0;
static uint32_t respiratory_timestamp = 0;
static uint32_t heart_rate_timestamp = 0;
static uint32_t movement_system_timestamp = 0;
static uint32_t respiratory_system_timestamp = 0;
static uint32_t heart_rate_system_timestamp = 0;

// 内部函数声明
static bool parse_packet(const uint8_t *data, uint16_t length);
static void parse_human_data(uint8_t command, const uint8_t *payload, uint16_t data_len);
static void parse_respiratory_data(uint8_t command, const uint8_t *payload, uint16_t data_len);
static void parse_heart_rate_data(uint8_t command, const uint8_t *payload, uint16_t data_len);
static void parse_product_info(uint8_t command, const uint8_t *payload, uint16_t data_len);
static void parse_sleep_data(uint8_t command, const uint8_t *payload, uint16_t data_len);

// ============================================================================
// 公共接口实现
// ============================================================================

void r60abd1_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,  // R60ABD1使用115200波特率
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    
    ESP_LOGI(TAG, "初始化R60ABD1雷达UART (115200波特率)...");
    
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
    
    ESP_LOGI(TAG, "R60ABD1雷达UART初始化完成");
}

void r60abd1_start(void)
{
    if (radar_running) {
        ESP_LOGW(TAG, "R60ABD1雷达已在运行");
        return;
    }
    
    radar_running = true;
    // 重置帧解析状态
    memset(&parse_state, 0, sizeof(parse_state));
    
    // 启动时查询产品信息和固件版本
    // r60abd1_query_product_info();
    r60abd1_query_firmware_version();
    
    ESP_LOGI(TAG, "R60ABD1雷达已启动（flush模式）");
}

void r60abd1_stop(void)
{
    radar_running = false;
    ESP_LOGI(TAG, "R60ABD1雷达已停止");
}

void r60abd1_set_human_presence_callback(radar_human_callback_t callback)
{
    human_presence_callback = callback;
}

void r60abd1_set_human_movement_callback(radar_human_callback_t callback)
{
    human_movement_callback = callback;
}

void r60abd1_set_respiratory_callback(radar_respiratory_callback_t callback)
{
    respiratory_callback = callback;
}

void r60abd1_set_heart_rate_callback(radar_heart_rate_callback_t callback)
{
    heart_rate_callback = callback;
}

// 睡眠监测回调函数设置
void r60abd1_set_sleep_bed_callback(radar_sleep_bed_callback_t callback)
{
    sleep_bed_callback = callback;
}

void r60abd1_set_sleep_status_callback(radar_sleep_status_callback_t callback)
{
    sleep_status_callback = callback;
}

void r60abd1_set_sleep_duration_callback(radar_sleep_duration_callback_t callback)
{
    sleep_duration_callback = callback;
}

void r60abd1_set_sleep_quality_score_callback(radar_sleep_quality_score_callback_t callback)
{
    sleep_quality_score_callback = callback;
}

void r60abd1_set_sleep_comprehensive_callback(radar_sleep_comprehensive_callback_t callback)
{
    sleep_comprehensive_callback = callback;
}

void r60abd1_set_sleep_quality_analysis_callback(radar_sleep_quality_analysis_callback_t callback)
{
    sleep_quality_analysis_callback = callback;
}

void r60abd1_set_sleep_abnormal_callback(radar_sleep_abnormal_callback_t callback)
{
    sleep_abnormal_callback = callback;
}

void r60abd1_set_sleep_quality_rating_callback(radar_sleep_quality_rating_callback_t callback)
{
    sleep_quality_rating_callback = callback;
}

void r60abd1_set_sleep_struggle_callback(radar_sleep_struggle_callback_t callback)
{
    sleep_struggle_callback = callback;
}

void r60abd1_set_sleep_no_person_callback(radar_sleep_no_person_callback_t callback)
{
    sleep_no_person_callback = callback;
}

bool r60abd1_is_connected(void)
{
    return radar_connected;
}

bool r60abd1_get_human_data(radar_human_data_t *data)
{
    if (data == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = cached_human_data;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_get_respiratory_data(radar_respiratory_data_t *data)
{
    if (data == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = cached_respiratory_data;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_get_heart_rate_data(radar_heart_rate_data_t *data)
{
    if (data == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = cached_heart_rate_data;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_get_product_info(radar_product_info_t *info)
{
    if (info == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *info = cached_product_info;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_get_latest_data(radar_latest_data_t *data)
{
    if (data == NULL) return false;
    
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        data->movement_param = cached_human_data.movement_param;
        data->movement_timestamp = movement_timestamp;
        data->respiratory_value = cached_respiratory_data.respiratory_value;
        data->respiratory_timestamp = respiratory_timestamp;
        data->heart_rate_value = cached_heart_rate_data.heart_rate_value;
        data->heart_rate_timestamp = heart_rate_timestamp;
        data->heart_rate_system_timestamp = heart_rate_system_timestamp;
        data->respiratory_system_timestamp = respiratory_system_timestamp;
        data->movement_system_timestamp = movement_system_timestamp;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

// 睡眠监测数据获取
bool r60abd1_get_sleep_bed_data(radar_sleep_bed_data_t *data)
{
    if (data == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = cached_sleep_bed_data;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_get_sleep_status_data(radar_sleep_status_data_t *data)
{
    if (data == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = cached_sleep_status_data;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_get_sleep_duration_data(radar_sleep_duration_data_t *data)
{
    if (data == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = cached_sleep_duration_data;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_get_sleep_quality_score_data(radar_sleep_quality_score_data_t *data)
{
    if (data == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = cached_sleep_quality_score_data;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_get_sleep_comprehensive_data(radar_sleep_comprehensive_data_t *data)
{
    if (data == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = cached_sleep_comprehensive_data;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_get_sleep_quality_analysis_data(radar_sleep_quality_analysis_data_t *data)
{
    if (data == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = cached_sleep_quality_analysis_data;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_get_sleep_abnormal_data(radar_sleep_abnormal_data_t *data)
{
    if (data == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = cached_sleep_abnormal_data;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_get_sleep_quality_rating_data(radar_sleep_quality_rating_data_t *data)
{
    if (data == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = cached_sleep_quality_rating_data;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_get_sleep_struggle_data(radar_sleep_struggle_data_t *data)
{
    if (data == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = cached_sleep_struggle_data;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_get_sleep_no_person_data(radar_sleep_no_person_data_t *data)
{
    if (data == NULL) return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *data = cached_sleep_no_person_data;
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool r60abd1_send_command(uint8_t control, uint8_t command, const uint8_t *data, uint16_t length)
{
    if (length > RADAR_MAX_DATA_LENGTH) {
        ESP_LOGE(TAG, "数据长度超出限制: %d", length);
        return false;
    }
    
    // 构建数据包
    uint8_t packet[128];
    uint16_t index = 0;
    
    // 帧头
    packet[index++] = RADAR_FRAME_HEADER_1;
    packet[index++] = RADAR_FRAME_HEADER_2;
    
    // 控制字和命令字
    packet[index++] = control;
    packet[index++] = command;
    
    // 数据长度（大端序）
    packet[index++] = (uint8_t)((length >> 8) & 0xFF);
    packet[index++] = (uint8_t)(length & 0xFF);
    
    // 数据内容
    if (data != NULL && length > 0) {
        memcpy(&packet[index], data, length);
        index += length;
    }
    
    // 计算校验和：从帧头到数据结束（索引0到index-1）
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < index; i++) {
        checksum += packet[i];
    }
    packet[index++] = checksum;
    
    // 帧尾
    packet[index++] = RADAR_FRAME_TAIL_1;
    packet[index++] = RADAR_FRAME_TAIL_2;
    
    // 发送数据
    int len = uart_write_bytes(uart_num, packet, index);
    if (len == index) {
        ESP_LOGD(TAG, "发送命令: CTRL=0x%02X, CMD=0x%02X, LEN=%d", control, command, length);
        return true;
    } else {
        ESP_LOGE(TAG, "发送命令失败: 期望%d字节，实际%d字节", index, len);
        return false;
    }
}

bool r60abd1_query_product_info(void)
{
    return r60abd1_send_command(RADAR_CTRL_PRODUCT, RADAR_CMD_PRODUCT_MODEL, NULL, 0);
}

bool r60abd1_query_firmware_version(void)
{
    return r60abd1_send_command(RADAR_CTRL_PRODUCT, RADAR_CMD_FIRMWARE_VERSION, NULL, 0);
}

bool r60abd1_query_human_presence(void)
{
    return r60abd1_send_command(RADAR_CTRL_HUMAN, RADAR_CMD_HUMAN_QUERY_PRESENCE, NULL, 0);
}

bool r60abd1_query_human_motion(void)
{
    return r60abd1_send_command(RADAR_CTRL_HUMAN, RADAR_CMD_HUMAN_MOTION, NULL, 0);
}

bool r60abd1_set_human_switch(bool enable)
{
    uint8_t data = enable ? 0x01 : 0x00;
    return r60abd1_send_command(RADAR_CTRL_HUMAN, RADAR_CMD_HUMAN_SWITCH, &data, 1);
}

bool r60abd1_set_respiratory_switch(bool enable)
{
    uint8_t data = enable ? 0x01 : 0x00;
    return r60abd1_send_command(RADAR_CTRL_RESPIRATORY, RADAR_CMD_RESPIRATORY_SWITCH, &data, 1);
}

bool r60abd1_set_heart_rate_switch(bool enable)
{
    uint8_t data = enable ? 0x01 : 0x00;
    return r60abd1_send_command(RADAR_CTRL_HEART_RATE, RADAR_CMD_HEART_RATE_SWITCH, &data, 1);
}

// ============================================================================
// 内部实现
// ============================================================================

void r60abd1_flush(void)
{
    if (!radar_running) {
        return;
    }
    
    uint8_t data[RD_BUF_SIZE];
    int len = uart_read_bytes(uart_num, data, RD_BUF_SIZE, 0);  // 非阻塞读取
    
    if (len > 0) {
        parse_state.last_data_time = esp_log_timestamp();
        radar_connected = true;
        
        // 逐字节处理数据包
        for (int i = 0; i < len; i++) {
            uint8_t byte = data[i];
            
            if (!parse_state.header_found) {
                // 查找帧头 0x53 0x59
                if (byte == RADAR_FRAME_HEADER_1) {
                    parse_state.frame_buffer[0] = byte;
                    parse_state.frame_index = 1;
                    parse_state.header_found = true;
                }
            } else {
                if (parse_state.frame_index < sizeof(parse_state.frame_buffer)) {
                    parse_state.frame_buffer[parse_state.frame_index++] = byte;
                }
                
                // 检查是否找到完整的帧头
                if (parse_state.frame_index == 2) {
                    if (parse_state.frame_buffer[1] != RADAR_FRAME_HEADER_2) {
                        // 帧头不匹配，重新开始
                        parse_state.header_found = false;
                        parse_state.frame_index = 0;
                        continue;
                    }
                }
                
                // 检查是否收集到长度信息
                if (parse_state.frame_index == 6) {
                    // 数据长度是大端序
                    uint16_t data_len = (parse_state.frame_buffer[4] << 8) | parse_state.frame_buffer[5];
                    // 总包长度 = 帧头(2) + 控制字(1) + 命令字(1) + 长度(2) + 数据(n) + 校验(1) + 帧尾(2)
                    parse_state.expected_length = 9 + data_len;
                }
                
                // 检查是否收集到完整的数据包
                if (parse_state.frame_index >= 6 && parse_state.frame_index >= parse_state.expected_length) {
                    // 解析数据包
                    if (parse_packet(parse_state.frame_buffer, parse_state.frame_index)) {
                        // 解析成功
                    }
                    
                    // 重置状态
                    parse_state.header_found = false;
                    parse_state.frame_index = 0;
                    parse_state.expected_length = 0;
                }
            }
        }
    } else {
        // 检查连接状态
        if (radar_connected && (esp_log_timestamp() - parse_state.last_data_time > 2000)) {
            radar_connected = false;
            ESP_LOGW(TAG, "R60ABD1雷达连接丢失");
        }
    }
}

// ============================================================================
// 数据解析函数
// ============================================================================

static void parse_human_data(uint8_t command, const uint8_t *payload, uint16_t data_len)
{
    bool need_callback = false;
    bool need_movement_callback = false;
    
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    switch (command) {
        case RADAR_CMD_HUMAN_PRESENCE:
        case RADAR_CMD_HUMAN_QUERY_PRESENCE:
            // 人体存在信息
            if (data_len < 1) {
                goto error;
            }
            cached_human_data.presence = (payload[0] != 0);
            need_callback = true;
            break;
            
        case RADAR_CMD_HUMAN_MOVEMENT:
            // 体动参数
            if (data_len < 1) {
                goto error;
            }
            cached_human_data.movement_param = payload[0];
            movement_timestamp = get_utc_timestamp_s();
            movement_system_timestamp = esp_log_timestamp();
            need_movement_callback = true;
            break;
            
        case RADAR_CMD_HUMAN_DISTANCE:
            // 人体距离（小端序）
            if (data_len < 2) {
                goto error;
            }
            cached_human_data.distance = payload[0] | (payload[1] << 8);
            break;
    }
    
    xSemaphoreGive(data_mutex);
    goto callback;
    
error:
    xSemaphoreGive(data_mutex);
    return;
    
callback:
    // 在锁外触发回调
    if (need_callback && human_presence_callback != NULL) {
        human_presence_callback(&cached_human_data);
    }
    if (need_movement_callback && human_movement_callback != NULL) {
        human_movement_callback(&cached_human_data);
    }
}

static void parse_respiratory_data(uint8_t command, const uint8_t *payload, uint16_t data_len)
{
    bool need_callback = false;
    
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    switch (command) {
        case RADAR_CMD_RESPIRATORY_SWITCH:
            if (data_len < 1) {
                goto error;
            }
            cached_respiratory_data.respiratory_switch = (payload[0] == 0x01);
            need_callback = true;
            break;
        case RADAR_CMD_RESPIRATORY_INFO:
            if (data_len < 1) {
                goto error;
            }
            cached_respiratory_data.respiratory_info = payload[0];
            need_callback = true;
            break;
        case RADAR_CMD_RESPIRATORY_VALUE:
            if (data_len < 1) {
                goto error;
            }
            cached_respiratory_data.respiratory_value = payload[0];
            respiratory_timestamp = get_utc_timestamp_s();
            respiratory_system_timestamp = esp_log_timestamp();
            need_callback = true;
            break;
        case RADAR_CMD_RESPIRATORY_WAVEFORM:
            if (data_len < 5) {
                goto error;
            }
            memcpy(cached_respiratory_data.respiratory_waveform, payload, 5);
            need_callback = true;
            break;
        case RADAR_CMD_RESPIRATORY_SLOW:
            if (data_len < 1) {
                goto error;
            }
            cached_respiratory_data.slow_respiratory = payload[0];
            need_callback = true;
            break;
        case RADAR_CMD_RESPIRATORY_UPLOAD:
            if (data_len < 1) {
                goto error;
            }
            cached_respiratory_data.waveform_upload_switch = (payload[0] == 0x01);
            need_callback = true;
            break;
    }
    
    xSemaphoreGive(data_mutex);
    goto callback;
    
error:
    xSemaphoreGive(data_mutex);
    return;
    
callback:
    // 在锁外触发回调
    if (need_callback && respiratory_callback != NULL) {
        respiratory_callback(&cached_respiratory_data);
    }
}

static void parse_heart_rate_data(uint8_t command, const uint8_t *payload, uint16_t data_len)
{
    bool need_callback = false;
    
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    switch (command) {
        case RADAR_CMD_HEART_RATE_SWITCH:
            if (data_len < 1) {
                goto error;
            }
            cached_heart_rate_data.heart_rate_switch = (payload[0] == 0x01);
            need_callback = true;
            break;
        case RADAR_CMD_HEART_RATE_VALUE:
            if (data_len < 1) {
                goto error;
            }
            cached_heart_rate_data.heart_rate_value = payload[0];
            heart_rate_timestamp = get_utc_timestamp_s();
            heart_rate_system_timestamp = esp_log_timestamp();
            need_callback = true;
            break;
        case RADAR_CMD_HEART_RATE_WAVEFORM:
            if (data_len < 5) {
                goto error;
            }
            memcpy(cached_heart_rate_data.heart_rate_waveform, payload, 5);
            need_callback = true;
            break;
    }
    
    xSemaphoreGive(data_mutex);
    goto callback;
    
error:
    xSemaphoreGive(data_mutex);
    return;
    
callback:
    // 在锁外触发回调
    if (need_callback && heart_rate_callback != NULL) {
        heart_rate_callback(&cached_heart_rate_data);
    }
}

static void parse_product_info(uint8_t command, const uint8_t *payload, uint16_t data_len)
{
    if (data_len == 0) {
        return;
    }
    
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    if (command == RADAR_CMD_PRODUCT_MODEL) {
        if (data_len >= sizeof(cached_product_info.product_model)) {
            goto error;
        }
        memcpy(cached_product_info.product_model, payload, data_len);
        cached_product_info.product_model[data_len] = '\0';
        ESP_LOGI(TAG, "产品型号: %s", cached_product_info.product_model);
    } else if (command == RADAR_CMD_FIRMWARE_VERSION) {
        if (data_len >= sizeof(cached_product_info.firmware_version)) {
            goto error;
        }
        memcpy(cached_product_info.firmware_version, payload, data_len);
        cached_product_info.firmware_version[data_len] = '\0';
        ESP_LOGI(TAG, "固件版本: %s", cached_product_info.firmware_version);
    }
    
    xSemaphoreGive(data_mutex);
    return;
    
error:
    xSemaphoreGive(data_mutex);
}

static void parse_sleep_data(uint8_t command, const uint8_t *payload, uint16_t data_len)
{
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    switch (command) {
        case RADAR_CMD_SLEEP_SWITCH:
        case RADAR_CMD_SLEEP_QUERY_SWITCH:
            // 睡眠监测开关
            if (data_len < 1) {
                goto error;
            }
            sleep_switch = (payload[0] == 0x01);
            break;
            
        case RADAR_CMD_SLEEP_BED_STATUS:
        case RADAR_CMD_SLEEP_QUERY_BED_STATUS:
            // 入床/离床状态
            if (data_len < 1) {
                goto error;
            }
            cached_sleep_bed_data.status = (radar_sleep_bed_status_t)payload[0];
            cached_sleep_bed_data.timestamp = get_utc_timestamp_s();
            cached_sleep_bed_data.system_timestamp = esp_log_timestamp();
            break;
            
        case RADAR_CMD_SLEEP_STATUS:
        case RADAR_CMD_SLEEP_QUERY_STATUS:
            // 睡眠状态
            if (data_len < 1) {
                goto error;
            }
            cached_sleep_status_data.status = (radar_sleep_status_t)payload[0];
            cached_sleep_status_data.timestamp = get_utc_timestamp_s();
            cached_sleep_status_data.system_timestamp = esp_log_timestamp();
            break;
            
        case RADAR_CMD_SLEEP_AWAKE_DURATION:
        case RADAR_CMD_SLEEP_QUERY_AWAKE_DURATION:
            // 清醒时长
            if (data_len < 2) {
                goto error;
            }
            cached_sleep_duration_data.awake_duration = (payload[0] << 8) | payload[1];
            cached_sleep_duration_data.timestamp = get_utc_timestamp_s();
            cached_sleep_duration_data.system_timestamp = esp_log_timestamp();
            break;
            
        case RADAR_CMD_SLEEP_LIGHT_DURATION:
        case RADAR_CMD_SLEEP_QUERY_LIGHT_DURATION:
            // 浅睡时长
            if (data_len < 2) {
                goto error;
            }
            cached_sleep_duration_data.light_duration = (payload[0] << 8) | payload[1];
            cached_sleep_duration_data.timestamp = get_utc_timestamp_s();
            cached_sleep_duration_data.system_timestamp = esp_log_timestamp();
            break;
            
        case RADAR_CMD_SLEEP_DEEP_DURATION:
        case RADAR_CMD_SLEEP_QUERY_DEEP_DURATION:
            // 深睡时长
            if (data_len < 2) {
                goto error;
            }
            cached_sleep_duration_data.deep_duration = (payload[0] << 8) | payload[1];
            cached_sleep_duration_data.timestamp = get_utc_timestamp_s();
            cached_sleep_duration_data.system_timestamp = esp_log_timestamp();
            break;
            
        case RADAR_CMD_SLEEP_QUALITY_SCORE:
        case RADAR_CMD_SLEEP_QUERY_QUALITY_SCORE:
            // 睡眠质量评分
            if (data_len < 1) {
                goto error;
            }
            cached_sleep_quality_score_data.quality_score = payload[0];
            cached_sleep_quality_score_data.timestamp = get_utc_timestamp_s();
            cached_sleep_quality_score_data.system_timestamp = esp_log_timestamp();
            break;
            
        case RADAR_CMD_SLEEP_COMPREHENSIVE:
        case RADAR_CMD_SLEEP_QUERY_COMPREHENSIVE:
            // 睡眠综合状态 (8字节)
            if (data_len < 8) {
                goto error;
            }
            cached_sleep_comprehensive_data.presence = (payload[0] != 0);
            cached_sleep_comprehensive_data.sleep_status = (radar_sleep_status_t)payload[1];
            cached_sleep_comprehensive_data.avg_respiratory = payload[2];
            cached_sleep_comprehensive_data.avg_heart_rate = payload[3];
            cached_sleep_comprehensive_data.turn_over_count = payload[4];
            cached_sleep_comprehensive_data.large_movement_ratio = payload[5];
            cached_sleep_comprehensive_data.small_movement_ratio = payload[6];
            cached_sleep_comprehensive_data.apnea_count = payload[7];
            cached_sleep_comprehensive_data.timestamp = get_utc_timestamp_s();
            cached_sleep_comprehensive_data.system_timestamp = esp_log_timestamp();
            break;
            
        case RADAR_CMD_SLEEP_QUALITY_ANALYSIS:
        case RADAR_CMD_SLEEP_QUERY_STATISTICS:
            // 睡眠质量分析 (12字节)
            if (data_len < 12) {
                goto error;
            }
            cached_sleep_quality_analysis_data.quality_score = payload[0];
            cached_sleep_quality_analysis_data.total_duration = (payload[1] << 8) | payload[2];
            cached_sleep_quality_analysis_data.awake_ratio = payload[3];
            cached_sleep_quality_analysis_data.light_ratio = payload[4];
            cached_sleep_quality_analysis_data.deep_ratio = payload[5];
            cached_sleep_quality_analysis_data.off_bed_duration = payload[6];
            cached_sleep_quality_analysis_data.off_bed_count = payload[7];
            cached_sleep_quality_analysis_data.turn_over_count = payload[8];
            cached_sleep_quality_analysis_data.avg_respiratory = payload[9];
            cached_sleep_quality_analysis_data.avg_heart_rate = payload[10];
            cached_sleep_quality_analysis_data.apnea_count = payload[11];
            cached_sleep_quality_analysis_data.timestamp = get_utc_timestamp_s();
            cached_sleep_quality_analysis_data.system_timestamp = esp_log_timestamp();
            break;
            
        case RADAR_CMD_SLEEP_ABNORMAL:
        case RADAR_CMD_SLEEP_QUERY_ABNORMAL:
            // 睡眠异常
            if (data_len < 1) {
                goto error;
            }
            cached_sleep_abnormal_data.abnormal_type = (radar_sleep_abnormal_t)payload[0];
            cached_sleep_abnormal_data.timestamp = get_utc_timestamp_s();
            cached_sleep_abnormal_data.system_timestamp = esp_log_timestamp();
            break;
            
        case RADAR_CMD_SLEEP_QUALITY_RATING:
        case RADAR_CMD_SLEEP_QUERY_QUALITY_RATING:
            // 睡眠质量评级
            if (data_len < 1) {
                goto error;
            }
            cached_sleep_quality_rating_data.rating = (radar_sleep_quality_rating_t)payload[0];
            cached_sleep_quality_rating_data.timestamp = get_utc_timestamp_s();
            cached_sleep_quality_rating_data.system_timestamp = esp_log_timestamp();
            break;
            
        case RADAR_CMD_SLEEP_STRUGGLE_STATUS:
        case RADAR_CMD_SLEEP_QUERY_STRUGGLE_STATUS:
            // 异常挣扎状态
            if (data_len < 1) {
                goto error;
            }
            cached_sleep_struggle_data.status = (radar_sleep_struggle_status_t)payload[0];
            cached_sleep_struggle_data.timestamp = get_utc_timestamp_s();
            cached_sleep_struggle_data.system_timestamp = esp_log_timestamp();
            break;
            
        case RADAR_CMD_SLEEP_NO_PERSON_STATUS:
        case RADAR_CMD_SLEEP_QUERY_NO_PERSON_STATUS:
            // 无人计时状态
            if (data_len < 1) {
                goto error;
            }
            cached_sleep_no_person_data.status = (radar_sleep_no_person_status_t)payload[0];
            cached_sleep_no_person_data.timestamp = get_utc_timestamp_s();
            cached_sleep_no_person_data.system_timestamp = esp_log_timestamp();
            break;
            
        case RADAR_CMD_SLEEP_STRUGGLE_SWITCH:
        case RADAR_CMD_SLEEP_QUERY_STRUGGLE_SWITCH:
        case RADAR_CMD_SLEEP_NO_PERSON_SWITCH:
        case RADAR_CMD_SLEEP_QUERY_NO_PERSON_SWITCH:
        case RADAR_CMD_SLEEP_NO_PERSON_TIME:
        case RADAR_CMD_SLEEP_QUERY_NO_PERSON_TIME:
        case RADAR_CMD_SLEEP_CUTOFF_TIME:
        case RADAR_CMD_SLEEP_QUERY_CUTOFF_TIME:
        case RADAR_CMD_SLEEP_STRUGGLE_SENSITIVITY:
        case RADAR_CMD_SLEEP_QUERY_STRUGGLE_SENSITIVITY:
            // 设置和查询命令的回复，暂不处理具体数据
            break;
    }
    
    xSemaphoreGive(data_mutex);
    goto callback;
    
error:
    xSemaphoreGive(data_mutex);
    return;
    
callback:
    // 在锁外统一触发回调
    switch (command) {
        case RADAR_CMD_SLEEP_BED_STATUS:
        case RADAR_CMD_SLEEP_QUERY_BED_STATUS:
            if (sleep_bed_callback != NULL) {
                sleep_bed_callback(&cached_sleep_bed_data);
            }
            break;
            
        case RADAR_CMD_SLEEP_STATUS:
        case RADAR_CMD_SLEEP_QUERY_STATUS:
            if (sleep_status_callback != NULL) {
                sleep_status_callback(&cached_sleep_status_data);
            }
            break;
            
        case RADAR_CMD_SLEEP_DEEP_DURATION:
        case RADAR_CMD_SLEEP_QUERY_DEEP_DURATION:
            if (sleep_duration_callback != NULL) {
                sleep_duration_callback(&cached_sleep_duration_data);
            }
            break;
            
        case RADAR_CMD_SLEEP_QUALITY_SCORE:
        case RADAR_CMD_SLEEP_QUERY_QUALITY_SCORE:
            if (sleep_quality_score_callback != NULL) {
                sleep_quality_score_callback(&cached_sleep_quality_score_data);
            }
            break;
            
        case RADAR_CMD_SLEEP_COMPREHENSIVE:
        case RADAR_CMD_SLEEP_QUERY_COMPREHENSIVE:
            if (sleep_comprehensive_callback != NULL) {
                sleep_comprehensive_callback(&cached_sleep_comprehensive_data);
            }
            break;
            
        case RADAR_CMD_SLEEP_QUALITY_ANALYSIS:
        case RADAR_CMD_SLEEP_QUERY_STATISTICS:
            if (sleep_quality_analysis_callback != NULL) {
                sleep_quality_analysis_callback(&cached_sleep_quality_analysis_data);
            }
            break;
            
        case RADAR_CMD_SLEEP_ABNORMAL:
        case RADAR_CMD_SLEEP_QUERY_ABNORMAL:
            if (sleep_abnormal_callback != NULL) {
                sleep_abnormal_callback(&cached_sleep_abnormal_data);
            }
            break;
            
        case RADAR_CMD_SLEEP_QUALITY_RATING:
        case RADAR_CMD_SLEEP_QUERY_QUALITY_RATING:
            if (sleep_quality_rating_callback != NULL) {
                sleep_quality_rating_callback(&cached_sleep_quality_rating_data);
            }
            break;
            
        case RADAR_CMD_SLEEP_STRUGGLE_STATUS:
        case RADAR_CMD_SLEEP_QUERY_STRUGGLE_STATUS:
            if (sleep_struggle_callback != NULL) {
                sleep_struggle_callback(&cached_sleep_struggle_data);
            }
            break;
            
        case RADAR_CMD_SLEEP_NO_PERSON_STATUS:
        case RADAR_CMD_SLEEP_QUERY_NO_PERSON_STATUS:
            if (sleep_no_person_callback != NULL) {
                sleep_no_person_callback(&cached_sleep_no_person_data);
            }
            break;
    }
}

static bool parse_packet(const uint8_t *data, uint16_t length)
{
    if (length < 9) return false;
    
    // 验证帧头和帧尾
    if (data[0] != RADAR_FRAME_HEADER_1 || data[1] != RADAR_FRAME_HEADER_2) {
        return false;
    }
    if (data[length - 2] != RADAR_FRAME_TAIL_1 || data[length - 1] != RADAR_FRAME_TAIL_2) {
        return false;
    }
    
    uint8_t control = data[2];
    uint8_t command = data[3];
    // 数据长度是大端序
    uint16_t data_len = (data[4] << 8) | data[5];
    uint8_t checksum = data[6 + data_len];
    
    // 验证数据长度
    if (data_len > RADAR_MAX_DATA_LENGTH) {
        ESP_LOGW(TAG, "数据长度超出范围: %d", data_len);
        return false;
    }
    
    // 验证校验和：从帧头到数据结束（索引0到6+data_len-1）
    uint8_t calculated_checksum = 0;
    for (uint16_t i = 0; i < 6 + data_len; i++) {
        calculated_checksum += data[i];
    }
    if (calculated_checksum != checksum) {
        ESP_LOGW(TAG, "校验和错误: 计算=0x%02X, 接收=0x%02X", calculated_checksum, checksum);
        return false;
    }
    
    const uint8_t *payload = &data[6];
    
    // 处理不同的控制字和命令字
    if (control == RADAR_CTRL_HUMAN) {
        parse_human_data(command, payload, data_len);
    } else if (control == RADAR_CTRL_RESPIRATORY) {
        parse_respiratory_data(command, payload, data_len);
    } else if (control == RADAR_CTRL_HEART_RATE) {
        parse_heart_rate_data(command, payload, data_len);
    } else if (control == RADAR_CTRL_PRODUCT) {
        parse_product_info(command, payload, data_len);
    } else if (control == RADAR_CTRL_SLEEP) {
        parse_sleep_data(command, payload, data_len);
    }
    
    ESP_LOGD(TAG, "收到数据包: CTRL=0x%02X, CMD=0x%02X, LEN=%d", control, command, data_len);
    return true;
}
