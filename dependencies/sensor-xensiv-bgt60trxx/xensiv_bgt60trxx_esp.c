/***********************************************************************************************//**
 * \file xensiv_bgt60trxx_esp.c
 *
 * \brief
 * This file contains the MTB platform functions implementation
 * for interacting with the XENSIV(TM) BGT60TRxx 60GHz FMCW radar sensors.
 *
 ***************************************************************************************************
 * \copyright
 * Copyright 2022 Infineon Technologies AG
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **************************************************************************************************/

#include "xensiv_bgt60trxx_esp.h"
#include "xensiv_bgt60trxx_platform.h"

#include <inttypes.h>
#include <string.h>

#define SAMPLE_REM_0_BIT_MASK       0xFFF0
#define SAMPLE_REM_0_BIT_SHIFT      4U
#define SAMPLE_REM_1_MSB_BIT_MASK   0x000F
#define SAMPLE_REM_1_LSB_BIT_MASK   0xFF00
#define SAMPLE_REM_1_MSB_BIT_SHIFT  8U
#define SAMPLE_REM_1_LSB_BIT_SHIFT  8U
#define SAMPLE_REM_2_MSB_BIT_MASK   0x00FF
#define SAMPLE_REM_2_LSB_BIT_MASK   0xF000
#define SAMPLE_REM_2_MSB_BIT_SHIFT  4U
#define SAMPLE_REM_2_LSB_BIT_SHIFT  12U
#define SAMPLE_REM_3_BIT_MASK       0x0FFF

/* Static Function Prototypes */
static void unpack_samples(uint16_t* rx_data, uint32_t rx_data_sample_relative_idx, uint32_t sample_num);
static uint32_t extract_sample(uint16_t* rx_data, uint32_t rx_data_idx, uint32_t sample_idx);
static uint32_t div_ceil_u32(uint32_t numerator, uint32_t denominator);
static uint32_t div_floor_u32(uint32_t numerator, uint32_t denominator);

/*******************************************************************************
 * Public interface implementation
 ********************************************************************************/
esp_err_t xensiv_bgt60trxx_esp_init(xensiv_bgt60trxx_esp_t* obj,
                                    spi_device_handle_t spi,
                                    gpio_num_t selpin,
                                    gpio_num_t rstpin,
                                    bool use_dma,
                                    const uint32_t* regs,
                                    size_t len)
{
    xensiv_bgt60trxx_platform_assert(obj != NULL);
    xensiv_bgt60trxx_platform_assert(spi != NULL);
    xensiv_bgt60trxx_platform_assert(selpin != GPIO_NUM_NC);
    xensiv_bgt60trxx_platform_assert(rstpin != GPIO_NUM_NC);
    xensiv_bgt60trxx_platform_assert(regs != NULL);

    xensiv_bgt60trxx_esp_iface_t* iface = &obj->iface;

    iface->spi = spi;
    iface->selpin = selpin;
    iface->rstpin = rstpin;
    iface->irqpin = GPIO_NUM_NC;
    iface->use_dma = use_dma;

    gpio_config_t sel_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_DEF_OUTPUT,
        .pin_bit_mask = (1ULL << selpin) | (1ULL << rstpin),
        .pull_down_en = 0,
        .pull_up_en = 1
    };

    esp_err_t rslt = gpio_config(&sel_conf);
    xensiv_bgt60trxx_t* dev = &obj->dev;

    /* perform device hard reset before beginning init via SPI */
    xensiv_bgt60trxx_platform_rst_set(iface, true);
    xensiv_bgt60trxx_platform_spi_cs_set(iface, true);
    xensiv_bgt60trxx_platform_delay(1U);
    xensiv_bgt60trxx_platform_rst_set(iface, false);
    xensiv_bgt60trxx_platform_delay(1U);
    xensiv_bgt60trxx_platform_rst_set(iface, true);
    xensiv_bgt60trxx_platform_delay(1U);


    if (rslt == ESP_OK)
    {
        int32_t res = xensiv_bgt60trxx_init(dev, iface, false);
        if (res != XENSIV_BGT60TRXX_STATUS_OK)
        {
            printf("Failed init\n");
            rslt = ESP_FAIL;
        }
    }

    if (rslt == ESP_OK)
    {
        if (iface->rstpin != GPIO_NUM_NC)
        {
            xensiv_bgt60trxx_hard_reset(dev);
        }
    }

    if (rslt == ESP_OK)
    {
        int32_t res = xensiv_bgt60trxx_config(dev, regs, len);
        if (res != XENSIV_BGT60TRXX_STATUS_OK) 
        {
            printf("Failed config\n");
            rslt = ESP_FAIL;
        }
    }

    return rslt;
}


