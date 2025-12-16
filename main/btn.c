#include "btn.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include "board.h"
#include "my_ui_behavior.h"
#include "esp_log.h"

static board_bs814_pin_t bs814_pins;

// ---- 兼容新旧 IDF 的 delay 头文件 ----
#if __has_include("esp_rom/esp_rom_sys.h")
    #include "esp_rom/esp_rom_sys.h"
#elif __has_include("esp32s3/rom/ets_sys.h")
    #include "esp32s3/rom/ets_sys.h"
#elif __has_include("esp32/rom/ets_sys.h")
    #include "esp32/rom/ets_sys.h"
#else
    #error "No esp_rom_delay_us or ets_delay_us header found"
#endif
#define KEY_LONG_PRESS_TICKS pdMS_TO_TICKS(500)
static uint8_t g_last_raw = 0;
static const char *TAG_BTN = "btn";

// ---------- 音量调整函数 ----------
static void btn_adjust_volume(int step)
{
    uint8_t current_volume = my_ui_volume_get();
    int new_volume = current_volume + step;
    
    // 限制音量范围在0-100
    if (new_volume > 100) {
        new_volume = 100;
    } else if (new_volume < 0) {
        new_volume = 0;
    }
    
    if (new_volume != current_volume) {
        ESP_LOGI(TAG_BTN, "Volume: %d -> %d (step: %d)", current_volume, new_volume, step);
        my_ui_volume_set(new_volume);
        audio_board_handle_t board_handle = audio_board_init();
        audio_hal_set_volume(board_handle->audio_hal, new_volume);
    }
}

// ---------- 初始化 ----------
void bs814_init(void)
{
    // 获取BS814引脚配置
    ESP_ERROR_CHECK(get_bs814_pins(&bs814_pins));
    
    // CLK 输出
    gpio_config_t clk_conf = {
        .pin_bit_mask = 1ULL << bs814_pins.clk_pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&clk_conf);
    gpio_set_level(bs814_pins.clk_pin, 0);

    // DATA 输入
    gpio_config_t data_conf = {
        .pin_bit_mask = 1ULL << bs814_pins.data_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&data_conf);

    // 读取一次，避免首次误判为“按下”
    g_last_raw = bs814_read_raw();

    printf("BS814 init OK\n");
}

// ---------- 读取 RAW ----------
uint8_t bs814_read_raw(void)
{
    uint8_t result = 0;

    for (int i = 0; i < 8; i++)
    {
        gpio_set_level(bs814_pins.clk_pin, 0);
        esp_rom_delay_us(25);

        gpio_set_level(bs814_pins.clk_pin, 1);
        esp_rom_delay_us(5);

        int d = gpio_get_level(bs814_pins.data_pin);
        if (d) result |= (1 << i);

        esp_rom_delay_us(25);
    }

    gpio_set_level(bs814_pins.clk_pin, 0);

    g_last_raw = result;
    return result;
}

// ---------- 按键判断（0=按下） ----------
bool bs814_key1(void) { return !(g_last_raw & (1 << 1)); }
bool bs814_key2(void) { return !(g_last_raw & (1 << 2)); }
bool bs814_key3(void) { return !(g_last_raw & (1 << 3)); }

// ---------- KEY2 长短按逻辑（结合 rotary_encoder 的事件） ----------
static bool last_k2_state = false;      // 上一次按键状态
static TickType_t k2_press_tick = 0;    // 按下时刻
static bool long_sent = false;          // 长按事件是否已经发过

key_evt_t bs814_key2_update(void)
{
    bs814_read_raw();
    bool now_pressed = bs814_key2();
    TickType_t now_tick = xTaskGetTickCount();
    key_evt_t evt = KEY_EVT_NONE;

    // 刚按下
    if (now_pressed && !last_k2_state)
    {
        k2_press_tick = now_tick;
        long_sent = false;           // 重置长按标志
        evt = KEY_EVT_PRESSED;
    }

    // 长按，只触发一次
    if (now_pressed && !long_sent)
    {
        if (now_tick - k2_press_tick >= KEY_LONG_PRESS_TICKS)
        {
            evt = KEY_EVT_LONG;
            long_sent = true;         // 标记已触发
        }
    }

    // 松开
    if (!now_pressed && last_k2_state)
    {
        if (!long_sent)
        {
            evt = KEY_EVT_CLICKED;    // 短按点击
        }
        else
        {
            evt = KEY_EVT_RELEASED;   // 松开事件
        }
    }

    last_k2_state = now_pressed;
    return evt;
}

