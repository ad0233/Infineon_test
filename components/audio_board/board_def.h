/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2021 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _AUDIO_BOARD_DEFINITION_H_
#define _AUDIO_BOARD_DEFINITION_H_

/**
 * @brief SDCARD Function Definition
 */
#define FUNC_SDCARD_EN              (1)
#define SDCARD_OPEN_FILE_NUM_MAX    (5)
#define SDCARD_INTR_GPIO            GPIO_NUM_NC
#define SDCARD_PWR_CTRL             GPIO_NUM_NC
#define ESP_SD_PIN_CLK              GPIO_NUM_11 // SPI SCK
#define ESP_SD_PIN_CMD              GPIO_NUM_13 // SPI MOSI
#define ESP_SD_PIN_D0               GPIO_NUM_12 // SPI MISO
#define ESP_SD_PIN_D1               GPIO_NUM_NC
#define ESP_SD_PIN_D2               GPIO_NUM_NC
#define ESP_SD_PIN_D3               GPIO_NUM_10 // SPI CS
#define ESP_SD_PIN_D4               GPIO_NUM_NC
#define ESP_SD_PIN_D5               GPIO_NUM_NC
#define ESP_SD_PIN_D6               GPIO_NUM_NC
#define ESP_SD_PIN_D7               GPIO_NUM_NC
#define ESP_SD_PIN_CD               GPIO_NUM_NC
#define ESP_SD_PIN_WP               GPIO_NUM_NC

/**
 * @brief LCD TOUCH PANEL Function Definition
 */
#define FUNC_LCD_TOUCH_EN          (1)
#define TOUCH_PANEL_SWAP_XY        (0)
#define TOUCH_PANEL_INVERSE_X      (1)
#define TOUCH_PANEL_INVERSE_Y      (0)

/**
 * @brief LCD Function Definition
 */
#define FUNC_LCD_EN                (1)
#define LCD_BL_PWM                 GPIO_NUM_3
#define LCD_RESET                  -1
#define LCD_CS                     GPIO_NUM_39
#define LCD_SCK                    GPIO_NUM_9
#define LCD_DA0                     GPIO_NUM_6
#define LCD_DA1                     GPIO_NUM_2
#define LCD_DA2                     GPIO_NUM_4
#define LCD_DA3                     GPIO_NUM_7

/**
 * @brief Encoder Function Definition
 */
#define FUNC_ENCODER_EN            (1)
#define ENCODER_PIN_A               GPIO_NUM_14
#define ENCODER_PIN_B               GPIO_NUM_1
#define ENCODER_PIN_BTN             GPIO_NUM_38

/**
 * @brief BS814 Button Chip Function Definition
 */
#define FUNC_BS814_EN               (1)
#define BS814_CLK_PIN               GPIO_NUM_5
#define BS814_DATA_PIN              GPIO_NUM_38

/**
 * @brief PCF8574RGTR I2C IO Expander Function Definition
 */
#define FUNC_PCF8574_EN             (1)
#define PCF8574_I2C_ADDR            (0x20)  /* I2C address (A0=A1=A2=0) */
#define PCF8574_P1_LCD_RESET        (1)     /* P1 pin controls LCD_RESET */

/**
 * @brief  Audio Codec Chip Function Definition
 */
#define FUNC_AUDIO_CODEC_EN       (1)
#define CODEC_ADC_I2S_PORT        ((i2s_port_t)0)
#define CODEC_ADC_BITS_PER_SAMPLE ((i2s_data_bit_width_t)16)  /* 32bit */
#define CODEC_ADC_SAMPLE_RATE     (16000)
#define RECORD_HARDWARE_AEC       (true)
// #define BOARD_PA_GAIN             (6)  /* Power amplifier gain defined by board (dB) */
#define HEADPHONE_DETECT          (-1)
// #define PA_ENABLE_GPIO            GPIO_NUM_48
#define ES8311_MCLK_SOURCE        (0)  /* 0 From MCLK of esp32   1 From BCLK */
#define ES7210_MIC_SELECT         (ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2 | ES7210_INPUT_MIC3)

/**
 * @brief ADC input data format
 */
#define AUDIO_ADC_INPUT_CH_FORMAT "MMRN"

extern audio_hal_func_t AUDIO_CODEC_ES8311_DEFAULT_HANDLE;
extern audio_hal_func_t AUDIO_CODEC_ES7210_DEFAULT_HANDLE;

#define AUDIO_CODEC_DEFAULT_CONFIG(){                   \
        .adc_input  = AUDIO_HAL_ADC_INPUT_LINE1,        \
        .dac_output = AUDIO_HAL_DAC_OUTPUT_ALL,         \
        .codec_mode = AUDIO_HAL_CODEC_MODE_BOTH,        \
        .i2s_iface = {                                  \
            .mode = AUDIO_HAL_MODE_SLAVE,               \
            .fmt = AUDIO_HAL_I2S_NORMAL,                \
            .samples = AUDIO_HAL_16K_SAMPLES,           \
            .bits = AUDIO_HAL_BIT_LENGTH_16BITS,        \
        },                                              \
};

/**
 * @brief Button Function Definition
 */
#define FUNC_BUTTON_EN              (1)
#define INPUT_KEY_NUM               6
#define BUTTON_VOLUP_ID             0
#define BUTTON_VOLDOWN_ID           1
#define BUTTON_SET_ID               2
#define BUTTON_PLAY_ID              3
#define BUTTON_MODE_ID              4
#define BUTTON_REC_ID               5

#define INPUT_KEY_DEFAULT_INFO() {                      \
     {                                                  \
        .type = PERIPH_ID_ADC_BTN,                      \
        .user_id = INPUT_KEY_USER_ID_REC,               \
        .act_id = BUTTON_REC_ID,                        \
    },                                                  \
    {                                                   \
        .type = PERIPH_ID_ADC_BTN,                      \
        .user_id = INPUT_KEY_USER_ID_MUTE,              \
        .act_id = BUTTON_MODE_ID,                       \
    },                                                  \
    {                                                   \
        .type = PERIPH_ID_ADC_BTN,                      \
        .user_id = INPUT_KEY_USER_ID_SET,               \
        .act_id = BUTTON_SET_ID,                        \
    },                                                  \
    {                                                   \
        .type = PERIPH_ID_ADC_BTN,                      \
        .user_id = INPUT_KEY_USER_ID_PLAY,              \
        .act_id = BUTTON_PLAY_ID,                       \
    },                                                  \
    {                                                   \
        .type = PERIPH_ID_ADC_BTN,                      \
        .user_id = INPUT_KEY_USER_ID_VOLUP,             \
        .act_id = BUTTON_VOLUP_ID,                      \
    },                                                  \
    {                                                   \
        .type = PERIPH_ID_ADC_BTN,                      \
        .user_id = INPUT_KEY_USER_ID_VOLDOWN,           \
        .act_id = BUTTON_VOLDOWN_ID,                    \
    }                                                   \
}

#endif
