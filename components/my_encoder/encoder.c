/*
 * Copyright (c) 2019 Ruslan V. Uss <unclerus@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of itscontributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file encoder.c
 *
 * ESP-IDF HW timer-based driver for rotary encoders
 *
 * Copyright (c) 2019 Ruslan V. Uss <unclerus@gmail.com>
 *
 * BSD Licensed as described in the file LICENSE
 */
#include "encoder.h"
#include <esp_log.h>
#include <string.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
#include <driver/gpio.h>

#define MUTEX_TIMEOUT 10

#ifdef CONFIG_RE_BTN_PRESSED_LEVEL_0
#define BTN_PRESSED_LEVEL 0
#else
#define BTN_PRESSED_LEVEL 1
#endif

#if defined(CONFIG_IDF_TARGET_ESP8266) && CONFIG_RE_INTERVAL_US < 10000
#error Too small CONFIG_RE_INTERVAL_US! For ESP8266 it should be >= 10000
#endif

#define CONFIG_RE_MAX 1
#define CONFIG_RE_ACCELERATION_MAX_CUTOFF 1000000
#define CONFIG_RE_ACCELERATION_MIN_CUTOFF 500000
#define CONFIG_RE_BTN_LONG_PRESS_TIME_US 1000000
#define CONFIG_RE_BTN_DEAD_TIME_US 100000
#define CONFIG_RE_INTERVAL_US 10000

static const char *TAG = "encoder";
static rotary_encoder_t *encs[CONFIG_RE_MAX] = { 0 };
static SemaphoreHandle_t mutex;
static QueueHandle_t _queue;
// static bool gpio_isr_service_installed = false;

#define GPIO_BIT(x) ((x) < 32 ? BIT(x) : ((uint64_t)(((uint64_t)1)<<(x))))
#define CHECK(x) do { esp_err_t __; if ((__ = x) != ESP_OK) return __; } while (0)
#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)

// 状态机定义
typedef enum {
    ENCODER_STATE_00 = 0,  // A=0, B=0
    ENCODER_STATE_01 = 1,  // A=0, B=1  
    ENCODER_STATE_10 = 2,  // A=1, B=0
    ENCODER_STATE_11 = 3,  // A=1, B=1
    ENCODER_STATE_INVALID = 255
} encoder_state_t;

// 状态转换表 - 定义有效的状态转换和方向
typedef struct {
    encoder_state_t from;
    encoder_state_t to;
    int8_t direction;  // 1=顺时针, -1=逆时针, 0=无效
} state_transition_t;

// 有效的状态转换表
static const state_transition_t valid_transitions[] = {
    // 顺时针转换
    {ENCODER_STATE_00, ENCODER_STATE_01, 1},   // 0->1 顺时针
    {ENCODER_STATE_01, ENCODER_STATE_11, 1},   // 1->3 顺时针  
    {ENCODER_STATE_11, ENCODER_STATE_10, 1},   // 3->2 顺时针
    {ENCODER_STATE_10, ENCODER_STATE_00, 1},   // 2->0 顺时针
    
    // 逆时针转换
    {ENCODER_STATE_00, ENCODER_STATE_10, -1},  // 0->2 逆时针
    {ENCODER_STATE_10, ENCODER_STATE_11, -1},  // 2->3 逆时针
    {ENCODER_STATE_11, ENCODER_STATE_01, -1},  // 3->1 逆时针
    {ENCODER_STATE_01, ENCODER_STATE_00, -1},  // 1->0 逆时针
};

#define NUM_TRANSITIONS (sizeof(valid_transitions) / sizeof(valid_transitions[0]))

// 状态机处理函数
static int8_t IRAM_ATTR process_encoder_state_machine(rotary_encoder_t *re, uint8_t current_state)
{
    // 查找有效的状态转换
    for (size_t i = 0; i < NUM_TRANSITIONS; i++) {
        if (valid_transitions[i].from == re->last_state && 
            valid_transitions[i].to == current_state) {
            
            // 增加计数器
            if (valid_transitions[i].direction == 1) {
                re->clockwise_count++;
                re->counterclockwise_count = 0; // 重置反向计数
            } else if (valid_transitions[i].direction == -1) {
                re->counterclockwise_count++;
                re->clockwise_count = 0; // 重置正向计数
            }
            
            // 检查是否达到阈值（2格变化才触发一次事件）
            if (re->clockwise_count >= 2) {
                re->clockwise_count = 0;
                return 1; // 顺时针
            } else if (re->counterclockwise_count >= 2) {
                re->counterclockwise_count = 0;
                return -1; // 逆时针
            }
            
            return 0; // 还没达到阈值，不触发事件
        }
    }
    return 0; // 无效转换
}

