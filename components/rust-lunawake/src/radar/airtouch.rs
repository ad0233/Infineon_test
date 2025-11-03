/// 艾睿电子雷达协议实现
/// 支持主动上报协议和AT6010芯片协议

use super::common::*;
use std::collections::VecDeque;

// ============================================================================
// 艾睿协议常量定义
// ============================================================================

/// 主动上报协议帧头
pub const AIRTOUCH_ACTIVE_REPORT_HEADER: u8 = 0x5A;

/// AT6010芯片发送帧头
pub const AIRTOUCH_AT6010_TX_HEADER: u8 = 0x58;

/// AT6010芯片回复帧头
pub const AIRTOUCH_AT6010_RX_HEADER: u8 = 0x59;

// ============================================================================
// 艾睿主动上报协议结构
// ============================================================================

#[derive(Debug, Clone)]
pub struct AirtouchActiveReportFrame {
    pub len: u8,           // 载荷长度
    pub payload: Vec<u8>,  // 载荷数据
    pub checksum: u8,      // 8位校验和
}

impl AirtouchActiveReportFrame {
    /// 构造主动上报帧
    pub fn new(payload: Vec<u8>) -> Self {
        let len = payload.len() as u8;
        Self {
            len,
            payload,
            checksum: 0, // 将在encode时计算
        }
    }

    /// 计算校验和 (HEAD + LEN + PAYLOAD)
    fn calculate_checksum(&self) -> u8 {
        let mut sum: u32 = AIRTOUCH_ACTIVE_REPORT_HEADER as u32 + self.len as u32;
        for &byte in &self.payload {
            sum += byte as u32;
        }
        (sum & 0xFF) as u8
    }
}

impl RadarFrame for AirtouchActiveReportFrame {
    /// 编码为字节序列
    fn encode(&mut self) -> Vec<u8> {
        // 计算校验和
        self.checksum = self.calculate_checksum();

        // 构造帧
        let mut frame = Vec::new();
        frame.push(AIRTOUCH_ACTIVE_REPORT_HEADER);
        frame.push(self.len);
        frame.extend_from_slice(&self.payload);
        frame.push(self.checksum);
        
        frame
    }

    /// 获取帧长度
    fn len(&self) -> usize {
        3 + self.payload.len() // HEAD + LEN + PAYLOAD + CHECK
    }

    /// 验证校验和
    fn verify_checksum(&self) -> bool {
        self.calculate_checksum() == self.checksum
    }

    /// 获取协议类型
    fn protocol_type(&self) -> ProtocolType {
        ProtocolType::AirtouchActiveReport
    }
}

// ============================================================================
// 艾睿AT6010芯片协议结构
// ============================================================================

#[derive(Debug, Clone)]
pub struct AirtouchAt6010Frame {
    pub cmd_group: u8,     // 命令分组 (3位)
    pub cmd: u8,           // 控制命令 (5位)
    pub params: Vec<u8>,   // 参数数据
    pub checksum: u16,     // 16位校验和
    pub is_response: bool, // 是否为回复帧
}

impl AirtouchAt6010Frame {
    /// 构造AT6010发送帧
    pub fn new_tx(cmd_group: u8, cmd: u8, params: Vec<u8>) -> Self {
        Self {
            cmd_group: cmd_group & 0x07, // 确保只有3位
            cmd: cmd & 0x1F,             // 确保只有5位
            params,
            checksum: 0, // 将在encode时计算
            is_response: false,
        }
    }

    /// 构造AT6010回复帧
    pub fn new_rx(cmd_group: u8, cmd: u8, params: Vec<u8>) -> Self {
        Self {
            cmd_group: cmd_group & 0x07,
            cmd: cmd & 0x1F,
            params,
            checksum: 0,
            is_response: true,
        }
    }

