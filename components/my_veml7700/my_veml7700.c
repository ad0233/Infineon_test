#include "my_veml7700.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "my_veml7700";

/* VEML7700 寄存器 (数据手册 / Vishay) */
#define VEML7700_REG_ALS_CONFIG   0x00
#define VEML7700_REG_ALS_THRESH_H  0x01
#define VEML7700_REG_ALS_THRESH_L  0x02
#define VEML7700_REG_POWER_SAVE    0x03
#define VEML7700_REG_ALS_DATA      0x04
#define VEML7700_REG_WHITE_DATA    0x05
#define VEML7700_REG_INT_STATUS    0x06

/* ALS_CONFIG: bit0=shutdown(0=run), bit1=int_en, bit4-5=persistence, bit6-9=IT, bit11-12=gain */
#define VEML7700_SHDN_RUN      0
#define VEML7700_GAIN_1_8      2   /* bits 11-12: 1/8x */
#define VEML7700_IT_100MS      0   /* bits 6-9: 100ms */
#define VEML7700_PERS_1        0
/* Config = (GAIN_1_8<<11) | (IT_100MS<<6) | (PERS_1<<4) | (0<<1) | SHDN_RUN = 0x1800 */
#define VEML7700_ALS_CONFIG_DEFAULT  0x1800

/* 分辨率: 0.0036 lx/count @ 800ms, 2x gain; 当前 IT=100ms, Gain=1/8 → 0.0036*(800/100)*(2/0.125)=0.4608 */
#define VEML7700_RESOLUTION_IT100_GAIN1_8  (0.4608f)

#define VEML7700_POWER_ON_DELAY_MS  5
#define VEML7700_FIRST_READ_DELAY_MS 110

struct my_veml7700_impl {
    i2c_master_dev_handle_t dev;
    i2c_master_bus_handle_t bus;
    bool first_read;
};

static int reg_write16(my_veml7700_handle_t self, uint8_t reg, uint16_t val, int timeout_ms)
{
    uint8_t buf[3] = { reg, (uint8_t)(val & 0xFF), (uint8_t)(val >> 8) };
    esp_err_t ret = i2c_master_transmit(self->dev, buf, 3, timeout_ms);
    return (ret == ESP_OK) ? 0 : (int)ret;
}

static int reg_read16(my_veml7700_handle_t self, uint8_t reg, uint16_t *out, int timeout_ms)
{
    uint8_t buf[2];
    esp_err_t ret = i2c_master_transmit_receive(self->dev, &reg, 1, buf, 2, timeout_ms);
    if (ret != ESP_OK) {
        return (int)ret;
    }
    *out = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return 0;
}

int my_veml7700_init(my_veml7700_handle_t *self_out, void *i2c_bus)
{
    if (self_out == NULL || i2c_bus == NULL) {
        return -1;
    }
    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)i2c_bus;

    struct my_veml7700_impl *impl = (struct my_veml7700_impl *)malloc(sizeof(struct my_veml7700_impl));
    if (impl == NULL) {
        return -1;
    }
    memset(impl, 0, sizeof(*impl));
    impl->bus = bus;
    impl->first_read = true;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MY_VEML7700_I2C_ADDR_7BIT,
        .scl_speed_hz = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &impl->dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c add device fail %d", ret);
        free(impl);
        return (int)ret;
    }

    /* 上电后先关 shutdown，等待 2.5ms+ 再测量；配置 GAIN=1/8, IT=100ms */
    if (reg_write16(impl, VEML7700_REG_ALS_CONFIG, VEML7700_ALS_CONFIG_DEFAULT, 100) != 0) {
        ESP_LOGE(TAG, "write ALS_CONFIG fail");
        i2c_master_bus_rm_device(impl->dev);
        free(impl);
        return -2;
    }
    vTaskDelay(pdMS_TO_TICKS(VEML7700_POWER_ON_DELAY_MS));

    *self_out = impl;
    ESP_LOGI(TAG, "VEML7700 init ok, addr 0x%02X", MY_VEML7700_I2C_ADDR_7BIT);
    return 0;
}

int my_veml7700_deinit(my_veml7700_handle_t self)
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

int my_veml7700_read_block(my_veml7700_handle_t self, float *lux_out, uint32_t timeout_ms)
{
    if (self == NULL) {
        return -1;
    }
    int t_ms = (timeout_ms > 0) ? (int)timeout_ms : 500;

    if (self->first_read) {
        vTaskDelay(pdMS_TO_TICKS(VEML7700_FIRST_READ_DELAY_MS));
        self->first_read = false;
    }

    uint16_t raw;
    if (reg_read16(self, VEML7700_REG_ALS_DATA, &raw, t_ms) != 0) {
        ESP_LOGE(TAG, "read ALS_DATA fail");
        return -2;
    }

    float lux = VEML7700_RESOLUTION_IT100_GAIN1_8 * (float)raw;
    if (lux_out) {
        *lux_out = lux;
    }
    return 0;
}
