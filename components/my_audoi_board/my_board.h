#ifndef MY_BOARD_H
#define MY_BOARD_H

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- I2C 配置 ---
#define BSP_I2C_SDA             GPIO_NUM_26
#define BSP_I2C_SCL             GPIO_NUM_27
#define BSP_I2C_NUM             I2C_NUM_0 
#define BSP_I2C_FREQ_HZ         100000

// --- I2S 配置 ---
#define BSP_I2S_NUM             I2S_NUM_0
#define BSP_I2S_MCLK            GPIO_NUM_9
#define BSP_I2S_SCLK            GPIO_NUM_8
#define BSP_I2S_LCLK            GPIO_NUM_7
#define BSP_I2S_DOUT            GPIO_NUM_6
#define BSP_I2S_DSIN            GPIO_NUM_NC 

// --- 功率放大器 (PA) 控制 ---
#define BSP_POWER_AMP_IO        GPIO_NUM_46

// --- 默认音频参数 ---
#define CODEC_DEFAULT_SAMPLE_RATE    (16000)
#define CODEC_DEFAULT_BIT_WIDTH      (I2S_DATA_BIT_WIDTH_16BIT)
#define CODEC_DEFAULT_CHANNEL        (I2S_SLOT_MODE_STEREO)

/**
 * @brief 获取 I2C 主机总线句柄（需在 bsp_audio_bus_init 之后调用）
 */
i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void);

/**
 * @brief 初始化 I2C 总线和 I2S（需先于 codec 调用）
 */
esp_err_t bsp_audio_bus_init(void);

/**
 * @brief 初始化两颗 ES8311 codec（依赖 bsp_audio_bus_init 已执行）
 */
esp_err_t bsp_audio_codec_init(void);

/**
 * @brief 初始化音频外设及编解码器（等价于 bus_init + codec_init）
 */
esp_err_t bsp_audio_init(void);

/**
 * @brief 获取音频操作句柄 (ES8311 @ 0x18)
 */
esp_codec_dev_handle_t bsp_audio_get_play_handle(void);

/**
 * @brief 获取第二路音频操作句柄 (ES8311 @ 0x19)
 */
esp_codec_dev_handle_t bsp_audio_get_play_handle_2(void);

/**
 * @brief 扫描 I2C 总线并打印所有应答的 7-bit 地址 (0x08~0x77)
 * @return ESP_OK 成功
 */
esp_err_t bsp_i2c_scan(void);

#ifdef __cplusplus
}
#endif

#endif