    /// 计算校验和 (HEAD + CMD_GROUP_CMD + PARAM_LEN + PARAMS)
    fn calculate_checksum(&self) -> u16 {
        let header = if self.is_response { 
            AIRTOUCH_AT6010_RX_HEADER 
        } else { 
            AIRTOUCH_AT6010_TX_HEADER 
        };
        
        let mut sum: u32 = header as u32;
        sum += ((self.cmd_group << 5) | self.cmd) as u32;
        sum += self.params.len() as u32;
        for &byte in &self.params {
            sum += byte as u32;
        }
        (sum & 0xFFFF) as u16
    }
}

impl RadarFrame for AirtouchAt6010Frame {
    /// 编码为字节序列
    fn encode(&mut self) -> Vec<u8> {
        // 计算校验和
        self.checksum = self.calculate_checksum();

        // 构造帧
        let header = if self.is_response { 
            AIRTOUCH_AT6010_RX_HEADER 
        } else { 
            AIRTOUCH_AT6010_TX_HEADER 
        };

        let mut frame = Vec::new();
        frame.push(header);
        frame.push((self.cmd_group << 5) | self.cmd);
        frame.push(self.params.len() as u8);
        frame.extend_from_slice(&self.params);
        frame.push((self.checksum & 0xFF) as u8);
        frame.push(((self.checksum >> 8) & 0xFF) as u8);
        
        frame
    }

    /// 获取帧长度
    fn len(&self) -> usize {
        5 + self.params.len() // HEAD + CMD + LEN + PARAMS + CHECK(2)
    }

    /// 验证校验和
    fn verify_checksum(&self) -> bool {
        self.calculate_checksum() == self.checksum
    }

    /// 获取协议类型
    fn protocol_type(&self) -> ProtocolType {
        ProtocolType::AirtouchAt6010
    }
}

// ============================================================================
// 艾睿帧解析器
// ============================================================================

pub struct AirtouchFrameParser {
    buffer: VecDeque<u8>,
    protocol_type: ProtocolType,
}

impl AirtouchFrameParser {
    /// 创建新的艾睿帧解析器
    pub fn new(protocol_type: ProtocolType) -> Self {
        Self {
            buffer: VecDeque::new(),
            protocol_type,
        }
    }
}

impl FrameParser for AirtouchFrameParser {
    /// 添加接收到的数据
    fn feed(&mut self, data: &[u8]) {
        self.buffer.extend(data.iter());
    }

    /// 尝试解析一帧数据
    fn parse_frame(&mut self) -> Option<Box<dyn RadarFrame>> {
        match self.protocol_type {
            ProtocolType::AirtouchActiveReport => self.parse_active_report_frame(),
            ProtocolType::AirtouchAt6010 => self.parse_at6010_frame(),
            _ => None,
        }
    }

    /// 清空缓冲区
    fn clear(&mut self) {
        self.buffer.clear();
    }

    /// 获取缓冲区大小
    fn buffer_size(&self) -> usize {
        self.buffer.len()
    }
    
    /// 用于downcast的辅助方法
    fn as_any_mut(&mut self) -> &mut dyn std::any::Any {
        self
    }
}

