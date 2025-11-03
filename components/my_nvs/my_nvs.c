#include "my_nvs.h"


#include <string.h>
#include <stdbool.h>
#include "nvs_flash.h"
#include "nvs.h"

#define NVS_NAMESPACE "devcfg"
#define NVS_KEY "config"

static device_config_t g_device_config;
static bool g_initialized = false;

// 默认配置
static void device_config_set_default(struct device_config *cfg) {
    memset(cfg, 0, sizeof(struct device_config));
    cfg->version = 1;
    cfg->magic = DEVICE_CONFIG_MAGIC;
    cfg->checksum = 0; // 稍后计算
    cfg->alarm_hour = 7;
    cfg->alarm_minute = 0;
    cfg->alarm_enable = 0;
    cfg->volume = 10;
}

// 简单校验和计算
static uint32_t device_config_calc_checksum(const struct device_config *cfg) {
    uint32_t sum = 0;
    const uint8_t *p = (const uint8_t *)cfg;
    // 跳过checksum字段本身
    for (size_t i = 0; i < offsetof(struct device_config, checksum); ++i) {
        sum += p[i];
    }
    for (size_t i = offsetof(struct device_config, checksum) + sizeof(uint32_t); i < sizeof(struct device_config); ++i) {
        sum += p[i];
    }
    return sum;
}

// 校验配置有效性
static bool device_config_is_valid(const struct device_config *cfg) {
    if (cfg->magic != DEVICE_CONFIG_MAGIC) return false;
    if (cfg->version == 0) return false;
    uint32_t sum = device_config_calc_checksum(cfg);
    if (cfg->checksum != sum) return false;
    return true;
}

// 从NVS读取配置
static bool device_config_load_from_nvs(device_config_t *out) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    size_t required_size = DEVICE_CONFIG_SIZE;
    err = nvs_get_blob(handle, NVS_KEY, out->raw_data, &required_size);
    nvs_close(handle);

    if (err != ESP_OK || required_size != DEVICE_CONFIG_SIZE) return false;
    if (!device_config_is_valid(&out->config)) return false;
    return true;
}

// 写入配置到NVS
static bool device_config_save_to_nvs(const device_config_t *in) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_set_blob(handle, NVS_KEY, in->raw_data, DEVICE_CONFIG_SIZE);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

// 延迟初始化配置
static bool my_nvs_ensure_initialized(void) {
    if (g_initialized) return true;
    
    // 确保NVS已初始化
    nvs_flash_init();

    bool loaded = device_config_load_from_nvs(&g_device_config);
    if (!loaded) {
        // 设置默认值
        device_config_set_default(&g_device_config.config);
        g_device_config.config.checksum = device_config_calc_checksum(&g_device_config.config);
        device_config_save_to_nvs(&g_device_config);
    }
    
    g_initialized = true;
    return true;
}

// 对外接口：获取配置指针（只读）
const struct device_config *my_nvs_get_config(void) {
    if (!my_nvs_ensure_initialized()) return NULL;
    return &g_device_config.config;
}

// 对外接口：写入配置（会自动校验和并保存到NVS）
bool my_nvs_update_config(const struct device_config *new_cfg) {
    if (!my_nvs_ensure_initialized()) return false;
    
    // 拷贝新配置
    memcpy(&g_device_config.config, new_cfg, sizeof(struct device_config));
    g_device_config.config.checksum = device_config_calc_checksum(&g_device_config.config);
    return device_config_save_to_nvs(&g_device_config);
}

