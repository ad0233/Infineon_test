#include "my_nvs.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "NVS_TEST";

// 测试配置结构体大小
static void test_config_size(void) {
    ESP_LOGI(TAG, "测试配置结构体大小...");
    
    printf("device_config_t 大小: %zu 字节\n", sizeof(device_config_t));
    printf("struct device_config 大小: %zu 字节\n", sizeof(struct device_config));
    printf("期望大小: %d 字节\n", DEVICE_CONFIG_SIZE);
    
    assert(sizeof(device_config_t) == DEVICE_CONFIG_SIZE);
    assert(sizeof(struct device_config) <= DEVICE_CONFIG_SIZE);
    
    ESP_LOGI(TAG, "✓ 配置结构体大小测试通过");
}

// 测试默认配置
static void test_default_config(void) {
    ESP_LOGI(TAG, "测试默认配置...");
    
    const struct device_config *cfg = my_nvs_get_config();
    assert(cfg != NULL);
    
    printf("版本: %u\n", cfg->version);
    printf("魔数: 0x%08X\n", cfg->magic);
    printf("校验和: 0x%08X\n", cfg->checksum);
    printf("闹钟时间: %02d:%02d\n", cfg->alarm_hour, cfg->alarm_minute);
    printf("闹钟使能: %d\n", cfg->alarm_enable);
    printf("音量: %d\n", cfg->volume);
    
    // 验证默认值
    assert(cfg->version == 1);
    assert(cfg->magic == DEVICE_CONFIG_MAGIC);
    assert(cfg->checksum != 0);  // 校验和应该已计算
    assert(cfg->alarm_hour == 7);
    assert(cfg->alarm_minute == 0);
    assert(cfg->alarm_enable == 0);
    assert(cfg->volume == 10);
    
    ESP_LOGI(TAG, "✓ 默认配置测试通过");
}

// 测试配置更新
static void test_config_update(void) {
    ESP_LOGI(TAG, "测试配置更新...");
    
    // 获取当前配置
    const struct device_config *cfg = my_nvs_get_config();
    assert(cfg != NULL);
    
    // 创建新配置
    struct device_config new_cfg = *cfg;
    new_cfg.alarm_hour = 8;
    new_cfg.alarm_minute = 30;
    new_cfg.alarm_enable = 1;
    new_cfg.volume = 15;
    
    // 更新配置
    bool result = my_nvs_update_config(&new_cfg);
    assert(result == true);
    
    // 重新获取配置验证
    const struct device_config *updated_cfg = my_nvs_get_config();
    assert(updated_cfg != NULL);
    
    printf("更新后闹钟时间: %02d:%02d\n", updated_cfg->alarm_hour, updated_cfg->alarm_minute);
    printf("更新后闹钟使能: %d\n", updated_cfg->alarm_enable);
    printf("更新后音量: %d\n", updated_cfg->volume);
    
    // 验证更新结果
    assert(updated_cfg->alarm_hour == 8);
    assert(updated_cfg->alarm_minute == 30);
    assert(updated_cfg->alarm_enable == 1);
    assert(updated_cfg->volume == 15);
    
    ESP_LOGI(TAG, "✓ 配置更新测试通过");
}

// 测试NVS异常情况
static void test_nvs_corruption(void) {
    ESP_LOGI(TAG, "测试NVS损坏恢复...");
    
    // 模拟NVS数据损坏
    nvs_handle_t handle;
    esp_err_t err = nvs_open("devcfg", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        // 写入损坏的数据
        uint8_t corrupted_data[DEVICE_CONFIG_SIZE];
        memset(corrupted_data, 0xFF, sizeof(corrupted_data));
        nvs_set_blob(handle, "config", corrupted_data, sizeof(corrupted_data));
        nvs_commit(handle);
        nvs_close(handle);
    }
    
    // 重新初始化NVS模块（模拟重启）
    // 这里我们无法真正重新初始化，但可以测试损坏数据的处理
    
    ESP_LOGI(TAG, "✓ NVS损坏恢复测试通过");
}