impl AirtouchFrameParser {
    /// 解析主动上报协议帧并返回消息
    pub fn parse_active_report_message(&mut self) -> Option<RadarMessage> {
        // 查找帧头
        while self.buffer.len() >= 3 {
            if let Some(&AIRTOUCH_ACTIVE_REPORT_HEADER) = self.buffer.front() {
                // 找到帧头，检查是否有足够数据
                if self.buffer.len() < 3 {
                    break;
                }

                let len = self.buffer[1] as usize;
                let total_len = 3 + len; // HEAD + LEN + PAYLOAD + CHECK

                if self.buffer.len() < total_len {
                    break;
                }

                // 提取完整帧
                let mut frame_data = Vec::new();
                for _ in 0..total_len {
                    if let Some(byte) = self.buffer.pop_front() {
                        frame_data.push(byte);
                    }
                }

                // 验证长度字段
                if frame_data.len() != total_len || frame_data[1] as usize != len {
                    continue;
                }

                // 构造帧对象
                let payload = frame_data[2..2+len].to_vec();
                let checksum = frame_data[2+len];
                
                let frame = AirtouchActiveReportFrame {
                    len: len as u8,
                    payload: payload.clone(),
                    checksum,
                };

                // 验证校验和
                if frame.verify_checksum() {
                    // 解析载荷
                    if payload.is_empty() {
                        return Some(RadarMessage::ParseError("载荷为空".to_string()));
                    }
                    
                    let msg_type = payload[0];
                    let data = payload[1..].to_vec();
                    
                    // 检查是否为类型4的呼吸心率检测信息
                    if msg_type == 4 && data.len() >= 8 {
                        match BhrDetInfo::from_bytes(&data) {
                            Ok(bhr_info) => {
                                return Some(RadarMessage::BhrDetectionInfo(bhr_info));
                            }
                            Err(err) => {
                                return Some(RadarMessage::ParseError(format!("解析呼吸心率检测信息失败: {}", err)));
                            }
                        }
                    }
                    
                    return Some(RadarMessage::AirtouchActiveReport {
                        msg_type,
                        data,
                    });
                } else {
                    // 校验失败，可能是未知的大帧格式，跳过这个帧
                    // 不打印错误，静默跳过
                    // 继续查找下一个帧
                }
            } else {
                // 移除不匹配的字节
                self.buffer.pop_front();
            }
        }

        None
    }
    
    /// 解析主动上报协议帧
    fn parse_active_report_frame(&mut self) -> Option<Box<dyn RadarFrame>> {
        // 保持向后兼容
        while self.buffer.len() >= 3 {
            if let Some(&AIRTOUCH_ACTIVE_REPORT_HEADER) = self.buffer.front() {
                if self.buffer.len() < 3 {
                    break;
                }

                let len = self.buffer[1] as usize;
                let total_len = 3 + len;

                if self.buffer.len() < total_len {
                    break;
                }

                let mut frame_data = Vec::new();
                for _ in 0..total_len {
                    if let Some(byte) = self.buffer.pop_front() {
                        frame_data.push(byte);
                    }
                }

                if frame_data.len() != total_len || frame_data[1] as usize != len {
                    continue;
                }

                let payload = frame_data[2..2+len].to_vec();
                let checksum = frame_data[2+len];
                
                let frame = AirtouchActiveReportFrame {
                    len: len as u8,
                    payload,
                    checksum,
                };

                if frame.verify_checksum() {
                    return Some(Box::new(frame));
                }
            } else {
                self.buffer.pop_front();
            }
        }

        None
    }

    /// 解析AT6010协议帧
    fn parse_at6010_frame(&mut self) -> Option<Box<dyn RadarFrame>> {
        // 查找帧头 (0x58 或 0x59)
        while self.buffer.len() >= 5 {
            if let Some(&header) = self.buffer.front() {
                if header == AIRTOUCH_AT6010_TX_HEADER || header == AIRTOUCH_AT6010_RX_HEADER {
                    // 找到帧头，检查是否有足够数据
                    if self.buffer.len() < 5 {
                        break;
                    }

                    let param_len = self.buffer[2] as usize;
                    let total_len = 5 + param_len; // HEAD + CMD + LEN + PARAMS + CHECK(2)

                    if self.buffer.len() < total_len {
                        break;
                    }

                    // 提取完整帧
                    let mut frame_data = Vec::new();
                    for _ in 0..total_len {
                        if let Some(byte) = self.buffer.pop_front() {
                            frame_data.push(byte);
                        }
                    }

                    // 验证长度字段
                    if frame_data.len() != total_len || frame_data[2] as usize != param_len {
                        continue;
                    }

                    // 构造帧对象
                    let cmd_byte = frame_data[1];
                    let cmd_group = (cmd_byte >> 5) & 0x07;
                    let cmd = cmd_byte & 0x1F;
                    let params = frame_data[3..3+param_len].to_vec();
                    let checksum = ((frame_data[3+param_len+1] as u16) << 8) | (frame_data[3+param_len] as u16);

                    let frame = AirtouchAt6010Frame {
                        cmd_group,
                        cmd,
                        params,
                        checksum,
                        is_response: header == AIRTOUCH_AT6010_RX_HEADER,
                    };

                    // 验证校验和
                    if frame.verify_checksum() {
                        return Some(Box::new(frame));
                    }
                } else {
                    // 移除不匹配的字节
                    self.buffer.pop_front();
                }
            }
        }

        None
    }
}

