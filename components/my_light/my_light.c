#include "my_light.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "my_light";

/* BH1750 指令 (手册) */
#define BH1750_POWER_DOWN   0x00
#define BH1750_POWER_ON     0x01
#define BH1750_RESET        0x07
#define BH1750_CONT_H_RES   0x10  /* 连续 H 分辨率，1lx，typ 120ms */
#define BH1750_CONT_H_RES2  0x11  /* 0.5lx */
#define BH1750_CONT_L_RES   0x13  /* 4lx, typ 16ms */
#define BH1750_ONE_H_RES    0x20
#define BH1750_ONE_L_RES    0x23

#define BH1750_LUX_SCALE    (1.2f)

struct my_light_impl {
    i2c_master_dev_handle_t dev;
    i2c_master_bus_handle_t bus;
};

int my_light_init(my_light_handle_t *self_out, void *i2c_bus)
{
    if (self_out == NULL || i2c_bus == NULL) {
        return -1;
    }
    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)i2c_bus;

    struct my_light_impl *impl = (struct my_light_impl *)malloc(sizeof(struct my_light_impl));
    if (impl == NULL) {
        return -1;
    }
    memset(impl, 0, sizeof(*impl));
    impl->bus = bus;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MY_LIGHT_I2C_ADDR_7BIT,
        .scl_speed_hz = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &impl->dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c add device fail %d", ret);
        free(impl);
        return (int)ret;
    }

    uint8_t cmd = BH1750_POWER_ON;
    ret = i2c_master_transmit(impl->dev, &cmd, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "power on fail %d", ret);
        i2c_master_bus_rm_device(impl->dev);
        free(impl);
        return (int)ret;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    *self_out = impl;
    ESP_LOGI(TAG, "BH1750 init ok, addr 0x%02X", MY_LIGHT_I2C_ADDR_7BIT);
    return 0;
}

int my_light_deinit(my_light_handle_t self)
{
    if (self == NULL) {
        return 0;
    }
    uint8_t cmd = BH1750_POWER_DOWN;
    i2c_master_transmit(self->dev, &cmd, 1, 100);
    if (self->dev) {
        i2c_master_bus_rm_device(self->dev);
    }
    free(self);
    return 0;
}

static int read_raw_to_lux(my_light_handle_t self, uint8_t *raw, float *lux_out, uint32_t timeout_ms)
{
    esp_err_t ret = i2c_master_receive(self->dev, raw, 2, (int)timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "read fail %d", ret);
        return (int)ret;
    }
    uint32_t val = (uint32_t)(raw[0] << 8) | raw[1];
    float lux = (float)val / BH1750_LUX_SCALE;
    if (lux_out) {
        *lux_out = lux;
    }
    return 0;
}

int my_light_read_oneshot(my_light_handle_t self, float *lux_out, uint32_t timeout_ms)
{
    if (self == NULL) {
        return -1;
    }
    uint8_t cmd = BH1750_ONE_H_RES;
    esp_err_t ret = i2c_master_transmit(self->dev, &cmd, 1, (int)timeout_ms);
    if (ret != ESP_OK) {
        return (int)ret;
    }
    vTaskDelay(pdMS_TO_TICKS(180));

    uint8_t raw[2];
    return read_raw_to_lux(self, raw, lux_out, timeout_ms);
}

int my_light_start_continuous(my_light_handle_t self)
{
    if (self == NULL) {
        return -1;
    }
    uint8_t cmd = BH1750_CONT_H_RES;
    esp_err_t ret = i2c_master_transmit(self->dev, &cmd, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start continuous fail %d", ret);
        return (int)ret;
    }
    vTaskDelay(pdMS_TO_TICKS(130));
    return 0;
}

int my_light_read_continuous(my_light_handle_t self, float *lux_out, uint32_t timeout_ms)
{
    if (self == NULL) {
        return -1;
    }
    uint8_t raw[2];
    return read_raw_to_lux(self, raw, lux_out, timeout_ms);
}

int my_light_read_block(my_light_handle_t self, float *lux_out, uint32_t timeout_ms)
{
    return my_light_read_oneshot(self, lux_out, timeout_ms);
}
