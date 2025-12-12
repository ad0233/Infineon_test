#include "my_lidar.h"

#ifdef USE_AIRTOUCH_RADAR
#include "my_lidar_airtouch.h"
#else
#include "my_lidar_r60abd1.h"
#endif

// 标准库
#include <stdlib.h>
#include <string.h>

// 环境库
#include "esp_err.h"
#include "esp_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "my_lidar";

// 内部实现结构
struct my_lidar_impl {
    bool initialized;
    bool started;
    bool have_human;
    // 回调函数和上下文
    my_lidar_human_presence_callback_t human_presence_cb;
    void *human_presence_context;
    my_lidar_human_movement_callback_t human_movement_cb;
    void *human_movement_context;
    my_lidar_respiratory_callback_t respiratory_cb;
    void *respiratory_context;
    my_lidar_heart_rate_callback_t heart_rate_cb;
    void *heart_rate_context;
    // flush 相关
    uint32_t last_flush_time;  // 上次flush执行时间（毫秒）
    // 灵敏度参数
    uint8_t movement_threshold;      // 体动参数阈值（默认20）
    uint32_t heart_rate_timeout_ms;  // 心率数据超时时间（默认5秒=5000ms）
};

// 内部回调包装函数
static void human_presence_callback_wrapper(const radar_human_data_t *data)
{
    // 这个回调由底层实现调用，但我们无法直接获取 handle
    // 需要修改底层实现以支持传递上下文，或者使用全局 handle
    // 暂时保持兼容性，不在这里实现
}

static void human_movement_callback_wrapper(const radar_human_data_t *data)
{
    // 同上
}

static void respiratory_callback_wrapper(const radar_respiratory_data_t *data)
{
    // 同上
}

static void heart_rate_callback_wrapper(const radar_heart_rate_data_t *data)
{
    // 同上
}

// 全局 handle（用于回调，因为底层实现使用全局回调）
static my_lidar_handle_t g_lidar_handle = NULL;

// 回调包装函数（带 handle）
static void human_presence_callback_with_handle(const radar_human_data_t *data)
{
    if (g_lidar_handle != NULL && g_lidar_handle->human_presence_cb != NULL) {
        g_lidar_handle->human_presence_cb(data, g_lidar_handle->human_presence_context, g_lidar_handle);
    }
}

static void human_movement_callback_with_handle(const radar_human_data_t *data)
{
    if (g_lidar_handle != NULL && g_lidar_handle->human_movement_cb != NULL) {
        g_lidar_handle->human_movement_cb(data, g_lidar_handle->human_movement_context, g_lidar_handle);
    }
}

static void respiratory_callback_with_handle(const radar_respiratory_data_t *data)
{
    if (g_lidar_handle != NULL && g_lidar_handle->respiratory_cb != NULL) {
        g_lidar_handle->respiratory_cb(data, g_lidar_handle->respiratory_context, g_lidar_handle);
    }
}

static void heart_rate_callback_with_handle(const radar_heart_rate_data_t *data)
{
    if (g_lidar_handle != NULL && g_lidar_handle->heart_rate_cb != NULL) {
        g_lidar_handle->heart_rate_cb(data, g_lidar_handle->heart_rate_context, g_lidar_handle);
    }
}

// 初始化
int my_lidar_init(my_lidar_handle_t *self_out) {
    if(self_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    auto* self = (struct my_lidar_impl*)malloc(sizeof(struct my_lidar_impl));
    if(self == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    memset(self, 0, sizeof(struct my_lidar_impl));
    
    // 设置默认灵敏度参数
    self->movement_threshold = 20;        // 默认体动参数阈值
    self->heart_rate_timeout_ms = 5000;   // 默认心率数据超时5秒
    
    // 初始化底层雷达
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGI(TAG, "初始化艾睿雷达（BhrDetInfo自动上报模式）");
    airtouch_init();
#else
    ESP_LOGI(TAG, "初始化R60ABD1雷达");
    r60abd1_init();
#endif
    
    self->initialized = true;
    g_lidar_handle = self;
    
    *self_out = self;
    return ESP_OK;
}

// 启动雷达
int my_lidar_start(my_lidar_handle_t self) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!self->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (self->started) {
        return ESP_OK;
    }
    
#ifdef USE_AIRTOUCH_RADAR
    airtouch_start();
#else
    r60abd1_start();
#endif
    
    self->started = true;
    return ESP_OK;
}

// 停止雷达
int my_lidar_stop(my_lidar_handle_t self) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!self->started) {
        return ESP_OK;
    }
    
