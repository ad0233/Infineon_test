#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void my_mqtt_init(const char* broker_uri, const char* server_cert, const char* client_cert, const char* client_key);

#ifdef __cplusplus
}
#endif