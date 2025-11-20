#include "my_nvs.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define NVS_NAMESPACE "devcfg"
#define NVS_KEY "config"
#define NVS_IOT_KEY "iot_config"
#define NVS_PRIVATE_KEY_KEY "priv_key"

#define IOT_CFG_PARTITION_NAME "iot_config"
#define IOT_CFG_NAMESPACE "iot_config"
#define IOT_CFG_JSON_KEY "json"

static device_config_t g_device_config;
static bool g_initialized = false;

static iot_config_t g_iot_config;
static bool g_iot_initialized = false;

static private_key_config_t g_private_key_config;
static bool g_private_key_initialized = false;

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

// ========== IoT配置相关函数 ==========

// 默认IoT配置
static void iot_config_set_default(struct iot_config *cfg) {
    memset(cfg, 0, sizeof(struct iot_config));
    cfg->version = 1;
    cfg->magic = IOT_CONFIG_MAGIC;
    cfg->checksum = 0;
    cfg->iot_port = 8883;
    cfg->enable = 0;
}

// 计算IoT配置校验和
static uint32_t iot_config_calc_checksum(const struct iot_config *cfg) {
    uint32_t sum = 0;
    const uint8_t *p = (const uint8_t *)cfg;
    for (size_t i = 0; i < offsetof(struct iot_config, checksum); ++i) {
        sum += p[i];
    }
    for (size_t i = offsetof(struct iot_config, checksum) + sizeof(uint32_t); i < sizeof(struct iot_config); ++i) {
        sum += p[i];
    }
    return sum;
}

// 校验IoT配置有效性
static bool iot_config_is_valid(const struct iot_config *cfg) {
    if (cfg->magic != IOT_CONFIG_MAGIC) return false;
    if (cfg->version == 0) return false;
    uint32_t sum = iot_config_calc_checksum(cfg);
    if (cfg->checksum != sum) return false;
    return true;
}

// 从NVS读取IoT配置
static bool iot_config_load_from_nvs(iot_config_t *out) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    size_t required_size = IOT_CONFIG_SIZE;
    err = nvs_get_blob(handle, NVS_IOT_KEY, out->raw_data, &required_size);
    nvs_close(handle);

    if (err != ESP_OK || required_size != IOT_CONFIG_SIZE) return false;
    if (!iot_config_is_valid(&out->config)) return false;
    return true;
}

// 写入IoT配置到NVS
static bool iot_config_save_to_nvs(const iot_config_t *in) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_set_blob(handle, NVS_IOT_KEY, in->raw_data, IOT_CONFIG_SIZE);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

// 延迟初始化IoT配置
static bool my_nvs_ensure_iot_initialized(void) {
    if (g_iot_initialized) return true;
    
    nvs_flash_init();

    bool loaded = iot_config_load_from_nvs(&g_iot_config);
    if (!loaded) {
        iot_config_set_default(&g_iot_config.config);
        g_iot_config.config.checksum = iot_config_calc_checksum(&g_iot_config.config);
        iot_config_save_to_nvs(&g_iot_config);
    }
    
    g_iot_initialized = true;
    return true;
}

// 对外接口：获取IoT配置指针（只读）
const struct iot_config *my_nvs_get_iot_config(void) {
    if (!my_nvs_ensure_iot_initialized()) return NULL;
    return &g_iot_config.config;
}

// 对外接口：写入IoT配置
bool my_nvs_update_iot_config(const struct iot_config *new_cfg) {
    if (!my_nvs_ensure_iot_initialized()) return false;
    
    memcpy(&g_iot_config.config, new_cfg, sizeof(struct iot_config));
    g_iot_config.config.checksum = iot_config_calc_checksum(&g_iot_config.config);
    return iot_config_save_to_nvs(&g_iot_config);
}

// ========== 私钥配置相关函数 ==========

// 默认私钥配置
static void private_key_config_set_default(struct private_key_config *cfg) {
    memset(cfg, 0, sizeof(struct private_key_config));
    cfg->version = 1;
    cfg->magic = PRIVATE_KEY_CONFIG_MAGIC;
    cfg->checksum = 0;
    cfg->enable = 0;
}

// 计算私钥配置校验和
static uint32_t private_key_config_calc_checksum(const struct private_key_config *cfg) {
    uint32_t sum = 0;
    const uint8_t *p = (const uint8_t *)cfg;
    for (size_t i = 0; i < offsetof(struct private_key_config, checksum); ++i) {
        sum += p[i];
    }
    for (size_t i = offsetof(struct private_key_config, checksum) + sizeof(uint32_t); i < sizeof(struct private_key_config); ++i) {
        sum += p[i];
    }
    return sum;
}

