/***************************************************************************//**
* \file ifx_range_fft_f32.c
*
* \brief
* This file contains the implementation for the
* ifx_range_fft_f32 function
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
#include <string.h>
#include "ifx_sensor_dsp.h"

static void copy_f32_to_cf32(float32_t* src, cfloat32_t* dest, uint16_t len) 
{
    for (int i = 0; i < len; i++) 
    {
        dest[i] = src[i] + (0.0f * I);
    }
}

int32_t ifx_range_fft_f32(float32_t* frame,
                          cfloat32_t* range,
                          bool mean_removal,
                          const float32_t* win,
                          uint16_t num_samples_per_chirp,
                          uint16_t num_chirps_per_frame)
{
    assert(frame != NULL);
    assert(range != NULL);

    // static arr to store intermediate result of cfft
    // unsafe to return directly to range param as expected size is halved
    static cfloat32_t* cfft_temp_arr;
    static bool isMemAlloc = false;
    static uint16_t allocSize = 0;

    if (!isMemAlloc) 
    {
        cfft_temp_arr = (cfloat32_t*) malloc(num_samples_per_chirp*sizeof(cfloat32_t));
        allocSize = num_samples_per_chirp;
        isMemAlloc = true;        
    }
    else
    {
        /* resize cfft temp array if previous allocation is too small */
        if (allocSize < num_samples_per_chirp) 
        {
            cfft_temp_arr = (cfloat32_t*) realloc(cfft_temp_arr, num_samples_per_chirp*sizeof(cfloat32_t));
            allocSize = num_samples_per_chirp;
        }
    }

    if (!dsps_fft2r_initialized)
    {
        if (dsps_fft2r_init_fc32(NULL, num_samples_per_chirp) != ESP_OK)
        {
            return IFX_SENSOR_DSP_ARGUMENT_ERROR;
        }        
    }
    else
    {
        // Check if need bigger table for fft
        if (dsps_fft_w_table_size < num_samples_per_chirp) 
        {
            dsps_fft2r_deinit_fc32();
            if (dsps_fft2r_init_fc32(NULL, num_samples_per_chirp) != ESP_OK)
            {
                return IFX_SENSOR_DSP_ARGUMENT_ERROR;
            }  
        }
    }

    for (uint32_t chirp_idx = 0; chirp_idx < num_chirps_per_frame; ++chirp_idx)
    {
        if (mean_removal)
        {
            ifx_mean_removal_f32(frame, num_samples_per_chirp);
        }

        if (win != NULL)
        {
            dsps_mul_f32(frame, win, frame, num_samples_per_chirp, 1, 1, 1);
        }

        copy_f32_to_cf32(frame,  cfft_temp_arr, num_samples_per_chirp);
        dsps_fft2r_fc32((float32_t*) cfft_temp_arr, num_samples_per_chirp);
        dsps_bit_rev2r_fc32((float32_t*) cfft_temp_arr, num_samples_per_chirp);

        // Copy first half of cfft result to range arr from temp arr 
        // temp_arr is needed as cfft returning 2x more than rfft
        // directly return to range arr will cause overflow
        memcpy(range, cfft_temp_arr, sizeof(cfloat32_t)*(num_samples_per_chirp / 2U));    
        CIMAG_F32(range[0]) = 0.0f;

        frame += num_samples_per_chirp;
        range += (num_samples_per_chirp / 2U);
    }

    return IFX_SENSOR_DSP_STATUS_OK;
}
