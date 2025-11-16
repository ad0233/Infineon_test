#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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

// AWS IoT配置结构体 (4096字节)
#define IOT_CONFIG_SIZE 4096
#define IOT_CONFIG_MAGIC 0xB6B6B6B6

struct iot_config {
    uint32_t version;           // 配置版本号
    uint32_t magic;            // 魔数
    uint32_t checksum;         // 校验和
    uint16_t iot_port;         // IoT端口
    uint8_t enable;            // IoT功能使能
    uint8_t reserved;
    
    char iot_endpoint[128];    // IoT端点
    char thing_name[64];       // 设备名称
    char certificate_id[128];  // 证书ID
    char root_ca[1536];        // 根证书
    char certificate_pem[2048]; // 设备证书
} __attribute__((packed));

// 私钥配置结构体 (2048字节，单独存储)
#define PRIVATE_KEY_CONFIG_SIZE 2048
#define PRIVATE_KEY_CONFIG_MAGIC 0xC7C7C7C7

struct private_key_config {
    uint32_t version;           // 配置版本号
    uint32_t magic;            // 魔数
    uint32_t checksum;         // 校验和
    uint8_t enable;            // 私钥使能
    uint8_t reserved[3];
    char private_key_pem[2032]; // 私钥
} __attribute__((packed));

// 使用union确保结构体大小恒定
typedef union {
    struct device_config config;
    uint8_t raw_data[DEVICE_CONFIG_SIZE];
} device_config_t;

typedef union {
    struct iot_config config;
    uint8_t raw_data[IOT_CONFIG_SIZE];
} iot_config_t;

typedef union {
    struct private_key_config config;
    uint8_t raw_data[PRIVATE_KEY_CONFIG_SIZE];
} private_key_config_t;

// 验证结构体大小
#ifdef __cplusplus
static_assert(sizeof(device_config_t) == DEVICE_CONFIG_SIZE, "device_config_t size must be exactly 1024 bytes");
static_assert(sizeof(struct device_config) <= DEVICE_CONFIG_SIZE, "device_config struct size must not exceed 1024 bytes");
static_assert(sizeof(iot_config_t) == IOT_CONFIG_SIZE, "iot_config_t size must be exactly 4096 bytes");
static_assert(sizeof(struct iot_config) <= IOT_CONFIG_SIZE, "iot_config struct size must not exceed 4096 bytes");
static_assert(sizeof(private_key_config_t) == PRIVATE_KEY_CONFIG_SIZE, "private_key_config_t size must be exactly 2048 bytes");
static_assert(sizeof(struct private_key_config) <= PRIVATE_KEY_CONFIG_SIZE, "private_key_config struct size must not exceed 2048 bytes");
#else
_Static_assert(sizeof(device_config_t) == DEVICE_CONFIG_SIZE, "device_config_t size must be exactly 1024 bytes");
_Static_assert(sizeof(struct device_config) <= DEVICE_CONFIG_SIZE, "device_config struct size must not exceed 1024 bytes");
_Static_assert(sizeof(iot_config_t) == IOT_CONFIG_SIZE, "iot_config_t size must be exactly 4096 bytes");
_Static_assert(sizeof(struct iot_config) <= IOT_CONFIG_SIZE, "iot_config struct size must not exceed 4096 bytes");
_Static_assert(sizeof(private_key_config_t) == PRIVATE_KEY_CONFIG_SIZE, "private_key_config_t size must be exactly 2048 bytes");
_Static_assert(sizeof(struct private_key_config) <= PRIVATE_KEY_CONFIG_SIZE, "private_key_config struct size must not exceed 2048 bytes");
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

/**
 * @brief 获取IoT配置（只读）
 * @return IoT配置结构体指针，失败时返回NULL
 */
const struct iot_config *my_nvs_get_iot_config(void);

/**
 * @brief 更新IoT配置
 * @param new_cfg 新的IoT配置结构体指针
 * @return true表示成功，false表示失败
 */
bool my_nvs_update_iot_config(const struct iot_config *new_cfg);

/**
 * @brief 获取私钥配置（只读）
 * @return 私钥配置结构体指针，失败时返回NULL
 */
const struct private_key_config *my_nvs_get_private_key_config(void);

/**
 * @brief 更新私钥配置
 * @param new_cfg 新的私钥配置结构体指针
 * @return true表示成功，false表示失败
 */
bool my_nvs_update_private_key_config(const struct private_key_config *new_cfg);

/**
 * @brief 读取 iot_config 分区中的 JSON
 * @param[out] out_buffer 存放 JSON 文本的缓冲区
 * @param[in]  buffer_size 缓冲区大小
 * @return true 表示读取成功且内容已写入缓冲区
 */
bool my_nvs_read_iot_config_json(char *out_buffer, size_t buffer_size);

// ===== 惰性加载的 IoT 视图结构（仅指针/只读） =====
struct iot_config_view {
    const char *mqtt_uri;       // 例如 mqtts://host:8883
    const char *protocol;       // 例如 mqtt / mqtts
    const char *iot_endpoint;   // 主机名
    const char *thing_name;     // 设备名
    int         iot_port;       // 端口
    // topics（如不存在则为 NULL）
    const char *topic_request;
    const char *topic_response;
    const char *topic_command;
    const char *topic_shadow_update;
    const char *topic_shadow_update_delta;
    const char *topic_shadow_update_accepted;
    const char *topic_shadow_update_rejected;
    const char *topic_shadow_get;
    const char *topic_shadow_get_accepted;
    const char *topic_shadow_get_rejected;
};

/**
 * @brief 获取 IoT 配置视图（惰性解析+缓存，返回只读指针）
 * @return 成功返回只读视图指针；失败返回 NULL
 */
const struct iot_config_view *my_nvs_get_iot_config_view(void);

#ifdef __cplusplus
}
#endif