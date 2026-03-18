#include "my_bmp580.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "my_bmp580";

/* BMP580 寄存器 (数据手册 v1.2) */
#define BMP580_REG_CHIP_ID    0x01
#define BMP580_REG_ODR_CONFIG 0x37
#define BMP580_REG_OSR_CONFIG 0x36
#define BMP580_REG_TEMP_DATA  0x1D
#define BMP580_REG_PRESS_DATA 0x20

#define BMP580_CHIP_ID_EXPECT 0x50

/* ODR_CONFIG: pwr_mode [1:0] 00=STANDBY 01=FORCED 10=NORMAL 11=CONTINUOUS */
#define BMP580_PWRMODE_FORCED  0x01
/* OSR_CONFIG: press_en=bit6, osr_p[5:3], osr_t[2:0]; 1x=0 .. 128x=7 */
#define BMP580_OSR_CONFIG     0x5B  /* press_en=1, osr_p=8x(3), osr_t=8x(3) */

#define BMP580_CONV_DELAY_MS  25

struct my_bmp580_impl {
    i2c_master_dev_handle_t dev;
    i2c_master_bus_handle_t bus;
};

static int reg_write(my_bmp580_handle_t self, uint8_t reg, uint8_t val, int timeout_ms)
{
    uint8_t buf[2] = { reg, val };
    esp_err_t ret = i2c_master_transmit(self->dev, buf, 2, timeout_ms);
    return (ret == ESP_OK) ? 0 : (int)ret;
}

static int reg_read(my_bmp580_handle_t self, uint8_t reg, uint8_t *out, size_t len, int timeout_ms)
{
    esp_err_t ret = i2c_master_transmit_receive(self->dev, &reg, 1, out, len, timeout_ms);
    return (ret == ESP_OK) ? 0 : (int)ret;
}

int my_bmp580_init(my_bmp580_handle_t *self_out, void *i2c_bus)
{
    if (self_out == NULL || i2c_bus == NULL) {
        return -1;
    }
    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)i2c_bus;

    struct my_bmp580_impl *impl = (struct my_bmp580_impl *)malloc(sizeof(struct my_bmp580_impl));
    if (impl == NULL) {
        return -1;
    }
    memset(impl, 0, sizeof(*impl));
    impl->bus = bus;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MY_BMP580_I2C_ADDR_7BIT,
        .scl_speed_hz = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &impl->dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c add device fail %d", ret);
        free(impl);
        return (int)ret;
    }

    uint8_t chip_id = 0;
    if (reg_read(impl, BMP580_REG_CHIP_ID, &chip_id, 1, 100) != 0) {
        ESP_LOGE(TAG, "read chip_id fail");
        i2c_master_bus_rm_device(impl->dev);
        free(impl);
        return -2;
    }
    if (chip_id != BMP580_CHIP_ID_EXPECT) {
        ESP_LOGE(TAG, "chip_id 0x%02X expect 0x%02X", chip_id, BMP580_CHIP_ID_EXPECT);
        i2c_master_bus_rm_device(impl->dev);
        free(impl);
        return -3;
    }

    if (reg_write(impl, BMP580_REG_OSR_CONFIG, BMP580_OSR_CONFIG, 100) != 0) {
        ESP_LOGE(TAG, "write OSR_CONFIG fail");
        i2c_master_bus_rm_device(impl->dev);
        free(impl);
        return -4;
    }

    *self_out = impl;
    ESP_LOGI(TAG, "BMP580 init ok, addr 0x%02X", MY_BMP580_I2C_ADDR_7BIT);
    return 0;
}

int my_bmp580_deinit(my_bmp580_handle_t self)
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

int my_bmp580_read_block(my_bmp580_handle_t self, float *pressure_pa, float *temp_c, uint32_t timeout_ms)
{
    if (self == NULL) {
        return -1;
    }
    int t_ms = (timeout_ms > 0) ? (int)timeout_ms : 500;

    if (reg_write(self, BMP580_REG_ODR_CONFIG, BMP580_PWRMODE_FORCED, t_ms) != 0) {
        ESP_LOGE(TAG, "FORCED trigger fail");
        return -2;
    }
    vTaskDelay(pdMS_TO_TICKS(BMP580_CONV_DELAY_MS));

    uint8_t raw[6];
    if (reg_read(self, BMP580_REG_TEMP_DATA, raw, 6, t_ms) != 0) {
        ESP_LOGE(TAG, "read data fail");
        return -3;
    }

    /* 温度 24-bit: MSB 0x1F, LSB 0x1E, XLSB 0x1D，分辨率 1/65536 °C，有符号 */
    int32_t tr = (int32_t)((uint32_t)raw[2] << 16 | (uint32_t)raw[1] << 8 | raw[0]);
    if (tr & 0x800000) {
        tr -= 0x1000000;
    }
    if (temp_c) {
        *temp_c = (float)tr / 65536.0f;
    }

    /* 气压 24-bit: MSB 0x22, LSB 0x21, XLSB 0x20，分辨率 1/64 Pa，无符号 */
    uint32_t pr = (uint32_t)raw[5] << 16 | (uint32_t)raw[4] << 8 | raw[3];
    if (pressure_pa) {
        *pressure_pa = (float)pr / 64.0f;
    }

    return 0;
}
