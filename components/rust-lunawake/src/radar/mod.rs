/// 雷达协议模块
/// 按公司组织不同的雷达协议实现

pub mod airtouch;  // 艾睿电子/艾睿雷达协议
pub mod common;    // 通用雷达协议接口
pub mod ffi;       // FFI 绑定层（C 接口）

// 重新导出常用类型
pub use common::*;