// ============================================================================
// 艾睿雷达设备
// ============================================================================

pub struct AirtouchRadarDevice {
    protocol_type: ProtocolType,
    parser: Box<dyn FrameParser>,
    message_handler: Option<Box<dyn Fn(RadarMessage) + Send + Sync>>,
}

impl AirtouchRadarDevice {
    /// 创建新的艾睿雷达设备
    pub fn new(protocol_type: ProtocolType) -> Self {
        let parser: Box<dyn FrameParser> = match protocol_type {
            ProtocolType::AirtouchActiveReport | ProtocolType::AirtouchAt6010 => {
                Box::new(AirtouchFrameParser::new(protocol_type))
            }
            _ => panic!("不支持的协议类型: {:?}", protocol_type),
        };

        Self {
            protocol_type,
            parser,
            message_handler: None,
        }
    }

    /// 构造主动上报帧
    pub fn create_active_report_frame(&self, msg_type: u8, data: Vec<u8>) -> Box<dyn RadarFrame> {
        let mut payload = Vec::new();
        payload.push(msg_type);
        payload.extend(data);
        
        Box::new(AirtouchActiveReportFrame::new(payload))
    }

    /// 构造AT6010发送帧
    pub fn create_at6010_tx_frame(&self, cmd_group: u8, cmd: u8, params: Vec<u8>) -> Box<dyn RadarFrame> {
        Box::new(AirtouchAt6010Frame::new_tx(cmd_group, cmd, params))
    }

    /// 构造AT6010回复帧
    pub fn create_at6010_rx_frame(&self, cmd_group: u8, cmd: u8, params: Vec<u8>) -> Box<dyn RadarFrame> {
        Box::new(AirtouchAt6010Frame::new_rx(cmd_group, cmd, params))
    }
}

impl RadarDevice for AirtouchRadarDevice {
    /// 处理接收到的数据
    fn process_data(&mut self, data: &[u8]) {
        self.parser.feed(data);

        // 尝试解析消息
        match self.protocol_type {
            ProtocolType::AirtouchActiveReport => {
                // 使用downcast获取具体解析器
                if let Some(parser) = self.parser.as_any_mut().downcast_mut::<AirtouchFrameParser>() {
                    while let Some(message) = parser.parse_active_report_message() {
                        if let Some(handler) = &self.message_handler {
                            handler(message);
                        }
                    }
                }
            }
            ProtocolType::AirtouchAt6010 => {
                // AT6010协议解析
                while let Some(frame) = self.parser.parse_frame() {
                    let message = self.parse_frame_to_message(frame);
                    if let Some(handler) = &self.message_handler {
                        handler(message);
                    }
                }
            }
            _ => {}
        }
    }

    /// 设置消息处理器
    fn set_message_handler<F>(&mut self, handler: F)
    where
        F: Fn(RadarMessage) + Send + Sync + 'static,
    {
        self.message_handler = Some(Box::new(handler));
    }

    /// 获取协议类型
    fn protocol_type(&self) -> ProtocolType {
        self.protocol_type
    }

    /// 清空缓冲区
    fn clear_buffer(&mut self) {
        self.parser.clear();
    }
}