esp_err_t xensiv_bgt60trxx_esp_interrupt_init(xensiv_bgt60trxx_esp_t* obj,
                                              uint16_t fifo_limit,
                                              gpio_num_t irqpin,
                                              gpio_isr_t callback,
                                              void* callback_arg)
{
    xensiv_bgt60trxx_platform_assert(obj != NULL);
    
    xensiv_bgt60trxx_t* dev = &obj->dev;
    xensiv_bgt60trxx_esp_iface_t* iface = &obj->iface;

    gpio_config_t conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << irqpin),
        .pull_down_en = 0,
        .pull_up_en = 0
    };

    if (iface->irqpin == GPIO_NUM_NC) {
        esp_err_t result = gpio_config(&conf);
        if (result == ESP_OK) 
        {
            result = gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
        }

        if (result == ESP_OK)
        {            
            result = gpio_isr_handler_add(irqpin, callback, (void*) irqpin);
            iface->irqpin = irqpin;
        }
        else
        {
            return ESP_FAIL;
        }        
    } 

    return (xensiv_bgt60trxx_set_fifo_limit(dev, fifo_limit) == XENSIV_BGT60TRXX_STATUS_OK)
            ? ESP_OK
            : ESP_FAIL;
}

/*******************************************************************************
 * Platform functions implementation
 ********************************************************************************/
int32_t xensiv_bgt60trxx_platform_spi_transfer(void* iface,
                                               uint8_t* tx_data,
                                               uint8_t* rx_data,
                                               uint32_t len)
{
    xensiv_bgt60trxx_platform_assert(iface != NULL);
    xensiv_bgt60trxx_platform_assert((tx_data != NULL) || (rx_data != NULL));


    const xensiv_bgt60trxx_esp_iface_t* esp_iface = iface; 

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = tx_data;
    t.rx_buffer = rx_data;

    esp_err_t status = spi_device_polling_transmit(esp_iface->spi, &t);

    return (status == ESP_OK)
            ? XENSIV_BGT60TRXX_STATUS_OK
            : XENSIV_BGT60TRXX_STATUS_COM_ERROR;
}

