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
static esp_codec_dev_handle_t play_dev_handle_2 = NULL;
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

i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void)
{
    return bus_handle;
}

esp_err_t bsp_audio_bus_init(void)
{
    if (bsp_i2c_init() != ESP_OK) {
        ESP_LOGE(TAG, "I2C Init Failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "I2C bus initialized: SDA=GPIO%d, SCL=GPIO%d", BSP_I2C_SDA, BSP_I2C_SCL);

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
    return ESP_OK;
}

esp_err_t bsp_audio_codec_init(void)
{
    if (bus_handle == NULL || i2s_tx_chan == NULL) {
        ESP_LOGE(TAG, "Call bsp_audio_bus_init() first");
        return ESP_FAIL;
    }

    audio_codec_i2s_cfg_t i2s_data_cfg = {
        .port = BSP_I2S_NUM,
        .tx_handle = i2s_tx_chan,
        .rx_handle = i2s_rx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_data_cfg);


    audio_codec_i2c_cfg_t i2c_ctrl_cfg = {
        .port = BSP_I2C_NUM,
        .addr = 0x30,
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
    esp_err_t ret = esp_codec_dev_open(play_dev_handle, &fs);
    if (ret != ESP_OK) {
        return ret;
    }

    /* 第二颗 ES8311，7-bit 地址 0x19 -> 8-bit 写地址 0x32 */
    audio_codec_i2c_cfg_t i2c_ctrl_cfg_2 = {
        .port = BSP_I2C_NUM,
        .addr = 0x32,
        .bus_handle = bus_handle,
    };
    ESP_LOGI(TAG, "Attempting to connect ES8311 #2 at I2C 7-bit addr 0x19");
    const audio_codec_ctrl_if_t *i2c_ctrl_if_2 = audio_codec_new_i2c_ctrl(&i2c_ctrl_cfg_2);
    if (i2c_ctrl_if_2 == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C control for ES8311 at 0x19");
        return ESP_FAIL;
    }
    es8311_codec_cfg_t es8311_cfg_2 = {
        .ctrl_if = i2c_ctrl_if_2,
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
    const audio_codec_if_t *codec_if_2 = es8311_codec_new(&es8311_cfg_2);
    esp_codec_dev_cfg_t dev_cfg_2 = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if_2,
        .data_if = data_if,
    };
    play_dev_handle_2 = esp_codec_dev_new(&dev_cfg_2);
    ret = esp_codec_dev_open(play_dev_handle_2, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 #2 (0x19) open failed");
        return ret;
    }

    esp_codec_dev_write_reg(play_dev_handle, 0x09, 0x0C);

    esp_codec_dev_write_reg(play_dev_handle_2, 0x09, 0x8C);

    ESP_LOGI(TAG, "ES8311 #2 at 0x19 opened OK");
    return ESP_OK;
}

esp_err_t bsp_audio_init(void)
{
    esp_err_t ret = bsp_audio_bus_init();
    if (ret != ESP_OK) {
        return ret;
    }
    return bsp_audio_codec_init();
}

esp_codec_dev_handle_t bsp_audio_get_play_handle(void)
{
    return play_dev_handle;
}

esp_codec_dev_handle_t bsp_audio_get_play_handle_2(void)
{
    return play_dev_handle_2;
}

esp_err_t bsp_i2c_scan(void)
{
    if (bsp_i2c_init() != ESP_OK) {
        return ESP_FAIL;
    }
    const int timeout_ms = 50;
    int found = 0;
    ESP_LOGI(TAG, "I2C scan (SDA=%d, SCL=%d) 0x08~0x77:", BSP_I2C_SDA, BSP_I2C_SCL);
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = BSP_I2C_FREQ_HZ,
        };
        i2c_master_dev_handle_t dev = NULL;
        esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev);
        if (ret != ESP_OK) {
            continue;
        }
        uint8_t dummy = 0;
        ret = i2c_master_transmit(dev, &dummy, 1, timeout_ms);
        i2c_master_bus_rm_device(dev);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  0x%02X", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "I2C scan done, found %d device(s)", found);
    return ESP_OK;
}

esp_err_t bsp_gpio46_set_level(int level)
{
    const gpio_num_t pin = GPIO_NUM_46;
    ESP_ERROR_CHECK(gpio_reset_pin(pin));
    ESP_ERROR_CHECK(gpio_set_direction(pin, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(pin, level ? 1 : 0));
    return ESP_OK;
}

esp_err_t bsp_es8311_write_reg(uint8_t slave_addr, uint8_t reg_addr, uint8_t data)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_bus_handle();
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // 配置临时设备句柄
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = slave_addr,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };

    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) return ret;

    // ES8311 写协议: [SlaveAddr + W] -> [Reg Addr] -> [Data]
    uint8_t write_buf[2] = {reg_addr, data};
    ret = i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), -1);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 Write Err [0x%02X]: Reg 0x%02X, Val 0x%02X, Error: %s", 
                 slave_addr, reg_addr, data, esp_err_to_name(ret));
    }

    i2c_master_bus_rm_device(dev_handle);
    return ret;
}

/**
 * @brief 从 ES8311 读取寄存器
 * @param slave_addr ES8311 的 7位 I2C 地址
 * @param reg_addr   寄存器地址
 * @param data       存放读回数据的指针
 */
esp_err_t bsp_es8311_read_reg(uint8_t slave_addr, uint8_t reg_addr, uint8_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_handle_t bus = bsp_i2c_get_bus_handle();
    if (bus == NULL) return ESP_ERR_INVALID_STATE;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = slave_addr,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };

    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) return ret;

    // ES8311 读协议: 先发送寄存器地址，再接收数据
    ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, 1, -1);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 Read Err [0x%02X]: Reg 0x%02X", slave_addr, reg_addr);
    }

    i2c_master_bus_rm_device(dev_handle);
    return ret;
}