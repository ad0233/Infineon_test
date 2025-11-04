#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef __cplusplus
extern "C" {
#endif

// PWM配置
#define PWM_GPIO_PIN         5
#define PWM_FREQUENCY_HZ     10000  // 10kHz
#define PWM_RESOLUTION_BITS  8      // 8位分辨率，占空比范围0-255
#define PWM_DUTY_CYCLE_MAX   100    // 用户输入范围0-100
#define PWM_DUTY_CYCLE_RAW_MAX 255 // 内部PWM范围0-255

// PWM控制结构体
typedef struct {
    uint8_t duty_cycle;     // 占空比 (0-100)
    uint32_t duration_ms;    // 持续时间 (毫秒)
    bool is_active;          // 是否激活
} pwm_control_t;

/**
 * @brief 初始化PWM控制模块
 * @return true 成功, false 失败
 */
bool pwm_control_init(void);

/**
 * @brief 设置PWM占空比和持续时间
 * @param duty_cycle 占空比 (0-100)
 * @param duration_ms 持续时间 (毫秒)
 * @return true 成功, false 失败
 */
bool pwm_control_set(uint8_t duty_cycle, uint32_t duration_ms);

/**
 * @brief 停止PWM输出
 * @return true 成功, false 失败
 */
bool pwm_control_stop(void);

/**
 * @brief 获取当前PWM状态
 * @return PWM控制结构体指针
 */
const pwm_control_t* pwm_control_get_status(void);

/**
 * @brief 反初始化PWM控制模块
 */
void pwm_control_deinit(void);

#ifdef __cplusplus
}
#endif