int32_t xensiv_bgt60trxx_platform_spi_fifo_read(void* iface,
                                                uint16_t* rx_data,
                                                uint32_t len)
{
    
    xensiv_bgt60trxx_platform_assert(iface != NULL);
    xensiv_bgt60trxx_platform_assert(rx_data != NULL);

    const xensiv_bgt60trxx_esp_iface_t* esp_iface = iface;    
    esp_err_t status = ESP_OK;       
    
    
    /* Single DMA transaction implementation with in-place unpacking */
    if (esp_iface->use_dma)
    {          
        spi_transaction_t t;    
        memset(&t, 0, sizeof(t));
        t.length = len * 12;
        t.rxlength = len * 12;
        t.tx_buffer = NULL;
        t.rx_buffer = rx_data; 

        spi_device_acquire_bus(esp_iface->spi, portMAX_DELAY);
        status = spi_device_polling_transmit(esp_iface->spi, &t);
        spi_device_release_bus(esp_iface->spi);
        if (status == ESP_OK)
        {
            // Get the relative buffer idx of the last packed sample
            uint32_t rx_data_sample_relative_idx =  div_ceil_u32(len * 12, 16) - 1; 

            unpack_samples(rx_data, rx_data_sample_relative_idx, len);        
        }
    }    
    else /* Using FIFO with looped transaction */
    {                  
        uint32_t min_trans_num;
        uint32_t max_single_trans_sample_num;
        uint32_t leftover_trans_sample_num;

        if (len * 12 <= SOC_SPI_MAXIMUM_BUFFER_SIZE * 8) 
        {
            // Samples fits into single transaction            
            max_single_trans_sample_num = len;
            leftover_trans_sample_num = len; 
            min_trans_num = 1;
        }
        else 
        {
            max_single_trans_sample_num = div_floor_u32(SOC_SPI_MAXIMUM_BUFFER_SIZE * 8, 12);
            leftover_trans_sample_num = len % max_single_trans_sample_num;
            min_trans_num = div_ceil_u32(len, max_single_trans_sample_num);
        }
        
        spi_transaction_t t;    
        memset(&t, 0, sizeof(t));
        t.length = max_single_trans_sample_num * 12;
        t.rxlength = max_single_trans_sample_num * 12;
        t.tx_buffer = NULL;

        /* Do looped transactions of up to maximum allowable size for FIFO */
        spi_device_acquire_bus(esp_iface->spi, portMAX_DELAY);
        for (uint32_t trans_num = 0; trans_num < min_trans_num; ++trans_num) 
        {    
            if ((trans_num == min_trans_num - 1) && (leftover_trans_sample_num != 0))
            {
                t.length = leftover_trans_sample_num * 12;
                t.rxlength = leftover_trans_sample_num * 12;
                t.tx_buffer = NULL;                        
            }
            
            t.rx_buffer = rx_data + trans_num * max_single_trans_sample_num;    
            status = spi_device_polling_transmit(esp_iface->spi, &t);         

            if (status != ESP_OK) break;
        }
        spi_device_release_bus(esp_iface->spi);

        if (status == ESP_OK)
        {
            uint32_t rx_data_sample_relative_idx;

            // Unpack samples in their respective transaction buffer blocks in-place
            for (uint32_t trans_num = 0; trans_num < min_trans_num; ++trans_num) 
            {   
                if ((trans_num == min_trans_num - 1) && (leftover_trans_sample_num != 0))
                {
                    // Get the relative buffer idx of the last packed sample in transaction buffer block
                    rx_data_sample_relative_idx =  div_ceil_u32(leftover_trans_sample_num * 12, 16) - 1;
                    unpack_samples(rx_data + trans_num * max_single_trans_sample_num,
                                   rx_data_sample_relative_idx, leftover_trans_sample_num);
                }
                else
                {
                    // Get the relative buffer idx of the last packed sample in transaction buffer block
                    rx_data_sample_relative_idx =  div_ceil_u32(max_single_trans_sample_num * 12, 16) - 1;
                    unpack_samples(rx_data + trans_num * max_single_trans_sample_num,
                                   rx_data_sample_relative_idx, max_single_trans_sample_num);
                }            
            }
        }        
    }    

    return (status == ESP_OK)
            ? XENSIV_BGT60TRXX_STATUS_OK 
            : XENSIV_BGT60TRXX_STATUS_COM_ERROR;
}


void xensiv_bgt60trxx_platform_rst_set(const void* iface, bool val)
{
    xensiv_bgt60trxx_platform_assert(iface != NULL);

    const xensiv_bgt60trxx_esp_iface_t* esp_iface = iface;

    xensiv_bgt60trxx_platform_assert(esp_iface->rstpin != GPIO_NUM_NC);

    gpio_set_level(esp_iface->rstpin, (uint32_t) val);
}


void xensiv_bgt60trxx_platform_spi_cs_set(const void* iface, bool val)
{
    xensiv_bgt60trxx_platform_assert(iface != NULL);

    const xensiv_bgt60trxx_esp_iface_t* esp_iface = iface;

    xensiv_bgt60trxx_platform_assert(esp_iface->selpin != GPIO_NUM_NC);

    gpio_set_level(esp_iface->selpin, (uint32_t) val);
}


void xensiv_bgt60trxx_platform_delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}


uint32_t xensiv_bgt60trxx_platform_word_reverse(uint32_t x)
{  
    return (uint32_t) __bswap32(x);
}


void xensiv_bgt60trxx_platform_assert(bool expr)
{
    if (!expr) 
    {
        ESP_ERROR_CHECK(ESP_FAIL);
    }
}

