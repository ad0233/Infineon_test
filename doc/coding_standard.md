# C/C++ 编码规范

## 语言使用原则

- 保持 C 的写法，可以使用少部分 C++ 的功能
- 不使用 Class、继承等复杂特性
- 参考学习视频：
  1. [有史以来最烂的编程语言 - Lazo Velko](https://www.bilibili.com/video/BV1xYUtB5Evr)
  2. [代码嵌套的致命陷阱 - CodeAesthetic](https://www.bilibili.com/video/BV1kW41zmETw)

## 头文件规范

### 1. 全局变量
- 不得声明全局变量
- 临时快速验证可以，交付不能

### 2. 命名要求
- 全小写，单词间隔使用 `_`
- 格式：`<区域>_<功能>.h`
- 区域示例：`axiarz_hal`, `axiarz_qt`（目前可用 `my`）

### 3. 组件名称
- `<组件名称>` 和头文件名称一样

### 4. 头文件模板

```c
#pragma once  // 保证头文件在编译时只展开一次

// 非必要头文件应当去除

// 高复用要求，主要用于协议代码，方便多端使用（单片机，电脑，后端服务）
// 只添加 std 库，什么环境下都不会错误
#include <stdint.h>
#include <stdio.h>

// 低复用要求, 函数入参一定要用到环境相关的声明（主要用在单一环境）
// 可以再写一份同样的结构体，不一样的名称来避免
#include <esp_log.h>

// 句柄声明 struct <组件名称>_impl* 是在源文件内部实现，这里隐式声明，避免使用空指针
typedef struct <组件名称>_impl* <组件名称>_handle_t;

// 事件回调声明，context 是用于传递上下文
typedef void (*<组件名称>_<事件1名称>_callback_t)(..., void *context, <组件名称>_handle_t self);
typedef void (*<组件名称>_<事件2名称>_callback_t)(..., void *context, <组件名称>_handle_t self);

// CPP 文件兼容声明，很多时候还是需要用到 cpp 一些特性来简化代码
#ifdef __cplusplus
extern "C" {
#endif

/// 初始化，入参是句柄指针的指针,用于返回句柄指针
// 返回最好是 int，表示运行结果，可以参考下常规 Linux 对错误的定义，esp_err_t 本质也是 int，esp-idf/components/esp_common/include/esp_err.h
int <组件名称>_init(<组件名称>_handle_t *self_out, ...);

// 一般不写这个也行，基本都是初始化之后一直用，很少组件需要考虑释放问题
int <组件名称>_deinit(<组件名称>_handle_t self);

//******** 功能函数 ********

// 注册回调函数，建议只允许被注册一次，不然会有很多异步冲突的问题导致异常
int <组件名称>_reg_cb_<事件1名称>(<组件名称>_handle_t self, <组件名称>_<事件1名称>_callback_t func, void *context);
int <组件名称>_reg_cb_<事件2名称>(<组件名称>_handle_t self, <组件名称>_<事件2名称>_callback_t func, void *context);

//******** 功能函数 ********

// 一般避免写非阻塞的函数，但如果要写，就需要给出超时控制入参，一般支持 0xfff
int <组件名称>_<功能名称>_block(<组件名称>_handle_t self, ..., uint32_t timeout_ms);

// 非阻塞的功能函数
int <组件名称>_<功能名称>(<组件名称>_handle_t self, ...);

//******** 获取内部状态 ********

// 返回组件内部状态
enum <组件名称大写缩写>_<状态类别大写> <组件名称>_state(<组件名称>_handle_t self, ...);

//******** 运算类 ********

// 算法类，会有情况失败的，通过指针传递数值上来,
// 如果不需要 <组件名称>_handle_t self, 可以删除
int <组件名称>_math(<组件名称>_handle_t self, float *out, ...);

// 算法类，不会失败的（不会因为入参导致失败，如果入参会导致失败的，返回值最好也是错误情况）
// 如果不需要 <组件名称>_handle_t self, 可以删除
float <组件名称>_math(<组件名称>_handle_t self, ...);

//******** 异步 ********

// 用于节省内存，定时刷新函数，可以让一个线程运行多个组件的 flush，减少线程数量
int <组件名称>_flush(<组件名称>_handle_t self, uint32_t interval_ms);

#ifdef __cplusplus
}
#endif
```

## 源文件规范

### 1. 文件命名
- 名称和对应头文件一样
- 看情况使用 cpp 或者 c 都可以，建议使用 cpp，方便简化代码

### 2. 嵌套层级
- 嵌套不能超过 3 层
- 参考：[代码嵌套的致命陷阱 - CodeAesthetic](https://www.bilibili.com/video/BV1kW41zmETw)

### 3. 源文件模板

```c
#include "<区域>_<功能>.h"

// 标准库
#include <string.h>
#include <vector>
#include <map>

// 环境库
#include "esp_err.h"
#include "esp_log.h"

// 内部库
#include "my_...h"

struct <组件名称>_impl {
    ...
};

// 内部静态函数声明
int s_<功能名称>(...);

// 初始化
// 失败处理部分只是给个例子，单片机不用太严格，因为大多数如果能跑了，就一直都能跑，不释放内存也无所谓的
// 但前面申请内存部分的报错还是要的，内存不足的问题很常见
int <组件名称>_init(<组件名称>_handle_t *self_out, ...) {
    if(self_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    auto* self = (struct <组件名称>_impl*)malloc(sizeof(struct <组件名称>_impl));
    if(self == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    memset(self, 0, sizeof(struct <组件名称>_impl));
    
    // 初始化动作...,失败则释放内存然后返回错误
    // 申请 buf 空间...
    // 外设初始化...
    // 配置外部芯片...
    
    *self_out = self;
    return 0;
}

//...

//...

// 内部静态函数实现
int s_<功能名称>(...) {
    ...
    return 0;
}
```

## 函数命名规范

- 初始化：`<组件名称>_init`
- 去初始化：`<组件名称>_deinit`
- 注册回调：`<组件名称>_reg_cb_<事件名称>`
- 阻塞函数：`<组件名称>_<功能名称>_block`
- 非阻塞函数：`<组件名称>_<功能名称>`
- 获取状态：`<组件名称>_state`
- 异步刷新：`<组件名称>_flush`

## 错误处理

- 返回值使用 `int` 类型，参考 Linux 错误定义
- `esp_err_t` 本质也是 `int`，参考 `esp-idf/components/esp_common/include/esp_err.h`
- 内存申请失败必须返回错误
- 其他初始化失败建议释放内存后返回错误