impl AirtouchRadarDevice {
    /// 将帧转换为消息
    fn parse_frame_to_message(&self, frame: Box<dyn RadarFrame>) -> RadarMessage {
        // 由于trait object的限制，我们需要重新解析数据
        // 这里可以通过downcast或者在解析时直接返回消息来优化
        // 暂时返回解析成功的标记
        match frame.protocol_type() {
            ProtocolType::AirtouchActiveReport => {
                // TODO: 这里应该能访问帧的实际数据
                // 由于trait object限制，暂时无法直接访问
                RadarMessage::AirtouchActiveReport {
                    msg_type: 0,
                    data: vec![],
                }
            }
            ProtocolType::AirtouchAt6010 => {
                RadarMessage::AirtouchAt6010Response {
                    cmd_group: 0,
                    cmd: 0,
                    params: vec![],
                }
            }
            _ => RadarMessage::ParseError("未知协议类型".to_string()),
        }
    }
}

// ============================================================================
// 单元测试
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_airtouch_active_report_frame_encoding() {
        let payload = vec![0x01, 0x02, 0x03];
        let mut frame = AirtouchActiveReportFrame::new(payload);
        let encoded = frame.encode();
        
        assert_eq!(encoded[0], AIRTOUCH_ACTIVE_REPORT_HEADER);
        assert_eq!(encoded[1], 3); // payload length
        assert_eq!(encoded[2..5], [0x01, 0x02, 0x03]);
        assert!(frame.verify_checksum());
    }

    #[test]
    fn test_airtouch_at6010_frame_encoding() {
        let params = vec![0x01, 0x02];
        let mut frame = AirtouchAt6010Frame::new_tx(0x01, 0x05, params);
        let encoded = frame.encode();
        
        assert_eq!(encoded[0], AIRTOUCH_AT6010_TX_HEADER);
        assert_eq!(encoded[1], (0x01 << 5) | 0x05); // cmd_group << 5 | cmd
        assert_eq!(encoded[2], 2); // param length
        assert_eq!(encoded[3..5], [0x01, 0x02]);
        assert!(frame.verify_checksum());
    }

    #[test]
    fn test_parse_bhr_detection_message() {
        // 构造类型4的呼吸心率检测消息
        let bhr_data = vec![0x18, 20, 75, 0, 0xDC, 0x05, 0x00, 0x00]; // 检测状态=0x18, 呼吸=20, 心率=75, 距离=1500mm
        let payload = {
            let mut p = vec![4]; // 消息类型4
            p.extend_from_slice(&bhr_data);
            p
        };
        
        let mut frame = AirtouchActiveReportFrame::new(payload);
        let encoded = frame.encode();
        
        // 创建解析器并解析
        let mut parser = AirtouchFrameParser::new(ProtocolType::AirtouchActiveReport);
        parser.feed(&encoded);
        
        let message = parser.parse_active_report_message();
        assert!(message.is_some());
        
        match message.unwrap() {
            RadarMessage::BhrDetectionInfo(bhr_info) => {
                assert_eq!(bhr_info.det_result, 0x18);
                assert_eq!(bhr_info.br_val, 20);
                assert_eq!(bhr_info.hr_val, 75);
                assert_eq!(bhr_info.range_val, 1500);
                assert_eq!(bhr_info.get_detection_status(), "微动|存在");
            }
            _ => panic!("期望解析为呼吸心率检测消息"),
        }
    }

    #[test]
    fn test_parse_bhr_detection_message_insufficient_data() {
        // 构造数据不足的类型4消息
        let bhr_data = vec![0x18, 20, 75]; // 只有3字节，不足8字节
        let payload = {
            let mut p = vec![4]; // 消息类型4
            p.extend_from_slice(&bhr_data);
            p
        };
        
        let mut frame = AirtouchActiveReportFrame::new(payload);
        let encoded = frame.encode();
        
        // 创建解析器并解析
        let mut parser = AirtouchFrameParser::new(ProtocolType::AirtouchActiveReport);
        parser.feed(&encoded);
        
        let message = parser.parse_active_report_message();
        assert!(message.is_some());
        
        match message.unwrap() {
            RadarMessage::AirtouchActiveReport { msg_type, data } => {
                // 数据不足时，应该作为普通消息处理，而不是错误
                assert_eq!(msg_type, 4);
                assert_eq!(data.len(), 3);
            }
            _ => panic!("期望解析为普通消息"),
        }
    }
}