// ---------- KEY1 长短按逻辑（音量减） ----------
static bool last_k1_state = false;      // 上一次按键状态
static TickType_t k1_press_tick = 0;    // 按下时刻
static bool k1_long_sent = false;       // 长按事件是否已经发过
static TickType_t k1_last_long_tick = 0; // 上次长按触发时刻（用于持续触发）

key_evt_t bs814_key1_update(void)
{
    bs814_read_raw();
    bool now_pressed = bs814_key1();
    TickType_t now_tick = xTaskGetTickCount();
    key_evt_t evt = KEY_EVT_NONE;

    // 刚按下
    if (now_pressed && !last_k1_state)
    {
        k1_press_tick = now_tick;
        k1_long_sent = false;           // 重置长按标志
        k1_last_long_tick = 0;          // 重置长按触发时刻
        evt = KEY_EVT_PRESSED;
    }

    // 长按检测和持续触发
    if (now_pressed)
    {
        if (!k1_long_sent)
        {
            // 首次达到长按时间
            if (now_tick - k1_press_tick >= KEY_LONG_PRESS_TICKS)
            {
                evt = KEY_EVT_LONG;
                k1_long_sent = true;         // 标记已触发
                k1_last_long_tick = now_tick;
            }
        }
        else
        {
            // 长按中，每200ms触发一次持续事件
            if (now_tick - k1_last_long_tick >= pdMS_TO_TICKS(200))
            {
                evt = KEY_EVT_LONG;
                k1_last_long_tick = now_tick;
            }
        }
    }

    // 松开
    if (!now_pressed && last_k1_state)
    {
        if (!k1_long_sent)
        {
            evt = KEY_EVT_CLICKED;    // 短按点击
            // 短按：降低1点音量
            btn_adjust_volume(-1);
        }
        else
        {
            evt = KEY_EVT_RELEASED;   // 松开事件
        }
    }
    
    // 长按持续触发时调整音量
    if (evt == KEY_EVT_LONG)
    {
        // 长按：持续降低音量
        btn_adjust_volume(-1);
    }

    last_k1_state = now_pressed;
    return evt;
}

// ---------- KEY3 长短按逻辑（音量加） ----------
static bool last_k3_state = false;      // 上一次按键状态
static TickType_t k3_press_tick = 0;    // 按下时刻
static bool k3_long_sent = false;       // 长按事件是否已经发过
static TickType_t k3_last_long_tick = 0; // 上次长按触发时刻（用于持续触发）

key_evt_t bs814_key3_update(void)
{
    bs814_read_raw();
    bool now_pressed = bs814_key3();
    TickType_t now_tick = xTaskGetTickCount();
    key_evt_t evt = KEY_EVT_NONE;

    // 刚按下
    if (now_pressed && !last_k3_state)
    {
        k3_press_tick = now_tick;
        k3_long_sent = false;           // 重置长按标志
        k3_last_long_tick = 0;          // 重置长按触发时刻
        evt = KEY_EVT_PRESSED;
    }

    // 长按检测和持续触发
    if (now_pressed)
    {
        if (!k3_long_sent)
        {
            // 首次达到长按时间
            if (now_tick - k3_press_tick >= KEY_LONG_PRESS_TICKS)
            {
                evt = KEY_EVT_LONG;
                k3_long_sent = true;         // 标记已触发
                k3_last_long_tick = now_tick;
            }
        }
        else
        {
            // 长按中，每200ms触发一次持续事件
            if (now_tick - k3_last_long_tick >= pdMS_TO_TICKS(200))
            {
                evt = KEY_EVT_LONG;
                k3_last_long_tick = now_tick;
            }
        }
    }

    // 松开
    if (!now_pressed && last_k3_state)
    {
        if (!k3_long_sent)
        {
            evt = KEY_EVT_CLICKED;    // 短按点击
            // 短按：增加1点音量
            btn_adjust_volume(1);
        }
        else
        {
            evt = KEY_EVT_RELEASED;   // 松开事件
        }
    }
    
    // 长按持续触发时调整音量
    if (evt == KEY_EVT_LONG)
    {
        // 长按：持续增加音量
        btn_adjust_volume(1);
    }

    last_k3_state = now_pressed;
    return evt;
}

