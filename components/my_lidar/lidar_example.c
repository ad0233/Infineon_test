/*
 * R60ABD1雷达使用示例
 * 展示如何在主程序中使用R60ABD1毫米波雷达
 */

#include "my_lidar.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "radar_example";

// 雷达数据回调函数
static void radar_data_callback(const radar_packet_t *packet)
{
    ESP_LOGI(TAG, "收到雷达数据包:");
    ESP_LOGI(TAG, "  控制字: 0x%02X", packet->control);
    ESP_LOGI(TAG, "  命令字: 0x%02X", packet->command);
    ESP_LOGI(TAG, "  数据长度: %d", packet->length);
    
    if (packet->length > 0) {
        ESP_LOGI(TAG, "  数据内容: ");
        for (int i = 0; i < packet->length && i < 16; i++) {
            printf("%02X ", packet->data[i]);
        }
        printf("\n");
    }
}

// 人体数据回调函数
static void radar_human_callback(const radar_human_data_t *data)
{
    ESP_LOGI(TAG, "人体检测数据更新:");
    ESP_LOGI(TAG, "  存在: %s", data->presence ? "有人" : "无人");
    ESP_LOGI(TAG, "  运动状态: %d", data->motion_status);
    ESP_LOGI(TAG, "  体动参数: %d", data->movement_param);
    ESP_LOGI(TAG, "  距离: %d cm", data->distance);
    ESP_LOGI(TAG, "  位置: X=%d, Y=%d, Z=%d cm", 
             data->position_x, data->position_y, data->position_z);
}

// 雷达状态监控任务
static void radar_monitor_task(void *arg)
{
    while (1) {
        if (my_radar_is_connected()) {
            ESP_LOGI(TAG, "雷达已连接");
            
            // 查询人体数据
            radar_human_data_t human_data;
            if (my_radar_get_human_data(&human_data)) {
                ESP_LOGI(TAG, "当前人体状态: %s, 距离: %d cm", 
                         human_data.presence ? "有人" : "无人", human_data.distance);
            }
        } else {
            ESP_LOGW(TAG, "雷达未连接");
        }
        
        vTaskDelay(5000 / portTICK_PERIOD_MS); // 每5秒检查一次
    }
}

// 初始化雷达示例
void radar_example_init(void)
{
    ESP_LOGI(TAG, "初始化R60ABD1雷达示例...");
    
    // 初始化雷达
    my_radar_init();
    
    // 设置回调函数
    my_radar_set_callback(radar_data_callback);
    my_radar_set_human_callback(radar_human_callback);
    
    // 启动雷达接收
    my_radar_start();
    
    // 等待连接建立
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // 查询产品信息
    ESP_LOGI(TAG, "查询产品信息...");
    my_radar_query_product_info();
    
    // 启用人体检测
    ESP_LOGI(TAG, "启用人体检测...");
    my_radar_set_human_switch(true);
    
    // 创建监控任务
    xTaskCreate(radar_monitor_task, "radar_monitor", 2048, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "R60ABD1雷达示例初始化完成");
}

// 停止雷达示例
void radar_example_stop(void)
{
    ESP_LOGI(TAG, "停止R60ABD1雷达示例...");
    
    // 禁用人体检测
    my_radar_set_human_switch(false);
    
    // 停止雷达接收
    my_radar_stop();
    
    ESP_LOGI(TAG, "R60ABD1雷达示例已停止");
}
