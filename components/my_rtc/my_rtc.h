#pragma once

// 高复用要求，主要用于协议代码，方便多端使用（单片机，电脑，后端服务）
// 只添加 std 库，什么环境下都不会错误
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// 低复用要求, 函数入参一定要用到环境相关的声明（主要用在单一环境）
#include <esp_err.h>

// 句柄声明 struct my_rtc_impl* 是在源文件内部实现，这里隐式声明，避免使用空指针
typedef struct my_rtc_impl* my_rtc_handle_t;

// CPP 文件兼容声明，很多时候还是需要用到 cpp 一些特性来简化代码
#ifdef __cplusplus
extern "C" {
#endif

/// 初始化，入参是句柄指针的指针,用于返回句柄指针
// 返回最好是 int，表示运行结果，可以参考下常规 Linux 对错误的定义，esp_err_t 本质也是 int
int my_rtc_init(my_rtc_handle_t *self_out);

//******** 功能函数 ********

// 获取RTC时间
int my_rtc_get_time(my_rtc_handle_t self, struct tm *time, bool *valid);

// 设置RTC时间
int my_rtc_set_time(my_rtc_handle_t self, struct tm *time);

// 检查时间是否有效
bool my_rtc_is_time_valid(my_rtc_handle_t self);

// 从NTP同步时间到RTC
int my_rtc_sync_from_ntp(my_rtc_handle_t self);

// 设置NTP同步状态
void my_rtc_set_ntp_synced(my_rtc_handle_t self, bool synced);

// 获取NTP同步状态
bool my_rtc_is_ntp_synced(my_rtc_handle_t self);

#ifdef __cplusplus
}
#endif