// 校验私钥配置有效性
static bool private_key_config_is_valid(const struct private_key_config *cfg) {
    if (cfg->magic != PRIVATE_KEY_CONFIG_MAGIC) return false;
    if (cfg->version == 0) return false;
    uint32_t sum = private_key_config_calc_checksum(cfg);
    if (cfg->checksum != sum) return false;
    return true;
}

// 从NVS读取私钥配置
static bool private_key_config_load_from_nvs(private_key_config_t *out) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    size_t required_size = PRIVATE_KEY_CONFIG_SIZE;
    err = nvs_get_blob(handle, NVS_PRIVATE_KEY_KEY, out->raw_data, &required_size);
    nvs_close(handle);

    if (err != ESP_OK || required_size != PRIVATE_KEY_CONFIG_SIZE) return false;
    if (!private_key_config_is_valid(&out->config)) return false;
    return true;
}

// 写入私钥配置到NVS
static bool private_key_config_save_to_nvs(const private_key_config_t *in) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_set_blob(handle, NVS_PRIVATE_KEY_KEY, in->raw_data, PRIVATE_KEY_CONFIG_SIZE);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

// 延迟初始化私钥配置
static bool my_nvs_ensure_private_key_initialized(void) {
    if (g_private_key_initialized) return true;
    
    nvs_flash_init();

    bool loaded = private_key_config_load_from_nvs(&g_private_key_config);
    if (!loaded) {
        private_key_config_set_default(&g_private_key_config.config);
        g_private_key_config.config.checksum = private_key_config_calc_checksum(&g_private_key_config.config);
        private_key_config_save_to_nvs(&g_private_key_config);
    }
    
    g_private_key_initialized = true;
    return true;
}

// 对外接口：获取私钥配置指针（只读）
const struct private_key_config *my_nvs_get_private_key_config(void) {
    if (!my_nvs_ensure_private_key_initialized()) return NULL;
    return &g_private_key_config.config;
}

// 对外接口：写入私钥配置
bool my_nvs_update_private_key_config(const struct private_key_config *new_cfg) {
    if (!my_nvs_ensure_private_key_initialized()) return false;
    
    memcpy(&g_private_key_config.config, new_cfg, sizeof(struct private_key_config));
    g_private_key_config.config.checksum = private_key_config_calc_checksum(&g_private_key_config.config);
    return private_key_config_save_to_nvs(&g_private_key_config);
}

bool my_nvs_read_iot_config_json(char *out_buffer, size_t buffer_size) {
    if (!out_buffer || buffer_size == 0) {
        return false;
    }

    esp_err_t err = nvs_flash_init_partition(IOT_CFG_PARTITION_NAME);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase_partition(IOT_CFG_PARTITION_NAME);
        err = nvs_flash_init_partition(IOT_CFG_PARTITION_NAME);
    }
    if (err != ESP_OK) return false;

    nvs_handle_t handle;
    err = nvs_open_from_partition(IOT_CFG_PARTITION_NAME, IOT_CFG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t required = 0;
    err = nvs_get_str(handle, IOT_CFG_JSON_KEY, NULL, &required);
    if (err == ESP_OK) {
        if (required > buffer_size) {
            err = ESP_ERR_NVS_INVALID_LENGTH;
        } else {
            err = nvs_get_str(handle, IOT_CFG_JSON_KEY, out_buffer, &required);
        }
    }

    nvs_close(handle);
    return err == ESP_OK;
}

#include "cJSON.h"

static const char *s_keys_mqtt_uri[] = {
    "mqtt_uri",
    "broker_uri",
    "endpoint",
    "url",
};

// ===== 惰性加载缓存 =====
static struct iot_config_view s_iot_view;
static bool s_iot_view_loaded = false;
static char s_buf_mqtt_uri[192];
static char s_buf_protocol[8];
static char s_buf_endpoint[128];
static char s_buf_thing_name[64];

