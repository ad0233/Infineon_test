#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include "driver/i2c.h"
#include "i2c_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PCF8574 handle
 */
typedef struct {
    i2c_bus_handle_t i2c_bus;  /*!< I2C bus handle (shared with RTC/ES7210) */
    uint8_t i2c_addr;
    uint8_t port_state;  /*!< Current port state */
} pcf8574_handle_t;

/**
 * @brief Initialize PCF8574
 *
 * @param handle  PCF8574 handle
 * @param i2c_bus  I2C bus handle (shared with RTC/ES7210, can be NULL to create new)
 * @param i2c_addr  I2C address
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t pcf8574_init(pcf8574_handle_t *handle, i2c_bus_handle_t i2c_bus, uint8_t i2c_addr);

/**
 * @brief Write port state
 *
 * @param handle  PCF8574 handle
 * @param value  Port value (bit 0-7 correspond to P0-P7)
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t pcf8574_write_port(pcf8574_handle_t *handle, uint8_t value);

/**
 * @brief Read port state
 *
 * @param handle  PCF8574 handle
 * @param value  Pointer to store port value
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t pcf8574_read_port(pcf8574_handle_t *handle, uint8_t *value);

/**
 * @brief Set pin level
 *
 * @param handle  PCF8574 handle
 * @param pin  Pin number (0-7)
 * @param level  0 or 1
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t pcf8574_set_pin(pcf8574_handle_t *handle, uint8_t pin, bool level);

/**
 * @brief Get pin level
 *
 * @param handle  PCF8574 handle
 * @param pin  Pin number (0-7)
 * @param level  Pointer to store pin level
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t pcf8574_get_pin(pcf8574_handle_t *handle, uint8_t pin, bool *level);

#ifdef __cplusplus
}
#endif

