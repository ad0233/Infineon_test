#include "my_nvs.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "NVS_EXAMPLE";

void nvs_test_example(void) {
    ESP_LOGI(TAG, "=== NVS配置模块测试示例 ===");
    
    // 运行所有测试
    my_nvs_run_all_tests();
    
    // 运行性能测试
    my_nvs_performance_test();
    
    // 实际使用示例
    ESP_LOGI(TAG, "=== 实际使用示例 ===");
    
    // 获取配置
    const struct device_config *cfg = my_nvs_get_config();
    if (cfg != NULL) {
        ESP_LOGI(TAG, "当前配置:");
        ESP_LOGI(TAG, "  版本: %u", cfg->version);
        ESP_LOGI(TAG, "  闹钟: %02d:%02d (使能: %s)", 
                 cfg->alarm_hour, cfg->alarm_minute, 
                 cfg->alarm_enable ? "是" : "否");
        ESP_LOGI(TAG, "  音量: %d", cfg->volume);
    }
    
    // 更新配置
    struct device_config new_cfg = *cfg;
    new_cfg.alarm_hour = 9;
    new_cfg.alarm_minute = 15;
    new_cfg.alarm_enable = 1;
    new_cfg.volume = 20;
    
    if (my_nvs_update_config(&new_cfg)) {
        ESP_LOGI(TAG, "配置更新成功");
        
        // 验证更新
        const struct device_config *updated_cfg = my_nvs_get_config();
        ESP_LOGI(TAG, "更新后配置:");
        ESP_LOGI(TAG, "  闹钟: %02d:%02d (使能: %s)", 
                 updated_cfg->alarm_hour, updated_cfg->alarm_minute, 
                 updated_cfg->alarm_enable ? "是" : "否");
        ESP_LOGI(TAG, "  音量: %d", updated_cfg->volume);
    } else {
        ESP_LOGE(TAG, "配置更新失败");
    }
    
    ESP_LOGI(TAG, "=== 测试完成 ===");
}
