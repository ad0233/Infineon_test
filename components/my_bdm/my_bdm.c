#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"

#include "my_bdm.h"
#include "driver/i2s_pdm.h"

static const char *TAG = "PDM_INIT";
i2s_chan_handle_t rx_handle = NULL;

esp_err_t init_pdm_mic(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_PDM_I2S_NUM, I2S_ROLE_MASTER);
    
    // 创建新的接收通道
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    /* 2. 配置 PDM 接收模式 */
    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(16000), // 采样率 16kHz
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .clk = BSP_PDM_MIC_CLK,
#if SOC_I2S_PDM_MAX_RX_LINES >= 2
            .dins = {
                BSP_PDM_MIC_DATA0,
                BSP_PDM_MIC_DATA1,
            },
#else
            .din = BSP_PDM_MIC_DATA0,
#endif
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };

#if SOC_I2S_PDM_MAX_RX_LINES >= 2
    // 启用所有槽位，支持多个麦克风
    pdm_rx_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
    pdm_rx_cfg.slot_cfg.slot_mask = I2S_PDM_LINE_SLOT_ALL;
#endif

    /* 3. 初始化并使能 */
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_handle, &pdm_rx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    ESP_LOGI(TAG, "PDM 麦克风初始化成功，启用了 %d 个麦克风", SOC_I2S_PDM_MAX_RX_LINES >= 2 ? 2 : 1);
    return ESP_OK;
}