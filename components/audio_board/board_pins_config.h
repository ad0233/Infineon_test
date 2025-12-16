/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2019 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
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

#ifndef _BOARD_PINS_CONFIG_H_
#define _BOARD_PINS_CONFIG_H_

#include "driver/i2c.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "driver/spi_slave.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief                  Board i2s pin definition
 */
typedef struct {
    int mck_io_num;         /*!< MCK pin, output */
    int bck_io_num;         /*!< BCK pin, input in slave role, output in master role */
    int ws_io_num;          /*!< WS pin, input in slave role, output in master role */
    int data_out_num;       /*!< DATA pin, output */
    int data_in_num;        /*!< DATA pin, input */
} board_i2s_pin_t;

/**
 * @brief                  Board lcd pin definition
 */
typedef struct {
    int bl_pwm;             /*!< Backlight PWM pin */
    int reset;               /*!< Reset pin */
    int cs;                   /*!< Chip select pin */
    int sck;                  /*!< Clock pin */
    int da0;                  /*!< Data line 0 */
    int da1;                  /*!< Data line 1 */
    int da2;                  /*!< Data line 2 */
    int da3;                  /*!< Data line 3 */
} board_lcd_pin_t;

/**
 * @brief                  Board encoder pin definition
 */
typedef struct {
    int pin_a;               /*!< Encoder A pin */
    int pin_b;               /*!< Encoder B pin */
    int pin_btn;             /*!< Encoder button pin */
} board_encoder_pin_t;

/**
 * @brief                  Board BS814 button chip pin definition
 */
typedef struct {
    int clk_pin;             /*!< BS814 clock pin */
    int data_pin;            /*!< BS814 data pin */
} board_bs814_pin_t;

/**
 * @brief                  Board PCF8574RGTR I2C IO expander configuration
 */
typedef struct {
    uint8_t i2c_addr;        /*!< I2C address */
    int p1_lcd_reset;        /*!< P1 pin function: 1=LCD_RESET */
} board_pcf8574_config_t;

/**
 * @brief                  Get i2c pins configuration
 *
 * @param      port        i2c port number to get configuration
 * @param      i2c_config  i2c configuration parameters
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t get_i2c_pins(i2c_port_t port, i2c_config_t *i2c_config);

/**
 * @brief                  Get i2s pins configuration
 *
 * @param      port        i2s port number to get configuration
 * @param      i2s_config  i2s configuration parameters
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t get_i2s_pins(int port, board_i2s_pin_t *i2s_config);

/**
 * @brief                  Get spi pins configuration
 *
 * @param      spi_config                   spi bus configuration parameters
 * @param      spi_device_interface_config  spi device configuration parameters
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t get_spi_pins(spi_bus_config_t *spi_config, spi_device_interface_config_t *spi_device_interface_config);

/**
 * @brief  Get the gpio number for sdcard interrupt
 *
 * @return  -1      non-existent
 *          Others  sdcard interrupt gpio number
 */
int8_t get_sdcard_intr_gpio(void);

/**
 * @brief  Get sdcard maximum number of open files
 *
 * @return  -1      error
 *          Others  max num
 */
int8_t get_sdcard_open_file_num_max(void);

/**
 * @brief  Get the gpio number for sdcard power control
 *
 * @return  -1      error
 *          Others  SDCard power GPIO
 */
int8_t get_sdcard_power_ctrl_gpio(void);

/**
 * @brief  Get the gpio number for auxin detection
 *
 * @return  -1      non-existent
 *          Others  gpio number
 */
int8_t get_auxin_detect_gpio(void);

/**
 * @brief  Get the gpio number for headphone detection
 *
 * @return  -1      non-existent
 *          Others  gpio number
 */
int8_t get_headphone_detect_gpio(void);

/**
 * @brief  Get the gpio number for PA enable
 *
 * @return  -1      non-existent
 *          Others  gpio number
 */
int8_t get_pa_enable_gpio(void);

/**
 * @brief  Get the gpio number for adc detection
 *
 * @return  -1      non-existent
 *          Others  gpio number
 */
int8_t get_adc_detect_gpio(void);

/**
 * @brief  Get the mclk gpio number of es7243
 *
 * @return  -1      non-existent
 *          Others  gpio number
 */
int8_t get_es7243_mclk_gpio(void);

/**
 * @brief  Get the record-button id for adc-button
 *
 * @return  -1      non-existent
 *          Others  button id
 */
int8_t get_input_rec_id(void);

/**
 * @brief  Get the number for mode-button
 *
 * @return  -1      non-existent
 *          Others  number
 */
int8_t get_input_mode_id(void);

/**
 * @brief  Get the number for color-button
 *
 * @return  -1      non-existent
 *          Others  number
 */
int8_t get_input_color_id(void);

/**
 * @brief Get number for set function
 *
 * @return -1       non-existent
 *         Others   number
 */
int8_t get_input_set_id(void);

/**
 * @brief Get number for play function
 *
 * @return -1       non-existent
 *         Others   number
 */
int8_t get_input_play_id(void);

/**
 * @brief number for volume up function
 *
 * @return -1       non-existent
 *         Others   number
 */
int8_t get_input_volup_id(void);

/**
 * @brief Get number for volume down function
 *
 * @return -1       non-existent
 *         Others   number
 */
int8_t get_input_voldown_id(void);

/**
 * @brief Get green led gpio number
 *
 * @return -1       non-existent
 *        Others    gpio number
 */
int8_t get_reset_codec_gpio(void);

/**
 * @brief Get DSP reset gpio number
 *
 * @return -1       non-existent
 *         Others   gpio number
 */
int8_t get_reset_board_gpio(void);

/**
 * @brief Get DSP reset gpio number
 *
 * @return -1       non-existent
 *         Others   gpio number
 */
int8_t get_green_led_gpio(void);

/**
 * @brief Get green led gpio number
 *
 * @return -1       non-existent
 *         Others   gpio number
 */
int8_t get_blue_led_gpio(void);

/**
 * @brief                  Get lcd pins configuration
 *
 * @param      lcd_config  lcd configuration parameters
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t get_lcd_pins(board_lcd_pin_t *lcd_config);

/**
 * @brief                  Get encoder pins configuration
 *
 * @param      encoder_config  encoder configuration parameters
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t get_encoder_pins(board_encoder_pin_t *encoder_config);

/**
 * @brief                  Get BS814 button chip pins configuration
 *
 * @param      bs814_config  BS814 configuration parameters
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t get_bs814_pins(board_bs814_pin_t *bs814_config);

/**
 * @brief                  Get PCF8574RGTR I2C IO expander configuration
 *
 * @param      pcf8574_config  PCF8574RGTR configuration parameters
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t get_pcf8574_config(board_pcf8574_config_t *pcf8574_config);

#ifdef __cplusplus
}
#endif

#endif
