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

#ifdef __cplusplus
}
#endif