#ifdef USE_AIRTOUCH_RADAR
    airtouch_stop();
#else
    r60abd1_stop();
#endif
    
    self->started = false;
    return ESP_OK;
}

// 注册回调函数
int my_lidar_reg_cb_human_presence(my_lidar_handle_t self, my_lidar_human_presence_callback_t func, void *context) {
    if (self == NULL || func == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (self->human_presence_cb != NULL) {
        ESP_LOGW(TAG, "Human presence callback already registered");
        return ESP_ERR_INVALID_STATE;
    }
    self->human_presence_cb = func;
    self->human_presence_context = context;
    
#ifndef USE_AIRTOUCH_RADAR
    r60abd1_set_human_presence_callback(human_presence_callback_with_handle);
#endif
    return ESP_OK;
}

int my_lidar_reg_cb_human_movement(my_lidar_handle_t self, my_lidar_human_movement_callback_t func, void *context) {
    if (self == NULL || func == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (self->human_movement_cb != NULL) {
        ESP_LOGW(TAG, "Human movement callback already registered");
        return ESP_ERR_INVALID_STATE;
    }
    self->human_movement_cb = func;
    self->human_movement_context = context;
    
#ifndef USE_AIRTOUCH_RADAR
    r60abd1_set_human_movement_callback(human_movement_callback_with_handle);
#endif
    return ESP_OK;
}

int my_lidar_reg_cb_respiratory(my_lidar_handle_t self, my_lidar_respiratory_callback_t func, void *context) {
    if (self == NULL || func == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (self->respiratory_cb != NULL) {
        ESP_LOGW(TAG, "Respiratory callback already registered");
        return ESP_ERR_INVALID_STATE;
    }
    self->respiratory_cb = func;
    self->respiratory_context = context;
    
#ifndef USE_AIRTOUCH_RADAR
    r60abd1_set_respiratory_callback(respiratory_callback_with_handle);
#endif
    return ESP_OK;
}

int my_lidar_reg_cb_heart_rate(my_lidar_handle_t self, my_lidar_heart_rate_callback_t func, void *context) {
    if (self == NULL || func == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (self->heart_rate_cb != NULL) {
        ESP_LOGW(TAG, "Heart rate callback already registered");
        return ESP_ERR_INVALID_STATE;
    }
    self->heart_rate_cb = func;
    self->heart_rate_context = context;
    
#ifndef USE_AIRTOUCH_RADAR
    r60abd1_set_heart_rate_callback(heart_rate_callback_with_handle);
#endif
    return ESP_OK;
}

// 查询连接状态
bool my_lidar_is_connected(my_lidar_handle_t self) {
    if (self == NULL) {
        return false;
    }
#ifdef USE_AIRTOUCH_RADAR
    return airtouch_is_connected();
#else
    return r60abd1_is_connected();
#endif
}

// 查询是否有人体存在
bool my_lidar_have_human(my_lidar_handle_t self) {
    if (self == NULL) {
        return false;
    }
    return self->have_human;
}

// 数据获取函数
int my_lidar_get_human_data(my_lidar_handle_t self, radar_human_data_t *data) {
    if (self == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGW(TAG, "艾睿雷达暂不支持数据获取接口");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (r60abd1_get_human_data(data)) {
        return ESP_OK;
    }
    return ESP_FAIL;
#endif
}

int my_lidar_get_respiratory_data(my_lidar_handle_t self, radar_respiratory_data_t *data) {
    if (self == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGW(TAG, "艾睿雷达暂不支持数据获取接口");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (r60abd1_get_respiratory_data(data)) {
        return ESP_OK;
    }
    return ESP_FAIL;
#endif
}

int my_lidar_get_heart_rate_data(my_lidar_handle_t self, radar_heart_rate_data_t *data) {
    if (self == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGW(TAG, "艾睿雷达暂不支持数据获取接口");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (r60abd1_get_heart_rate_data(data)) {
        return ESP_OK;
    }
    return ESP_FAIL;
#endif
}

int my_lidar_get_product_info(my_lidar_handle_t self, radar_product_info_t *info) {
    if (self == NULL || info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGW(TAG, "艾睿雷达不支持产品信息查询");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (r60abd1_get_product_info(info)) {
        return ESP_OK;
    }
    return ESP_FAIL;
#endif
}

int my_lidar_get_latest_data(my_lidar_handle_t self, radar_latest_data_t *data) {
    if (self == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
#ifdef USE_AIRTOUCH_RADAR
    ESP_LOGW(TAG, "艾睿雷达暂不支持该接口");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (r60abd1_get_latest_data(data)) {
        return ESP_OK;
    }
    return ESP_FAIL;
#endif
}

int my_lidar_flush(my_lidar_handle_t self, uint32_t interval_ms) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查是否到了执行间隔
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (self->last_flush_time != 0 && 
        (current_time - self->last_flush_time) < interval_ms) {
        // 还没到执行间隔，直接退出
        return ESP_OK;
    }
    self->last_flush_time = current_time;
    
#ifndef USE_AIRTOUCH_RADAR
    // R60ABD1雷达需要 flush
    r60abd1_flush();

    radar_latest_data_t data;
    if (r60abd1_get_latest_data(&data)) {
        uint32_t current_time = esp_log_timestamp();
        uint32_t heart_rate_age = current_time - data.heart_rate_timestamp;
        
        if (data.movement_param > self->movement_threshold || 
            heart_rate_age < self->heart_rate_timeout_ms) {
            self->have_human = true;
        } else if (heart_rate_age > self->heart_rate_timeout_ms) {
            self->have_human = false;
        }
    }
#endif

    
    return ESP_OK;
}

// 灵敏度参数设置
int my_lidar_set_sensitivity(my_lidar_handle_t self, uint8_t movement_threshold, uint32_t heart_rate_timeout_ms) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    self->movement_threshold = movement_threshold;
    self->heart_rate_timeout_ms = heart_rate_timeout_ms;
    return ESP_OK;
}

// ============================================================================
// R60ABD1特有的命令接口（仅当使用R60ABD1雷达时可用）
// ============================================================================

#ifndef USE_AIRTOUCH_RADAR

int my_lidar_send_command(my_lidar_handle_t self, uint8_t control, uint8_t command, const uint8_t *data, uint16_t length) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (r60abd1_send_command(control, command, data, length)) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

int my_lidar_query_product_info(my_lidar_handle_t self) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (r60abd1_query_product_info()) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

int my_lidar_query_human_presence(my_lidar_handle_t self) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (r60abd1_query_human_presence()) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

int my_lidar_set_human_switch(my_lidar_handle_t self, bool enable) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (r60abd1_set_human_switch(enable)) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

int my_lidar_set_respiratory_switch(my_lidar_handle_t self, bool enable) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (r60abd1_set_respiratory_switch(enable)) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

int my_lidar_set_heart_rate_switch(my_lidar_handle_t self, bool enable) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (r60abd1_set_heart_rate_switch(enable)) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

int my_lidar_query_human_switch_status(my_lidar_handle_t self) {
    if (self == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGW(TAG, "该功能需要在R60ABD1雷达实现中补充");
    return ESP_ERR_NOT_SUPPORTED;
}

#endif // USE_AIRTOUCH_RADAR

