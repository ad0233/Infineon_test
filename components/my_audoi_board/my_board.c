#include "my_board.h"
#include "esp_log.h"
#include "esp_check.h"

// 关键：只使用新版驱动头文件
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"

// Codec 相关头文件
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"

static const char *TAG = "AUDIO_BSP";

static esp_codec_dev_handle_t play_dev_handle = NULL;
static i2s_chan_handle_t i2s_tx_chan = NULL;
static i2s_chan_handle_t i2s_rx_chan = NULL;

static i2c_master_bus_handle_t bus_handle = NULL; 

esp_err_t bsp_i2c_init(void)
{
    if (bus_handle != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BSP_I2C_NUM,
        .scl_io_num = BSP_I2C_SCL,
        .sda_io_num = BSP_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, // 如果外部没有上拉电阻，这里必须为true
    };

    return i2c_new_master_bus(&i2c_bus_config, &bus_handle);
}

esp_err_t bsp_audio_init(void)
{
    // 1. 初始化 I2C
    if (bsp_i2c_init() != ESP_OK) {
        ESP_LOGE(TAG, "I2C Init Failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "I2C bus initialized: SDA=GPIO%d, SCL=GPIO%d", BSP_I2C_SDA, BSP_I2C_SCL);

    // 2. 初始化 I2S 
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_NUM, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_chan, &i2s_rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(CODEC_DEFAULT_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(CODEC_DEFAULT_BIT_WIDTH, CODEC_DEFAULT_CHANNEL),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws   = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din  = BSP_I2S_DSIN,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_chan));


    audio_codec_i2s_cfg_t i2s_data_cfg = {
        .port = BSP_I2S_NUM,
        .tx_handle = i2s_tx_chan,
        .rx_handle = i2s_rx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_data_cfg);


    audio_codec_i2c_cfg_t i2c_ctrl_cfg = {
        .port = BSP_I2C_NUM,
        .addr = 0x18,
        .bus_handle = bus_handle,
    };
    ESP_LOGI(TAG, "Attempting to connect ES8311 at I2C address 0x18");
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_ctrl_cfg); 
    if (i2c_ctrl_if == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C control interface for ES8311 at 0x18");
        return ESP_FAIL;
    }

 
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();


    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = i2c_ctrl_if, 
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BSP_POWER_AMP_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es8311_cfg);


    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    play_dev_handle = esp_codec_dev_new(&dev_cfg);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = CODEC_DEFAULT_SAMPLE_RATE,
        .bits_per_sample = 16,
        .channel = I2S_SLOT_MODE_STEREO,
    };
    return esp_codec_dev_open(play_dev_handle, &fs);
}

esp_codec_dev_handle_t bsp_audio_get_play_handle(void)
{
    return play_dev_handle;
}