/***************************************************************************//**
* \file ifx_arcsin_f32.c
*
* \brief
* This file contains the implementation for the
* ifx_arcsin_f32 function
*
*******************************************************************************
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
*******************************************************************************/

#include "ifx_sensor_dsp.h"

int32_t ifx_arcsin_f32(float32_t x, float32_t* result)
{
    int32_t status = IFX_SENSOR_DSP_STATUS_OK;
    if (x >= 1.0F)
    {
        *result = PI_2_F32;
    }
    else if (x <= -1.0F)
    {
        *result = -PI_2_F32;
    }
    else
    {
        float32_t y = 0.0F;
        float32_t sqrt_in = 1.0F - (x * x);
        if (dsps_sqrt_f32(&sqrt_in, &y, 1) == ESP_OK)
        {
            *result = atan2f(x, y);
        }
        else
        {
            *result = 0.0F;
            status = IFX_SENSOR_DSP_ARGUMENT_ERROR;
        }
    }
    return status;
}