// GPIO中断处理函数
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    
    // 查找对应的编码器
    for (size_t i = 0; i < CONFIG_RE_MAX; i++) {
        if (encs[i] && (encs[i]->pin_a == gpio_num || encs[i]->pin_b == gpio_num)) {
            rotary_encoder_t *re = encs[i];
            
            // 读取当前A、B引脚状态
            uint8_t current_state = (gpio_get_level(re->pin_a) << 1) | gpio_get_level(re->pin_b);
            
            // 状态变化检测
            if (re->last_state != current_state) {
                // 添加时间间隔检查，防止过于频繁的状态变化
                int64_t current_time = esp_timer_get_time();
                if (current_time - re->last_change_time < 1000) { // 1ms防抖
                    return;
                }
                
                // 使用状态机处理状态转换
                int8_t direction = process_encoder_state_machine(re, current_state);
                
                const char* direction_str = "UNKNOWN";
                if (direction == 1) {
                    direction_str = "CLOCKWISE";
                } else if (direction == -1) {
                    direction_str = "COUNTER_CLOCKWISE";
                }
                
                // 打印状态变化信息（中断安全）
                // ESP_DRAM_LOGI(TAG, "State: %d->%d, %s, A=%d, B=%d", 
                //        re->last_state, current_state, direction_str,
                //        gpio_get_level(re->pin_a), gpio_get_level(re->pin_b));
                
                // 更新状态和时间
                re->last_state = current_state;
                re->last_change_time = current_time;
                
                // 如果有有效方向变化，发送事件
                if (direction != 0) {
                    rotary_encoder_event_t ev = {
                        .type = RE_ET_CHANGED,
                        .sender = re,
                        .diff = direction
                    };
                    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                    xQueueSendFromISR(_queue, &ev, &xHigherPriorityTaskWoken);
                    if (xHigherPriorityTaskWoken) {
                        portYIELD_FROM_ISR();
                    }
                }
            }
            break;
        }
    }
}


// 处理按钮状态（仍使用定时器轮询，因为按钮需要防抖）
inline static void read_button(rotary_encoder_t *re)
{
    rotary_encoder_event_t ev = {
        .sender = re
    };

    if (re->pin_btn >= GPIO_NUM_MAX)
        return;

    if (re->btn_state == RE_BTN_PRESSED && re->btn_pressed_time_us < CONFIG_RE_BTN_DEAD_TIME_US)
    {
        // Dead time
        re->btn_pressed_time_us += CONFIG_RE_INTERVAL_US;
        return;
    }

    // read button state
    if (gpio_get_level(re->pin_btn) == BTN_PRESSED_LEVEL)
    {
        if (re->btn_state == RE_BTN_RELEASED)
        {
            // first press
            re->btn_state = RE_BTN_PRESSED;
            re->btn_pressed_time_us = 0;
            ev.type = RE_ET_BTN_PRESSED;
            xQueueSendToBack(_queue, &ev, 0);
            return;
        }

        re->btn_pressed_time_us += CONFIG_RE_INTERVAL_US;

        if (re->btn_state == RE_BTN_PRESSED && re->btn_pressed_time_us >= CONFIG_RE_BTN_LONG_PRESS_TIME_US)
        {
            // Long press
            re->btn_state = RE_BTN_LONG_PRESSED;
            ev.type = RE_ET_BTN_LONG_PRESSED;
            xQueueSendToBack(_queue, &ev, 0);
        }
    }
    else if (re->btn_state != RE_BTN_RELEASED)
    {
        bool clicked = re->btn_state == RE_BTN_PRESSED;
        // released
        re->btn_state = RE_BTN_RELEASED;
        ev.type = RE_ET_BTN_RELEASED;
        xQueueSendToBack(_queue, &ev, 0);
        if (clicked)
        {
            ev.type = RE_ET_BTN_CLICKED;
            xQueueSendToBack(_queue, &ev, 0);
        }
    }
}

static void timer_handler(void *arg)
{
    if (!xSemaphoreTake(mutex, 0))
        return;

    // 只处理按钮状态，编码器由GPIO中断处理
    for (size_t i = 0; i < CONFIG_RE_MAX; i++)
        if (encs[i])
            read_button(encs[i]);

    xSemaphoreGive(mutex);
}

static const esp_timer_create_args_t timer_args =
{
    .name = "__encoder__",
    .arg = NULL,
    .callback = timer_handler,
    .dispatch_method = ESP_TIMER_TASK
};

static esp_timer_handle_t timer;

