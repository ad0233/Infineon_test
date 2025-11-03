#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ble_data_recv_callback_t)(const uint8_t *data, uint16_t len);

void my_ble_init(void);
void my_ble_register_recv_callback(ble_data_recv_callback_t callback);
int my_ble_send_data(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif