#include "pcf8574.h"
#include "esp_log.h"
#include "board_pins_config.h"

static const char *TAG = "pcf8574";

esp_err_t pcf8574_init(pcf8574_handle_t *handle, i2c_bus_handle_t i2c_bus, uint8_t i2c_addr)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 如果I2C总线句柄为NULL，创建新的I2C总线
    if (i2c_bus == NULL) {
        i2c_config_t i2c_cfg = {0};
        i2c_cfg.mode = I2C_MODE_MASTER;
        i2c_cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
        i2c_cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
        i2c_cfg.master.clk_speed = 100000;

        esp_err_t ret = get_i2c_pins(I2C_NUM_0, &i2c_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get I2C pins");
            return ret;
        }

        i2c_bus = i2c_bus_create(I2C_NUM_0, &i2c_cfg);
        if (i2c_bus == NULL) {
            ESP_LOGE(TAG, "Failed to create I2C bus");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Created new I2C bus");
    } else {
        ESP_LOGI(TAG, "Using existing I2C bus");
    }

    handle->i2c_bus = i2c_bus;
    handle->i2c_addr = i2c_addr;
    handle->port_state = 0xFF;  // 默认所有输出为高

    // 初始化时写入默认值
    esp_err_t ret = pcf8574_write_port(handle, handle->port_state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write initial port state");
        return ret;
    }

    ESP_LOGI(TAG, "PCF8574 initialized at address 0x%02X", i2c_addr);
    return ESP_OK;
}

esp_err_t pcf8574_write_port(pcf8574_handle_t *handle, uint8_t value)
{
    if (handle == NULL || handle->i2c_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = i2c_bus_write_data(handle->i2c_bus, handle->i2c_addr, &value, 1);
    if (ret == ESP_OK) {
        handle->port_state = value;
    }
    return ret;
}

esp_err_t pcf8574_read_port(pcf8574_handle_t *handle, uint8_t *value)
{
    if (handle == NULL || value == NULL || handle->i2c_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // PCF8574读取：发送设备地址，然后读取1字节
    uint8_t dummy = 0;
    return i2c_bus_read_bytes(handle->i2c_bus, handle->i2c_addr, &dummy, 0, value, 1);
}

esp_err_t pcf8574_set_pin(pcf8574_handle_t *handle, uint8_t pin, bool level)
{
    if (handle == NULL || pin > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t new_state = handle->port_state;
    if (level) {
        new_state |= (1 << pin);
    } else {
        new_state &= ~(1 << pin);
    }

    return pcf8574_write_port(handle, new_state);
}

esp_err_t pcf8574_get_pin(pcf8574_handle_t *handle, uint8_t pin, bool *level)
{
    if (handle == NULL || level == NULL || pin > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t port_value;
    esp_err_t ret = pcf8574_read_port(handle, &port_value);
    if (ret == ESP_OK) {
        *level = (port_value >> pin) & 0x01;
    }
    return ret;
}
