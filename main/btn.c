#include "btn.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

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

// ---------- 初始化 ----------
void bs814_init(void)
{
    // CLK 输出
    gpio_config_t clk_conf = {
        .pin_bit_mask = 1ULL << BS814_CLK_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&clk_conf);
    gpio_set_level(BS814_CLK_PIN, 0);

    // DATA 输入
    gpio_config_t data_conf = {
        .pin_bit_mask = 1ULL << BS814_DATA_PIN,
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
        gpio_set_level(BS814_CLK_PIN, 0);
        esp_rom_delay_us(25);

        gpio_set_level(BS814_CLK_PIN, 1);
        esp_rom_delay_us(5);

        int d = gpio_get_level(BS814_DATA_PIN);
        if (d) result |= (1 << i);

        esp_rom_delay_us(25);
    }

    gpio_set_level(BS814_CLK_PIN, 0);

    g_last_raw = result;
    return result;
}

// ---------- 按键判断（0=按下） ----------
bool bs814_key1(void) { return !(g_last_raw & (1 << 0)); }
bool bs814_key2(void) { return !(g_last_raw & (1 << 1)); }
bool bs814_key3(void) { return !(g_last_raw & (1 << 2)); }

// ---------- KEY3 长短按逻辑（结合 rotary_encoder 的事件） ----------
static bool last_k3_state = false;      // 上一次按键状态
static TickType_t k3_press_tick = 0;    // 按下时刻
static bool long_sent = false;          // 长按事件是否已经发过

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
        long_sent = false;           // 重置长按标志
        evt = KEY_EVT_PRESSED;
    }

    // 长按，只触发一次
    if (now_pressed && !long_sent)
    {
        if (now_tick - k3_press_tick >= KEY_LONG_PRESS_TICKS)
        {
            evt = KEY_EVT_LONG;
            long_sent = true;         // 标记已触发
        }
    }

    // 松开
    if (!now_pressed && last_k3_state)
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

    last_k3_state = now_pressed;
    return evt;
}

