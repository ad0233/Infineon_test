#pragma once
#include "esp_err.h"
#include "driver/gpio.h"


#define BSP_PDM_I2S_NUM      (0)           
#define BSP_PDM_MIC_CLK      GPIO_NUM_11     
#define BSP_PDM_MIC_DATA0     GPIO_NUM_10     
#define BSP_PDM_MIC_DATA1     GPIO_NUM_54   

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t init_pdm_mic(void);

#ifdef __cplusplus
}
#endif