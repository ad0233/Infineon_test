/// 通用雷达协议接口和基础类型
/// 定义所有雷达协议的通用接口

use std::fmt;

// ============================================================================
// 协议类型枚举
// ============================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProtocolType {
    /// 艾睿雷达主动上报协议
    AirtouchActiveReport,
    /// 艾睿雷达AT6010芯片协议
    AirtouchAt6010,
    /// 其他公司协议...
    Custom,
}

impl fmt::Display for ProtocolType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::AirtouchActiveReport => write!(f, "艾睿主动上报协议"),
            Self::AirtouchAt6010 => write!(f, "艾睿AT6010协议"),
            Self::Custom => write!(f, "自定义协议"),
        }
    }
}

// ============================================================================
// 通用雷达帧接口
// ============================================================================

/// 通用雷达帧接口
pub trait RadarFrame {
    /// 编码为字节序列
    fn encode(&mut self) -> Vec<u8>;
    
    /// 获取帧长度
    fn len(&self) -> usize;
    
    /// 验证校验和
    fn verify_checksum(&self) -> bool;
    
    /// 获取协议类型
    fn protocol_type(&self) -> ProtocolType;
}

// ============================================================================
// 呼吸心率检测信息结构
// ============================================================================

/// 呼吸心率检测信息结构体 (TYPE=4)
#[derive(Debug, Clone, PartialEq)]
pub struct BhrDetInfo {
    /// 算法检测状态
    /// 0x01/0x02/0x04: 运动
    /// 0x08: 微动
    /// 0x10: 存在
    pub det_result: u8,
    /// 呼吸频率值
    pub br_val: u8,
    /// 心率, 心脏跳动的速率
    pub hr_val: u8,
    /// 保留字段
    pub angle_val: u8,
    /// 检测距离值, 单位: mm
    pub range_val: u16,
    /// 保留字段
    pub padding: u16,
}

impl BhrDetInfo {
    /// 从字节数组解析呼吸心率检测信息
    pub fn from_bytes(data: &[u8]) -> Result<Self, String> {
        if data.len() < 8 {
            return Err(format!("数据长度不足，需要8字节，实际{}字节", data.len()));
        }

        Ok(Self {
            det_result: data[0],
            br_val: data[1],
            hr_val: data[2],
            angle_val: data[3],
            range_val: u16::from_le_bytes([data[4], data[5]]),
            padding: u16::from_le_bytes([data[6], data[7]]),
        })
    }

    /// 编码为字节数组
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::with_capacity(8);
        bytes.push(self.det_result);
        bytes.push(self.br_val);
        bytes.push(self.hr_val);
        bytes.push(self.angle_val);
        bytes.extend_from_slice(&self.range_val.to_le_bytes());
        bytes.extend_from_slice(&self.padding.to_le_bytes());
        bytes
    }

    /// 获取检测状态描述
    pub fn get_detection_status(&self) -> String {
        let mut status = Vec::new();
        
        if self.det_result & 0x01 != 0 || self.det_result & 0x02 != 0 || self.det_result & 0x04 != 0 {
            status.push("运动");
        }
        if self.det_result & 0x08 != 0 {
            status.push("微动");
        }
        if self.det_result & 0x10 != 0 {
            status.push("存在");
        }
        
        if status.is_empty() {
            "无检测".to_string()
        } else {
            status.join("|")
        }
    }
}

impl fmt::Display for BhrDetInfo {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "呼吸心率检测[状态:{}({:02X}) 呼吸:{} 心率:{} 距离:{}mm]", 
               self.get_detection_status(), 
               self.det_result,
               self.br_val, 
               self.hr_val, 
               self.range_val)
    }
}

// ============================================================================
// 通用雷达消息类型
// ============================================================================

#[derive(Debug, Clone)]
pub enum RadarMessage {
    /// 艾睿主动上报数据
    AirtouchActiveReport {
        msg_type: u8,
        data: Vec<u8>,
    },
    /// 艾睿AT6010命令回复
    AirtouchAt6010Response {
        cmd_group: u8,
        cmd: u8,
        params: Vec<u8>,
    },
    /// 呼吸心率检测信息 (TYPE=4)
    BhrDetectionInfo(BhrDetInfo),
    /// 原始数据
    RawData(Vec<u8>),
    /// 解析错误
    ParseError(String),
}

impl fmt::Display for RadarMessage {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::AirtouchActiveReport { msg_type, data } => {
                write!(f, "艾睿主动上报[类型:{:02X}] 数据:{:?}", msg_type, data)
            }
            Self::AirtouchAt6010Response { cmd_group, cmd, params } => {
                write!(f, "艾睿AT6010回复[组:{:X} 命令:{:X}] 参数:{:?}", cmd_group, cmd, params)
            }
            Self::BhrDetectionInfo(info) => {
                write!(f, "{}", info)
            }
            Self::RawData(data) => {
                write!(f, "原始数据:{:?}", data)
            }
            Self::ParseError(err) => {
                write!(f, "解析错误: {}", err)
            }
        }
    }
}

// ============================================================================
// 通用帧解析器接口
// ============================================================================

/// 通用帧解析器接口
pub trait FrameParser {
    /// 添加接收到的数据
    fn feed(&mut self, data: &[u8]);
    
    /// 尝试解析一帧数据
    fn parse_frame(&mut self) -> Option<Box<dyn RadarFrame>>;
    
    /// 清空缓冲区
    fn clear(&mut self);
    
    /// 获取缓冲区大小
    fn buffer_size(&self) -> usize;
    
    /// 用于downcast的辅助方法
    fn as_any_mut(&mut self) -> &mut dyn std::any::Any;
}

// ============================================================================
// 通用雷达设备接口
// ============================================================================

