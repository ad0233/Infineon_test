/***********************************************************************************************//**
 * \file xensiv_bgt60trxx_esp.h
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

#ifndef XENSIV_BGT60TRXX_ESP_H_
#define XENSIV_BGT60TRXX_ESP_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"


#include "xensiv_bgt60trxx.h"

/** Default flag for ESP ISR service*/
#define ESP_INTR_FLAG_DEFAULT 0

/** Result code indicating success. */
#define XENSIV_BGT60TRXX_RSLT_OK                    (0)

/** Result code indicating a communication error. */
#define XENSIV_BGT60TRXX_RSLT_ERR_COMM              (1)

/** Result code indicating an unsupported device error. */
#define XENSIV_BGT60TRXX_RSLT_ERR_UNKNOWN_DEVICE    (2)

/** An attempt was made to reconfigure the interrupt pin */
#define XENSIV_BGT60TRXX_RSLT_ERR_INTPIN_INUSE      (3)

/******************************** Type definitions ****************************************/

/**
 * Structure holding the XENSIV(TM) BGT60TRxx ESP interface.
 *
 * Application code should not rely on the specific content of this struct.
 * They are considered an implementation detail which is subject to change
 * between platforms and/or library releases.
 */
typedef struct
{
    spi_device_handle_t spi;
    gpio_num_t selpin;
    gpio_num_t rstpin;
    gpio_num_t irqpin;
    bool use_dma;
} xensiv_bgt60trxx_esp_iface_t;

/**
 * Structure holding the XENSIV(TM) BGT60TRxx ModusToolbox(TM) object.
 * Content initialized using \ref xensiv_bgt60trxx_mtb_init
 *
 */
typedef struct
{
    xensiv_bgt60trxx_t dev; /**< sensor object */
    xensiv_bgt60trxx_esp_iface_t iface; /**< interface object for communication */
} xensiv_bgt60trxx_esp_t;

/******************************* Function prototypes *************************************/

#ifdef __cplusplus
extern "C" {
#endif
 
esp_err_t xensiv_bgt60trxx_esp_init(xensiv_bgt60trxx_esp_t* obj,
                                    spi_device_handle_t spi,
                                    gpio_num_t selpin,
                                    gpio_num_t rstpin,
                                    bool use_dma,
                                    const uint32_t* regs,
                                    size_t len);

esp_err_t xensiv_bgt60trxx_esp_interrupt_init(xensiv_bgt60trxx_esp_t* obj,
                                              uint16_t fifo_limit,
                                              gpio_num_t irqpin,
                                              gpio_isr_t callback,
                                              void* callback_arg);

#ifdef __cplusplus
}
#endif

#endif // ifndef XENSIV_BGT60TRXX_ESP_H_
