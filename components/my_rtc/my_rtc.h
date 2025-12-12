#pragma once

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

int my_rtc_init();
int my_rtc_get_time(struct tm *time, bool *valid);
int my_rtc_set_time(struct tm *time);
bool my_rtc_is_time_valid();
int my_rtc_sync_from_ntp(void);
void my_rtc_set_ntp_synced(bool synced);
bool my_rtc_is_ntp_synced(void);
int pcf8574_write(uint8_t data);
int pcf8574_read(uint8_t *data);
int pcf8574_set_pin(uint8_t pin, bool level);
int pcf8574_get_pin(uint8_t pin, bool *level);
int pcf8574_set_port(uint8_t mask, uint8_t value);
#ifdef __cplusplus
}
#endif