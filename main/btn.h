#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BS814_CLK_PIN   5
#define BS814_DATA_PIN  38

void bs814_init(void);
uint8_t bs814_read_raw(void);

// 按键状态读取
bool bs814_key1(void);
bool bs814_key2(void);
bool bs814_key3(void);

// 按键事件类型
typedef enum {
    KEY_EVT_NONE = 0,
    KEY_EVT_PRESSED,      // 刚按下
    KEY_EVT_LONG,         // 长按中
    KEY_EVT_RELEASED,     // 松开
    KEY_EVT_CLICKED       // 短按点击
} key_evt_t;

// KEY3 更新函数（必须定时调用）
key_evt_t bs814_key3_update(void);

#ifdef __cplusplus
}
#endif
