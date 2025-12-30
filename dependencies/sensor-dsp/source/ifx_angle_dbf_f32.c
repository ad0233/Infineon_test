/***************************************************************************//**
* \file ifx_angle_dbf_f32.c
*
* \brief
* This file contains the implementation for the
* ifx_angle_dbf_f32 function
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

/* This assumes that M*N mul with N*K matrix and has no safety checks 
 * Complex float data is alternating between real and imaginary values */
static void mat_cmplx_mult_f32(const matrix_instance_f32* pSrcA,
                   const matrix_instance_f32* pSrcB,
                   matrix_instance_f32* pDest)
{
    float32_t* pOut = pDest->pData;
    assert(pOut != NULL);

    for(int i = 0; i< pSrcA->numRows; i++)
    {
        for(int j = 0; j < pSrcB->numCols; j++)
        {
            float32_t realRes = 0.0f;
            float32_t imagRes = 0.0f;

            for(int k = 0; k < pSrcB->numRows; k++)
            {
                float32_t* ptrA = &pSrcA->pData[((i*pSrcA->numCols)+k)*2]; // Pointer to complex val in A
                float32_t* ptrB = &pSrcB->pData[((k*pSrcB->numCols)+j)*2]; // Pointer to complex val in B
                realRes += (ptrA[0] * ptrB[0]) - (ptrA[1] * ptrB[1]); // Accumulate real val
                imagRes += (ptrA[0] * ptrB[1]) + (ptrA[1] * ptrB[0]); // Accumulate imag val
            }
            *pOut = realRes;
            pOut++;
            *pOut = imagRes;
            pOut++;
        }
    }
}

int32_t ifx_angle_dbf_f32(const matrix_instance_f32* pInput,
                             const matrix_instance_f32* pSteering,
                             matrix_instance_f32* pOutput)
{
    // corresponds to number of antennas
    assert(pSteering->numCols == pInput->numRows);

    // corresponds to number of samples
    assert(pInput->numCols == pOutput->numCols);

    // corresponds to number of angles
    assert(pSteering->numRows == pOutput->numRows);

    mat_cmplx_mult_f32(pSteering, pInput, pOutput);

    return IFX_SENSOR_DSP_STATUS_OK;
}