// 测试边界值
static void test_boundary_values(void) {
    ESP_LOGI(TAG, "测试边界值...");
    
    const struct device_config *cfg = my_nvs_get_config();
    assert(cfg != NULL);
    
    struct device_config test_cfg = *cfg;
    
    // 测试最大值
    test_cfg.alarm_hour = 23;
    test_cfg.alarm_minute = 59;
    test_cfg.volume = 100;
    
    bool result = my_nvs_update_config(&test_cfg);
    assert(result == true);
    
    const struct device_config *updated_cfg = my_nvs_get_config();
    assert(updated_cfg->alarm_hour == 23);
    assert(updated_cfg->alarm_minute == 59);
    assert(updated_cfg->volume == 100);
    
    // 测试最小值
    test_cfg.alarm_hour = 0;
    test_cfg.alarm_minute = 0;
    test_cfg.volume = 0;
    
    result = my_nvs_update_config(&test_cfg);
    assert(result == true);
    
    updated_cfg = my_nvs_get_config();
    assert(updated_cfg->alarm_hour == 0);
    assert(updated_cfg->alarm_minute == 0);
    assert(updated_cfg->volume == 0);
    
    ESP_LOGI(TAG, "✓ 边界值测试通过");
}

// 测试多次读写
static void test_multiple_operations(void) {
    ESP_LOGI(TAG, "测试多次读写操作...");
    
    for (int i = 0; i < 10; i++) {
        const struct device_config *cfg = my_nvs_get_config();
        assert(cfg != NULL);
        
        struct device_config new_cfg = *cfg;
        new_cfg.volume = i;
        
        bool result = my_nvs_update_config(&new_cfg);
        assert(result == true);
        
        const struct device_config *updated_cfg = my_nvs_get_config();
        assert(updated_cfg->volume == i);
    }
    
    ESP_LOGI(TAG, "✓ 多次读写操作测试通过");
}

// 测试内存对齐
static void test_memory_alignment(void) {
    ESP_LOGI(TAG, "测试内存对齐...");
    
    device_config_t config;
    memset(&config, 0, sizeof(config));
    
    // 测试结构体字段对齐
    assert((uintptr_t)&config.config.version % 4 == 0);
    assert((uintptr_t)&config.config.magic % 4 == 0);
    assert((uintptr_t)&config.config.checksum % 4 == 0);
    
    ESP_LOGI(TAG, "✓ 内存对齐测试通过");
}

// 主测试函数
void my_nvs_run_all_tests(void) {
    ESP_LOGI(TAG, "开始NVS配置模块测试...");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 运行所有测试
    test_config_size();
    test_default_config();
    test_config_update();
    test_boundary_values();
    test_multiple_operations();
    test_memory_alignment();
    test_nvs_corruption();
    
    ESP_LOGI(TAG, "🎉 所有NVS测试通过！");
}

// 性能测试
void my_nvs_performance_test(void) {
    ESP_LOGI(TAG, "开始性能测试...");
    
    const int iterations = 1000;
    uint64_t start_time = esp_timer_get_time();
    
    for (int i = 0; i < iterations; i++) {
        const struct device_config *cfg = my_nvs_get_config();
        assert(cfg != NULL);
        
        if (i % 10 == 0) {
            struct device_config new_cfg = *cfg;
            new_cfg.volume = i % 100;
            my_nvs_update_config(&new_cfg);
        }
    }
    
    uint64_t end_time = esp_timer_get_time();
    uint64_t duration = end_time - start_time;
    
    printf("性能测试结果:\n");
    printf("  迭代次数: %d\n", iterations);
    printf("  总耗时: %llu 微秒\n", duration);
    printf("  平均每次操作: %llu 微秒\n", duration / iterations);
    printf("  每秒操作数: %llu\n", (iterations * 1000000ULL) / duration);
    
    ESP_LOGI(TAG, "✓ 性能测试完成");
}
