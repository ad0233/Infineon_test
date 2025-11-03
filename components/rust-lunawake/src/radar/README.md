# 雷达协议模块

## 概述

本模块按公司组织不同的雷达协议实现，专为ESP32移植设计，不依赖Qt。

## 架构设计

### 目录结构
```
src/radar/
├── mod.rs           # 模块导出
├── common.rs        # 通用接口和基础类型
├── airtouch.rs      # 艾睿电子雷达协议
└── README.md        # 本文档
```

### 设计原则

1. **按公司分类**: 每个公司的雷达协议独立实现
2. **通用接口**: 提供统一的`RadarFrame`、`FrameParser`、`RadarDevice`接口
3. **可扩展性**: 易于添加新的公司协议
4. **ESP32友好**: 无Qt依赖，适合嵌入式移植
5. **严格错误处理**: 使用`Result`类型处理错误，不返回默认值
6. **简洁设计**: 避免不必要的封装，直接使用标准库方法

## 当前支持的协议

### 艾睿电子 (Airtouch)
- **主动上报协议** (`ProtocolType::AirtouchActiveReport`)
  - 帧头: `0x5A`
  - 格式: `HEAD(1) + LEN(1) + PAYLOAD(N) + CHECK(1)`
  - 校验: 8位校验和
  - 字节序: 小端序

- **AT6010芯片协议** (`ProtocolType::AirtouchAt6010`)
  - 发送帧头: `0x58`, 回复帧头: `0x59`
  - 格式: `HEAD(1) + CMD(1) + LEN(1) + PARAMS(N) + CHECK(2)`
  - 校验: 16位校验和
  - 字节序: 小端序

## 使用示例

### 基本使用

```rust
use rust_utils::radar::*;
use rust_utils::radar::airtouch::*;

// 创建艾睿主动上报协议设备
let mut device = AirtouchRadarDevice::new(ProtocolType::AirtouchActiveReport);

// 设置消息处理器
device.set_message_handler(|msg| {
    println!("收到消息: {}", msg);
});

// 处理接收数据
device.process_data(&received_bytes);

// 创建主动上报帧
let mut frame = device.create_active_report_frame(0x01, vec![0x02, 0x03]);
let encoded = frame.encode();
```

### 帧解析

```rust
// 创建解析器
let mut parser = AirtouchFrameParser::new(ProtocolType::AirtouchActiveReport);

// 喂数据
parser.feed(&received_data);

// 解析帧
while let Some(frame) = parser.parse_frame() {
    println!("帧长度: {}", frame.len());
    println!("协议类型: {}", frame.protocol_type());
    println!("校验通过: {}", frame.verify_checksum());
}
```

## 添加新公司协议

### 1. 创建公司模块

```rust
// src/radar/hikvision.rs
use super::common::*;

pub const HIKVISION_HEADER: u8 = 0xAA;

pub struct HikvisionFrame {
    // 实现具体的帧结构
}

impl RadarFrame for HikvisionFrame {
    // 实现接口方法
}

pub struct HikvisionParser {
    // 实现解析器
}

impl FrameParser for HikvisionParser {
    // 实现接口方法
}

pub struct HikvisionDevice {
    // 实现设备
}

impl RadarDevice for HikvisionDevice {
    // 实现接口方法
}
```

### 2. 更新协议类型

在 `common.rs` 中添加新的协议类型：

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProtocolType {
    AirtouchActiveReport,
    AirtouchAt6010,
    HikvisionProtocol,  // 新增
    Custom,
}
```

### 3. 更新模块导出

在 `mod.rs` 中添加新模块：

```rust
pub mod airtouch;
pub mod hikvision;  // 新增
pub mod common;
```

## 测试

运行测试示例：

```bash
# 艾睿雷达协议测试
cargo run --example airtouch_radar_test

# 错误处理测试
cargo run --example error_handling_test
```

运行单元测试：

```bash
cargo test radar
```

## ESP32移植注意事项

1. **内存管理**: 使用`Vec`和`Box`，在ESP32上注意堆内存限制
2. **串口通信**: 需要实现ESP32的UART驱动
3. **多线程**: 可能需要适配ESP32的FreeRTOS任务
4. **依赖**: 确保所有依赖都支持ESP32目标

## 协议文档

- [艾睿主动上报协议](./airtouch_protocol.md)
- [艾睿AT6010协议](./at6010_protocol.md)

## 贡献指南

1. 新协议必须实现`RadarFrame`、`FrameParser`、`RadarDevice`接口
2. 提供完整的单元测试
3. 更新文档和示例
4. 遵循现有的命名约定
