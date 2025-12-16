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

#include "board_pins_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <string.h>
#include "board.h"
#include "board_def.h"
#include "audio_error.h"
#include "audio_mem.h"
#include "soc/soc_caps.h"

static const char *TAG = "ESP32_S3_KORVO_2";

esp_err_t get_i2c_pins(i2c_port_t port, i2c_config_t *i2c_config)
{
    AUDIO_NULL_CHECK(TAG, i2c_config, return ESP_FAIL);
    if (port == I2C_NUM_0 || port == I2C_NUM_1) {
        i2c_config->sda_io_num = GPIO_NUM_48;
        i2c_config->scl_io_num = GPIO_NUM_47;
    } else {
        i2c_config->sda_io_num = -1;
        i2c_config->scl_io_num = -1;
        ESP_LOGE(TAG, "i2c port %d is not supported", port);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t get_i2s_pins(int port, board_i2s_pin_t *i2s_config)
{
    AUDIO_NULL_CHECK(TAG, i2s_config, return ESP_FAIL);
    if (port == 0) {
        i2s_config->bck_io_num = GPIO_NUM_42;
        i2s_config->ws_io_num = GPIO_NUM_41;
        i2s_config->data_out_num = GPIO_NUM_45;
        i2s_config->data_in_num = GPIO_NUM_40;
        i2s_config->mck_io_num = GPIO_NUM_46;
    } else if (port == 1) {
        i2s_config->bck_io_num = -1;
        i2s_config->ws_io_num = -1;
        i2s_config->data_out_num = -1;
        i2s_config->data_in_num = -1;
        i2s_config->mck_io_num = -1;
    } else {
        memset(i2s_config, -1, sizeof(board_i2s_pin_t));
        ESP_LOGE(TAG, "i2s port %d is not supported", port);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t get_spi_pins(spi_bus_config_t *spi_config, spi_device_interface_config_t *spi_device_interface_config)
{
    AUDIO_NULL_CHECK(TAG, spi_config, return ESP_FAIL);
    AUDIO_NULL_CHECK(TAG, spi_device_interface_config, return ESP_FAIL);

    spi_config->mosi_io_num = -1;
    spi_config->miso_io_num = -1;
    spi_config->sclk_io_num = -1;
    spi_config->quadwp_io_num = -1;
    spi_config->quadhd_io_num = -1;

    spi_device_interface_config->spics_io_num = -1;

    ESP_LOGW(TAG, "SPI interface is not supported");
    return ESP_OK;
}

// sdcard

int8_t get_sdcard_intr_gpio(void)
{
    return -1;
}

int8_t get_sdcard_open_file_num_max(void)
{
    return -1;
}

int8_t get_sdcard_power_ctrl_gpio(void)
{
    return -1;
}

// input-output pins

int8_t get_headphone_detect_gpio(void)
{
    return -1;
}

int8_t get_pa_enable_gpio(void)
{
    return -1;
}

// adc button id

int8_t get_input_rec_id(void)
{
    return BUTTON_REC_ID;
}

int8_t get_input_mode_id(void)
{
    return BUTTON_MODE_ID;
}

int8_t get_input_set_id(void)
{
    return BUTTON_SET_ID;
}

int8_t get_input_play_id(void)
{
    return BUTTON_PLAY_ID;
}

int8_t get_input_volup_id(void)
{
    return BUTTON_VOLUP_ID;
}

int8_t get_input_voldown_id(void)
{
    return BUTTON_VOLDOWN_ID;
}

// led pins

int8_t get_green_led_gpio(void)
{
    return -1;
}

int8_t get_blue_led_gpio(void)
{
    return -1;
}

int8_t get_es8311_mclk_src(void)
{
    return ES8311_MCLK_SOURCE;
}

// lcd pins

esp_err_t get_lcd_pins(board_lcd_pin_t *lcd_config)
{
    AUDIO_NULL_CHECK(TAG, lcd_config, return ESP_FAIL);
    
    lcd_config->bl_pwm = LCD_BL_PWM;
    lcd_config->reset = LCD_RESET;
    lcd_config->cs = LCD_CS;
    lcd_config->sck = LCD_SCK;
    lcd_config->da0 = LCD_DA0;
    lcd_config->da1 = LCD_DA1;
    lcd_config->da2 = LCD_DA2;
    lcd_config->da3 = LCD_DA3;
    
    return ESP_OK;
}

// encoder pins

esp_err_t get_encoder_pins(board_encoder_pin_t *encoder_config)
{
    AUDIO_NULL_CHECK(TAG, encoder_config, return ESP_FAIL);
    
    encoder_config->pin_a = ENCODER_PIN_A;
    encoder_config->pin_b = ENCODER_PIN_B;
    encoder_config->pin_btn = ENCODER_PIN_BTN;
    
    return ESP_OK;
}

// bs814 button chip pins

esp_err_t get_bs814_pins(board_bs814_pin_t *bs814_config)
{
    AUDIO_NULL_CHECK(TAG, bs814_config, return ESP_FAIL);
    
    bs814_config->clk_pin = BS814_CLK_PIN;
    bs814_config->data_pin = BS814_DATA_PIN;
    
    return ESP_OK;
}

// pcf8574rgtr i2c io expander config

esp_err_t get_pcf8574_config(board_pcf8574_config_t *pcf8574_config)
{
    AUDIO_NULL_CHECK(TAG, pcf8574_config, return ESP_FAIL);
    
    pcf8574_config->i2c_addr = PCF8574_I2C_ADDR;
    pcf8574_config->p1_lcd_reset = PCF8574_P1_LCD_RESET;
    
    return ESP_OK;
}