/// 通用雷达设备接口
pub trait RadarDevice {
    /// 处理接收到的数据
    fn process_data(&mut self, data: &[u8]);
    
    /// 设置消息处理器
    fn set_message_handler<F>(&mut self, handler: F)
    where
        F: Fn(RadarMessage) + Send + Sync + 'static;
    
    /// 获取协议类型
    fn protocol_type(&self) -> ProtocolType;
    
    /// 清空缓冲区
    fn clear_buffer(&mut self);
}

// ============================================================================
// 工具函数
// ============================================================================

/// 格式化字节数组为十六进制字符串
pub fn format_hex(data: &[u8]) -> String {
    data.iter()
        .map(|b| format!("{:02X}", b))
        .collect::<Vec<_>>()
        .join(" ")
}

/// 计算8位校验和
pub fn calculate_checksum_8(data: &[u8]) -> u8 {
    let sum: u32 = data.iter().map(|&b| b as u32).sum();
    (sum & 0xFF) as u8
}

/// 计算16位校验和
pub fn calculate_checksum_16(data: &[u8]) -> u16 {
    let sum: u32 = data.iter().map(|&b| b as u32).sum();
    (sum & 0xFFFF) as u16
}

/// 从字节序转换 (小端序)
pub fn from_le_bytes_u16(data: &[u8]) -> Result<u16, String> {
    if data.len() < 2 {
        return Err(format!("数据长度不足，需要2字节，实际{}字节", data.len()));
    }
    Ok(u16::from_le_bytes([data[0], data[1]]))
}

/// 从字节序转换 (大端序)
pub fn from_be_bytes_u16(data: &[u8]) -> Result<u16, String> {
    if data.len() < 2 {
        return Err(format!("数据长度不足，需要2字节，实际{}字节", data.len()));
    }
    Ok(u16::from_be_bytes([data[0], data[1]]))
}

// ============================================================================
// 单元测试
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_bhr_det_info_from_bytes() {
        // 测试数据: 检测状态=0x18(微动+存在), 呼吸=20, 心率=75, 角度=0, 距离=1500mm, 填充=0
        let data = vec![0x18, 20, 75, 0, 0xDC, 0x05, 0x00, 0x00]; // 1500 = 0x05DC
        
        let bhr_info = BhrDetInfo::from_bytes(&data).unwrap();
        
        assert_eq!(bhr_info.det_result, 0x18);
        assert_eq!(bhr_info.br_val, 20);
        assert_eq!(bhr_info.hr_val, 75);
        assert_eq!(bhr_info.angle_val, 0);
        assert_eq!(bhr_info.range_val, 1500);
        assert_eq!(bhr_info.padding, 0);
    }

    #[test]
    fn test_bhr_det_info_to_bytes() {
        let bhr_info = BhrDetInfo {
            det_result: 0x18,
            br_val: 20,
            hr_val: 75,
            angle_val: 0,
            range_val: 1500,
            padding: 0,
        };
        
        let bytes = bhr_info.to_bytes();
        let expected = vec![0x18, 20, 75, 0, 0xDC, 0x05, 0x00, 0x00];
        
        assert_eq!(bytes, expected);
    }

    #[test]
    fn test_bhr_det_info_detection_status() {
        // 测试运动状态
        let bhr_info = BhrDetInfo {
            det_result: 0x01,
            br_val: 0,
            hr_val: 0,
            angle_val: 0,
            range_val: 0,
            padding: 0,
        };
        assert_eq!(bhr_info.get_detection_status(), "运动");

        // 测试微动状态
        let bhr_info = BhrDetInfo {
            det_result: 0x08,
            br_val: 0,
            hr_val: 0,
            angle_val: 0,
            range_val: 0,
            padding: 0,
        };
        assert_eq!(bhr_info.get_detection_status(), "微动");

        // 测试存在状态
        let bhr_info = BhrDetInfo {
            det_result: 0x10,
            br_val: 0,
            hr_val: 0,
            angle_val: 0,
            range_val: 0,
            padding: 0,
        };
        assert_eq!(bhr_info.get_detection_status(), "存在");

        // 测试组合状态
        let bhr_info = BhrDetInfo {
            det_result: 0x18, // 微动 + 存在
            br_val: 0,
            hr_val: 0,
            angle_val: 0,
            range_val: 0,
            padding: 0,
        };
        assert_eq!(bhr_info.get_detection_status(), "微动|存在");

        // 测试无检测状态
        let bhr_info = BhrDetInfo {
            det_result: 0x00,
            br_val: 0,
            hr_val: 0,
            angle_val: 0,
            range_val: 0,
            padding: 0,
        };
        assert_eq!(bhr_info.get_detection_status(), "无检测");
    }

    #[test]
    fn test_bhr_det_info_from_bytes_insufficient_data() {
        let data = vec![0x18, 20, 75]; // 只有3字节，不足8字节
        
        let result = BhrDetInfo::from_bytes(&data);
        assert!(result.is_err());
        assert!(result.unwrap_err().contains("数据长度不足"));
    }

    #[test]
    fn test_radar_message_bhr_detection_display() {
        let bhr_info = BhrDetInfo {
            det_result: 0x18,
            br_val: 20,
            hr_val: 75,
            angle_val: 0,
            range_val: 1500,
            padding: 0,
        };
        
        let message = RadarMessage::BhrDetectionInfo(bhr_info);
        let display_str = format!("{}", message);
        
        assert!(display_str.contains("呼吸心率检测"));
        assert!(display_str.contains("微动|存在"));
        assert!(display_str.contains("呼吸:20"));
        assert!(display_str.contains("心率:75"));
        assert!(display_str.contains("距离:1500mm"));
    }
}