/*******************************************************************************
 * Static functions
 ********************************************************************************/
/**
 * Inplace extraction of packed 12-bit sample data to expected 
 * location in buffer with 16-bit data width. This function assumes
 * working backwards from the last sample to the first sample in packed buffer 
 * 
 * @param rx_data Buffer used to receive the 12-bit samples
 * @param rx_data_sample_relative_idx Relative buffer index of the last packed sample
 * @param sample_num Number of samples to be unpacked
 * 
 * @return The next expected rx_data_idx
 */
static void unpack_samples(uint16_t* rx_data, uint32_t rx_data_sample_relative_idx, uint32_t sample_num)
{
    // Byte swap all packed samples
    for (uint32_t rx_data_idx = 0; rx_data_idx <= rx_data_sample_relative_idx; ++rx_data_idx) 
    {
        rx_data[rx_data_idx] = __bswap16(rx_data[rx_data_idx]); 
    }

    // Set idx to last sample location in packed rx data for in-place unpacking
    uint32_t rx_data_idx = rx_data_sample_relative_idx;
    uint32_t sample_idx_max = sample_num - 1;

    // This has same logic as decrement loop but retains uint32_t for sample_idx    
    for (uint32_t sample_idx = 0; sample_idx <= sample_idx_max; ++sample_idx)
    {            
        rx_data_idx = extract_sample(rx_data, rx_data_idx, sample_idx_max - sample_idx);
    }    
}

/**
 * Inplace extraction of packed 12-bit sample data to expected 
 * location in buffer with 16-bit data width. This function assumes
 * working backwards from the last sample to the first sample in packed buffer 
 * 
 * @param rx_data Buffer used to receive the 12-bit samples
 * @param rx_data_idx Current index of rx_data, should refer to location of sample
 * @param sample_idx Current sample index to be extracted
 * 
 * @return The next expected rx_data_idx
 */
static uint32_t extract_sample(uint16_t* rx_data, uint32_t rx_data_idx, uint32_t sample_idx)
{
    uint32_t remainder = sample_idx % 4;
    
    if (remainder == 0) // Sample 0, 4, 8, ...
    {
        rx_data[sample_idx] = (uint16_t) ((rx_data[rx_data_idx] & SAMPLE_REM_0_BIT_MASK) >> SAMPLE_REM_0_BIT_SHIFT);       

        /* Prevent returning overflow, idx=0 is first sample */
        return (rx_data_idx == 0) 
                ? 0
                : rx_data_idx - 1;
    }
    else if (remainder == 1) // Sample 1, 5, 9, ...
    {        
        rx_data[sample_idx] = (uint16_t) (((rx_data[rx_data_idx-1] & SAMPLE_REM_1_MSB_BIT_MASK) << SAMPLE_REM_1_MSB_BIT_SHIFT) 
                                        + ((rx_data[rx_data_idx] & SAMPLE_REM_1_LSB_BIT_MASK) >> SAMPLE_REM_1_LSB_BIT_SHIFT));
        return rx_data_idx - 1;
    }
    else if (remainder == 2) // Sample 2, 6, 10, ...
    {
        rx_data[sample_idx] = (uint16_t) (((rx_data[rx_data_idx-1] & SAMPLE_REM_2_MSB_BIT_MASK) << SAMPLE_REM_2_MSB_BIT_SHIFT) 
                                        + ((rx_data[rx_data_idx] & SAMPLE_REM_2_LSB_BIT_MASK) >> SAMPLE_REM_2_LSB_BIT_SHIFT));
        return rx_data_idx - 1;
    }
    else // Sample 3, 7, 11, ...
    {
        rx_data[sample_idx] = (uint16_t) (rx_data[rx_data_idx] & SAMPLE_REM_3_BIT_MASK);
        return rx_data_idx;
    }
}

/* Simple u32 ceil */
static uint32_t div_ceil_u32(uint32_t numerator, uint32_t denominator) 
{
    return (uint32_t) (numerator + denominator - 1) / denominator;
}

/* Simple u32 floor */
static uint32_t div_floor_u32(uint32_t numerator, uint32_t denominator) 
{
    return (uint32_t) (numerator / denominator);
}