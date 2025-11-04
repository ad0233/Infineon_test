#include "pwm_control.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

static const char *TAG = "PWM_CONTROL";

// PWM控制相关变量
static TaskHandle_t pwm_task_handle = NULL;
static pwm_control_t current_pwm = {0};
static bool pwm_initialized = false;

// 任务通知位定义
#define PWM_NOTIFY_BIT_SET     (1UL << 0)
#define PWM_NOTIFY_BIT_STOP    (1UL << 1)

// LEDC配置 - 使用不同的通道和定时器避免与LCD背光冲突
#define LEDC_TIMER              LEDC_TIMER_1
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_1
#define LEDC_DUTY_RES           LEDC_TIMER_8_BIT
#define LEDC_FREQUENCY          PWM_FREQUENCY_HZ

// PWM任务栈大小 (分配到外部RAM)
#define PWM_TASK_STACK_SIZE     4096

/**
 * @brief PWM控制任务
 */
static void pwm_control_task(void *pvParameters)
{
    uint32_t notification_value;
    
    ESP_LOGI(TAG, "PWM控制任务已启动");
    
    while (1) {
        // 等待任务通知
        notification_value = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        if (notification_value & PWM_NOTIFY_BIT_SET) {
            // ESP_LOGI(TAG, "收到PWM设置通知: 占空比=%d%%, 持续时间=%ldms", 
            //          current_pwm.duty_cycle, current_pwm.duration_ms);
            
            // 将用户输入范围(0-100)转换为PWM范围(0-255)
            uint8_t raw_duty_cycle = (current_pwm.duty_cycle * PWM_DUTY_CYCLE_RAW_MAX) / PWM_DUTY_CYCLE_MAX;
            
            // 设置PWM占空比
            esp_err_t ret = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, raw_duty_cycle);
            if (ret == ESP_OK) {
                ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
                current_pwm.is_active = true;
                // ESP_LOGI(TAG, "PWM占空比设置成功: %d%% (原始值: %d)", current_pwm.duty_cycle, raw_duty_cycle);
            } else {
                ESP_LOGE(TAG, "PWM占空比设置失败: %s", esp_err_to_name(ret));
            }
            
            // 如果设置了持续时间，等待指定时间后停止
            if (current_pwm.duration_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(current_pwm.duration_ms));
                
                // 停止PWM输出
                ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
                ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
                
                current_pwm.is_active = false;
                // ESP_LOGI(TAG, "PWM输出已停止");
            }
        }
        
        if (notification_value & PWM_NOTIFY_BIT_STOP) {
            // ESP_LOGI(TAG, "收到PWM停止通知");
            
            // 停止PWM输出
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
            
            current_pwm.is_active = false;
            // ESP_LOGI(TAG, "PWM输出已停止");
        }
    }
}

bool pwm_control_init(void)
{
    if (pwm_initialized) {
        ESP_LOGW(TAG, "PWM控制模块已初始化");
        return true;
    }
    
    // 配置LEDC定时器
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC定时器配置失败: %s", esp_err_to_name(ret));
        return false;
    }
    
    // 配置LEDC通道
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PWM_GPIO_PIN,
        .duty = 0,
        .hpoint = 0
    };
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC通道配置失败: %s", esp_err_to_name(ret));
        return false;
    }
    
    // 在外部RAM中分配任务栈，在内部RAM中分配TCB
    StackType_t *pwm_task_stack = (StackType_t*)heap_caps_malloc(
        PWM_TASK_STACK_SIZE * sizeof(StackType_t), 
        MALLOC_CAP_SPIRAM
    );
    
    StaticTask_t *pwm_task_tcb = (StaticTask_t*)heap_caps_malloc(
        sizeof(StaticTask_t), 
        MALLOC_CAP_INTERNAL
    );
    
    if (pwm_task_stack == NULL || pwm_task_tcb == NULL) {
        ESP_LOGE(TAG, "无法分配任务栈或TCB内存");
        if (pwm_task_stack) free(pwm_task_stack);
        if (pwm_task_tcb) free(pwm_task_tcb);
        return false;
    }
    
    // 创建PWM控制任务
    pwm_task_handle = xTaskCreateStatic(
        pwm_control_task,
        "pwm_control_task",
        PWM_TASK_STACK_SIZE,
        NULL,
        5,  // 优先级
        pwm_task_stack,
        pwm_task_tcb
    );
    
    if (pwm_task_handle == NULL) {
        ESP_LOGE(TAG, "创建PWM控制任务失败");
        free(pwm_task_stack);
        free(pwm_task_tcb);
        return false;
    }
    
    // 初始化当前PWM状态
    current_pwm.duty_cycle = 0;
    current_pwm.duration_ms = 0;
    current_pwm.is_active = false;
    
    pwm_initialized = true;
    ESP_LOGI(TAG, "PWM控制模块初始化成功, GPIO=%d, 频率=%dHz", 
             PWM_GPIO_PIN, PWM_FREQUENCY_HZ);
    
    return true;
}

bool pwm_control_set(uint8_t duty_cycle, uint32_t duration_ms)
{
    if (!pwm_initialized) {
        ESP_LOGE(TAG, "PWM控制模块未初始化");
        return false;
    }
    
    if (duty_cycle > PWM_DUTY_CYCLE_MAX) {
        ESP_LOGE(TAG, "占空比超出范围: %d > %d", duty_cycle, PWM_DUTY_CYCLE_MAX);
        return false;
    }
    
    // 更新PWM控制参数
    current_pwm.duty_cycle = duty_cycle;
    current_pwm.duration_ms = duration_ms;
    current_pwm.is_active = false;
    
    // 发送任务通知
    BaseType_t ret = xTaskNotify(pwm_task_handle, PWM_NOTIFY_BIT_SET, eSetBits);
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "发送PWM控制通知失败");
        return false;
    }
    
    // ESP_LOGI(TAG, "PWM控制通知已发送: 占空比=%d%%, 持续时间=%ldms", 
    //          duty_cycle, duration_ms);
    
    return true;
}

bool pwm_control_stop(void)
{
    if (!pwm_initialized) {
        ESP_LOGE(TAG, "PWM控制模块未初始化");
        return false;
    }
    
    // 发送停止通知
    BaseType_t ret = xTaskNotify(pwm_task_handle, PWM_NOTIFY_BIT_STOP, eSetBits);
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "发送PWM停止通知失败");
        return false;
    }
    
    // ESP_LOGI(TAG, "PWM停止通知已发送");
    return true;
}

const pwm_control_t* pwm_control_get_status(void)
{
    return &current_pwm;
}

void pwm_control_deinit(void)
{
    if (!pwm_initialized) {
        return;
    }
    
    // 停止PWM输出
    pwm_control_stop();
    
    // 删除任务
    if (pwm_task_handle != NULL) {
        vTaskDelete(pwm_task_handle);
        pwm_task_handle = NULL;
    }
    
    // 停止LEDC
    ledc_stop(LEDC_MODE, LEDC_CHANNEL, 0);
    
    pwm_initialized = false;
    ESP_LOGI(TAG, "PWM控制模块已反初始化");
}
