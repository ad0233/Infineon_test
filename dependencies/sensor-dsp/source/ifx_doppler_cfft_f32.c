/***************************************************************************//**
* \file ifx_doppler_cfft_f32.c
*
* \brief
* This file contains the implementation for the
* ifx_doppler_cfft_f32 function
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

static void mat_cmplx_trans_f32(const matrix_instance_f32* pSrc, matrix_instance_f32* pDest)
{
    assert(pSrc->pData != NULL);
    assert(pDest->pData != NULL);

    assert(pSrc->numRows == pDest->numCols);
    assert(pSrc->numCols == pDest->numRows);

    float32_t* pOut = pDest->pData;

    for (int i = 0; i < pDest->numRows; ++i)
    {
        for (int j = 0; j < pDest->numCols; ++j) 
        {
            float32_t* pCmplxVal = &pSrc->pData[(pSrc->numCols*j + i)*2];
            *pOut = pCmplxVal[0]; // real part
            pOut++;
            *pOut = pCmplxVal[1]; // imag part
            pOut++;
        }
    }
}

int32_t ifx_doppler_cfft_f32(cfloat32_t* range,
                             cfloat32_t* doppler,
                             bool mean_removal,
                             const float32_t* win,
                             uint16_t num_range_bins,
                             uint16_t num_chirps_per_frame)
{
    assert(range != NULL);
    assert(doppler != NULL);

    if (!dsps_fft2r_initialized)
    {
        if (dsps_fft2r_init_fc32(NULL, num_chirps_per_frame) != ESP_OK)
        {
            return IFX_SENSOR_DSP_ARGUMENT_ERROR;
        }        
    }
    else
    {
        // Check if need bigger table for fft
        if (dsps_fft_w_table_size < num_chirps_per_frame) 
        {
            dsps_fft2r_deinit_fc32();
            if (dsps_fft2r_init_fc32(NULL, num_chirps_per_frame) != ESP_OK)
            {
                return IFX_SENSOR_DSP_ARGUMENT_ERROR;
            }  
        }
    }

    matrix_instance_f32 range_matrix =
    {
        num_chirps_per_frame,
        num_range_bins,
        (float32_t*)range
    };
    matrix_instance_f32 doppler_matrix =
    {
        num_range_bins,
        num_chirps_per_frame,
        (float32_t*)doppler
    };

    mat_cmplx_trans_f32(&range_matrix, &doppler_matrix);

    for (uint32_t range_idx = 0; range_idx < num_range_bins; ++range_idx)
    {
        if (mean_removal)
        {
            ifx_cmplx_mean_removal_f32(doppler, num_chirps_per_frame);
        }

        if (win != NULL)
        {
            // apply to real parts using step size of 2
            dsps_mul_f32(&((float32_t*) doppler)[0], win, &((float32_t*) doppler)[0], num_chirps_per_frame, 2, 1, 2);
            
            // apply to imag parts using step size of 2
            dsps_mul_f32(&((float32_t*) doppler)[1], win, &((float32_t*) doppler)[1], num_chirps_per_frame, 2, 1, 2);
        }

        dsps_fft2r_fc32((float32_t*) doppler, num_chirps_per_frame);
        dsps_bit_rev2r_fc32((float32_t*) doppler, num_chirps_per_frame);

        doppler += num_chirps_per_frame;
    }

    return IFX_SENSOR_DSP_STATUS_OK;
}
