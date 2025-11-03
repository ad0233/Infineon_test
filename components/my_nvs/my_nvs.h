#pragma once

#include <stdint.h>
#include <stdbool.h>

// 固定1024字节的设备配置结构体
#define DEVICE_CONFIG_SIZE 1024
#define DEVICE_CONFIG_MAGIC 0xA5A5A5A5

struct device_config {
    uint32_t version;           // 配置版本号
    uint32_t magic;            // 魔数，用于验证配置有效性
    uint32_t checksum;         // 校验和
    // 其他字段可以逐步添加到这里
    uint8_t alarm_hour;
    uint8_t alarm_minute;
    uint8_t alarm_enable;

    uint8_t volume;
    uint8_t wake_mode;
    uint8_t light_duty;
    
    // WiFi配置
    char wifi_ssid[32];        // WiFi SSID (有内容则自动连接)
    char wifi_password[64];    // WiFi密码
    uint8_t wifi_enable;       // WiFi功能使能开关
} __attribute__((packed));

// 使用union确保结构体大小恒定
typedef union {
    struct device_config config;
    uint8_t raw_data[DEVICE_CONFIG_SIZE];
} device_config_t;

// 验证结构体大小
#ifdef __cplusplus
static_assert(sizeof(device_config_t) == DEVICE_CONFIG_SIZE, "device_config_t size must be exactly 1024 bytes");
static_assert(sizeof(struct device_config) <= DEVICE_CONFIG_SIZE, "device_config struct size must not exceed 1024 bytes");
#else
_Static_assert(sizeof(device_config_t) == DEVICE_CONFIG_SIZE, "device_config_t size must be exactly 1024 bytes");
_Static_assert(sizeof(struct device_config) <= DEVICE_CONFIG_SIZE, "device_config struct size must not exceed 1024 bytes");
#endif

// 对外接口函数声明
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取设备配置（只读）
 * @return 配置结构体指针，失败时返回NULL
 */
const struct device_config *my_nvs_get_config(void);

/**
 * @brief 更新设备配置
 * @param new_cfg 新的配置结构体指针
 * @return true表示成功，false表示失败
 */
bool my_nvs_update_config(const struct device_config *new_cfg);

/**
 * @brief 运行所有NVS测试
 */
void my_nvs_run_all_tests(void);

/**
 * @brief 运行性能测试
 */
void my_nvs_performance_test(void);

#ifdef __cplusplus
}
#endif