esp_err_t rotary_encoder_init(QueueHandle_t queue)
{
    CHECK_ARG(queue);
    _queue = queue;

    mutex = xSemaphoreCreateMutex();
    if (!mutex)
    {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 安装GPIO中断服务（只安装一次）
    // if (!gpio_isr_service_installed) {
    //     esp_err_t ret = gpio_install_isr_service(0);
    //     if (ret == ESP_OK) {
    //         gpio_isr_service_installed = true;
    //     } else if (ret == ESP_ERR_INVALID_STATE) {
    //         // GPIO中断服务已经安装，这是正常的
    //         ESP_LOGI(TAG, "GPIO ISR service already installed");
    //         gpio_isr_service_installed = true;
    //     } else {
    //         ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
    //         return ret;
    //     }
    // }

    CHECK(esp_timer_create(&timer_args, &timer));
    CHECK(esp_timer_start_periodic(timer, CONFIG_RE_INTERVAL_US));

    ESP_LOGI(TAG, "Initialization complete, timer interval: %dms (for button only)", CONFIG_RE_INTERVAL_US / 1000);
    return ESP_OK;
}

esp_err_t rotary_encoder_add(rotary_encoder_t *re)
{
    CHECK_ARG(re);
    if (!xSemaphoreTake(mutex, MUTEX_TIMEOUT))
    {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_INVALID_STATE;
    }

    bool ok = false;
    for (size_t i = 0; i < CONFIG_RE_MAX; i++)
        if (!encs[i])
        {
            re->index = i;
            encs[i] = re;
            encs[i]->last_state = 255;
            ok = true;
            break;
        }
    if (!ok)
    {
        ESP_LOGE(TAG, "Too many encoders");
        xSemaphoreGive(mutex);
        return ESP_ERR_NO_MEM;
    }

    // setup GPIO for encoder pins (A, B) with interrupt
    gpio_config_t io_conf;
    memset(&io_conf, 0, sizeof(gpio_config_t));
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;  // 编码器通常需要上拉
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;    // 任何边沿都触发中断
    io_conf.pin_bit_mask = GPIO_BIT(re->pin_a) | GPIO_BIT(re->pin_b);
    CHECK(gpio_config(&io_conf));

    // 为A、B引脚添加中断处理
    CHECK(gpio_isr_handler_add(re->pin_a, gpio_isr_handler, (void*) re->pin_a));
    CHECK(gpio_isr_handler_add(re->pin_b, gpio_isr_handler, (void*) re->pin_b));

    // setup GPIO for button (if exists)
    if (re->pin_btn < GPIO_NUM_MAX) {
        memset(&io_conf, 0, sizeof(gpio_config_t));
        io_conf.mode = GPIO_MODE_INPUT;
        if (BTN_PRESSED_LEVEL == 0)
        {
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        }
        else
        {
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
        }
        io_conf.intr_type = GPIO_INTR_DISABLE;  // 按钮不使用中断，用定时器轮询
        io_conf.pin_bit_mask = GPIO_BIT(re->pin_btn);
        CHECK(gpio_config(&io_conf));
    }

    re->btn_state = RE_BTN_RELEASED;
    re->btn_pressed_time_us = 0;
    
    // 初始化编码器状态
    re->last_state = (gpio_get_level(re->pin_a) << 1) | gpio_get_level(re->pin_b);
    re->last_change_time = esp_timer_get_time();
    re->clockwise_count = 0;
    re->counterclockwise_count = 0;

    xSemaphoreGive(mutex);

    ESP_LOGI(TAG, "Added rotary encoder %d, A: %d, B: %d, BTN: %d", re->index, re->pin_a, re->pin_b, re->pin_btn);
    return ESP_OK;
}

esp_err_t rotary_encoder_remove(rotary_encoder_t *re)
{
    CHECK_ARG(re);
    if (!xSemaphoreTake(mutex, MUTEX_TIMEOUT))
    {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < CONFIG_RE_MAX; i++)
        if (encs[i] == re)
        {
            // 移除GPIO中断处理
            gpio_isr_handler_remove(re->pin_a);
            gpio_isr_handler_remove(re->pin_b);
            
            encs[i] = NULL;
            ESP_LOGI(TAG, "Removed rotary encoder %d", i);
            xSemaphoreGive(mutex);
            return ESP_OK;
        }

    ESP_LOGE(TAG, "Unknown encoder");
    xSemaphoreGive(mutex);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t rotary_encoder_enable_acceleration(rotary_encoder_t *re, uint16_t coeff)
{
    CHECK_ARG(re);
    re->acceleration.coeff = coeff;
    re->acceleration.last_time = esp_timer_get_time();
    return ESP_OK;
}

esp_err_t rotary_encoder_disable_acceleration(rotary_encoder_t *re)
{
    CHECK_ARG(re);
    re->acceleration.coeff = 0;
    return ESP_OK;
}
