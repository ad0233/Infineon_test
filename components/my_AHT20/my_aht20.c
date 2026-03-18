#include "my_aht20.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "my_aht20";

#define AHT20_CMD_SOFT_RESET  0xBA
#define AHT20_CMD_MEASURE     0xAC
#define AHT20_MEASURE_PARAM   0x3300
#define AHT20_MEASURE_DELAY_MS  80
#define AHT20_POWER_ON_DELAY_MS 5
#define AHT20_CRC_POLY  0x31
#define AHT20_CRC_INIT  0xFF

struct my_aht20_impl {
    i2c_master_dev_handle_t dev;
    i2c_master_bus_handle_t bus;
};

static uint8_t calc_crc8(const uint8_t *msg, unsigned len)
{
    uint8_t crc = AHT20_CRC_INIT;
    for (unsigned byte = 0; byte < len; byte++) {
        crc ^= msg[byte];
        for (int i = 8; i > 0; i--) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ AHT20_CRC_POLY;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

int my_aht20_init(my_aht20_handle_t *self_out, void *i2c_bus)
{
    if (self_out == NULL || i2c_bus == NULL) {
        return -1;
    }
    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)i2c_bus;

    struct my_aht20_impl *impl = (struct my_aht20_impl *)malloc(sizeof(struct my_aht20_impl));
    if (impl == NULL) {
        return -1;
    }
    memset(impl, 0, sizeof(*impl));
    impl->bus = bus;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MY_AHT20_I2C_ADDR_7BIT,
        .scl_speed_hz = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &impl->dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c add device fail %d", ret);
        free(impl);
        return (int)ret;
    }

    /* 上电后建议等待 5ms；软复位后稳定 */
    uint8_t reset_cmd = AHT20_CMD_SOFT_RESET;
    ret = i2c_master_transmit(impl->dev, &reset_cmd, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "soft reset tx fail %d", ret);
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    *self_out = impl;
    ESP_LOGI(TAG, "AHT20 init ok, addr 0x%02X", MY_AHT20_I2C_ADDR_7BIT);
    return 0;
}

int my_aht20_deinit(my_aht20_handle_t self)
{
    if (self == NULL) {
        return 0;
    }
    if (self->dev) {
        i2c_master_bus_rm_device(self->dev);
    }
    free(self);
    return 0;
}

int my_aht20_read_block(my_aht20_handle_t self, float *temp_c, float *rh_pct, uint32_t timeout_ms)
{
    if (self == NULL) {
        return -1;
    }

    uint8_t cmd[] = { AHT20_CMD_MEASURE, 0x33, 0x00 };
    esp_err_t ret = i2c_master_transmit(self->dev, cmd, sizeof(cmd), (int)timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "measure cmd fail %d", ret);
        return (int)ret;
    }

    vTaskDelay(pdMS_TO_TICKS(AHT20_MEASURE_DELAY_MS));

    uint8_t raw[7];
    ret = i2c_master_receive(self->dev, raw, sizeof(raw), (int)timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "read fail %d", ret);
        return (int)ret;
    }

    uint8_t crc = calc_crc8(raw, 6);
    if (crc != raw[6]) {
        ESP_LOGE(TAG, "crc fail got 0x%02X expect 0x%02X", raw[6], crc);
        return -2;
    }

    if (raw[0] & 0x80) {
        ESP_LOGW(TAG, "busy bit set, use previous data");
    }

    uint32_t rh_raw = (uint32_t)(raw[1]) << 12 | (uint32_t)(raw[2]) << 4 | (raw[3] >> 4);
    uint32_t t_raw  = (uint32_t)(raw[3] & 0x0F) << 16 | (uint32_t)(raw[4]) << 8 | raw[5];

    if (rh_pct) {
        *rh_pct = (float)rh_raw * 100.f / 1048576.f;
    }
    if (temp_c) {
        *temp_c = (float)t_raw * 200.f / 1048576.f - 50.f;
    }

    return 0;
}