static bool load_iot_view_once(void) {
    if (s_iot_view_loaded) return true;

    char json_buf[2048];
    if (!my_nvs_read_iot_config_json(json_buf, sizeof(json_buf))) {
        return false;
    }

    cJSON *root = cJSON_Parse(json_buf);
    if (!root) return false;

    // protocol
    const cJSON *protocol = cJSON_GetObjectItemCaseSensitive(root, "protocol");
    const char *proto = (cJSON_IsString(protocol) && protocol->valuestring) ? protocol->valuestring : "mqtts";
    snprintf(s_buf_protocol, sizeof(s_buf_protocol), "%s", proto);

    // endpoint
    const cJSON *endpoint = cJSON_GetObjectItemCaseSensitive(root, "iot_endpoint");
    const char *host = (cJSON_IsString(endpoint) && endpoint->valuestring) ? endpoint->valuestring : "";
    snprintf(s_buf_endpoint, sizeof(s_buf_endpoint), "%s", host);

    // port
    const cJSON *port = cJSON_GetObjectItemCaseSensitive(root, "iot_port");
    int port_val = cJSON_IsNumber(port) ? port->valueint : 8883;

    // thing_name
    const cJSON *thing = cJSON_GetObjectItemCaseSensitive(root, "thing_name");
    const char *thing_str = (cJSON_IsString(thing) && thing->valuestring) ? thing->valuestring : "";
    snprintf(s_buf_thing_name, sizeof(s_buf_thing_name), "%s", thing_str);

    // 优先直接 URI
    const cJSON *direct_uri = NULL;
    for (size_t i = 0; i < sizeof(s_keys_mqtt_uri)/sizeof(s_keys_mqtt_uri[0]); ++i) {
        const cJSON *it = cJSON_GetObjectItemCaseSensitive(root, s_keys_mqtt_uri[i]);
        if (cJSON_IsString(it) && it->valuestring && it->valuestring[0] != '\0') {
            direct_uri = it;
            break;
        }
    }

    if (direct_uri) {
        snprintf(s_buf_mqtt_uri, sizeof(s_buf_mqtt_uri), "%s", direct_uri->valuestring);
    } else {
        snprintf(s_buf_mqtt_uri, sizeof(s_buf_mqtt_uri), "%s://%s:%d", s_buf_protocol, s_buf_endpoint, port_val);
    }

    s_iot_view.mqtt_uri     = s_buf_mqtt_uri;
    s_iot_view.protocol     = s_buf_protocol;
    s_iot_view.iot_endpoint = s_buf_endpoint;
    s_iot_view.thing_name   = s_buf_thing_name;
    s_iot_view.iot_port     = port_val;

    cJSON_Delete(root);
    s_iot_view_loaded = true;
    return true;
}

const struct iot_config_view *my_nvs_get_iot_config_view(void) {
    if (!load_iot_view_once()) return NULL;
    return &s_iot_view;
}

bool my_nvs_read_iot_config_key(iot_config_key_type_t key_type, char *out_buffer, size_t buffer_size) {
    if (!out_buffer || buffer_size == 0 || key_type >= IOT_CONFIG_KEY_MAX) {
        return false;
    }

    // 将枚举转换为字符串键名
    const char *key_names[] = {
        "ca_cert",
        "device_cert",
        "private_key"
    };
    const char *key_name = key_names[key_type];

    esp_err_t err = nvs_flash_init_partition(IOT_CFG_PARTITION_NAME);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase_partition(IOT_CFG_PARTITION_NAME);
        err = nvs_flash_init_partition(IOT_CFG_PARTITION_NAME);
    }
    if (err != ESP_OK) return false;

    nvs_handle_t handle;
    err = nvs_open_from_partition(IOT_CFG_PARTITION_NAME, IOT_CFG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t required = 0;
    err = nvs_get_str(handle, key_name, NULL, &required);
    if (err == ESP_OK) {
        if (required > buffer_size) {
            err = ESP_ERR_NVS_INVALID_LENGTH;
        } else {
            err = nvs_get_str(handle, key_name, out_buffer, &required);
        }
    }

    nvs_close(handle);
    return err == ESP_OK;
}

void my_nvs_print_iot_config_keys(void) {
    const iot_config_key_type_t key_types[] = {
        IOT_CONFIG_KEY_CA_CERT,
        IOT_CONFIG_KEY_DEVICE_CERT,
        IOT_CONFIG_KEY_PRIVATE_KEY
    };
    const char *key_labels[] = {"CA Certificate", "Device Certificate", "Private Key"};
    const char *key_names[] = {"ca_cert", "device_cert", "private_key"};
    
    ESP_LOGI("NVS", "========== IoT Config Keys ==========");
    
    for (int i = 0; i < 3; i++) {
        char buffer[4096] = {0};
        if (my_nvs_read_iot_config_key(key_types[i], buffer, sizeof(buffer))) {
            size_t len = strlen(buffer);
            ESP_LOGI("NVS", "%s (%s):", key_labels[i], key_names[i]);
            if (len > 200) {
                // 如果内容太长，只打印前200个字符和后50个字符
                char preview[300];
                snprintf(preview, sizeof(preview), "%.200s...%s", buffer, buffer + len - 50);
                ESP_LOGI("NVS", "  Length: %zu bytes", len);
                ESP_LOGI("NVS", "  Preview: %s", preview);
            } else {
                ESP_LOGI("NVS", "  %s", buffer);
            }
        } else {
            ESP_LOGW("NVS", "%s (%s): Not found", key_labels[i], key_names[i]);
        }
    }
    
    ESP_LOGI("NVS", "=====================================");
}

