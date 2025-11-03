/// FFI 绑定层 - 为 C 代码提供 Rust 雷达协议接口
/// 
/// 这个模块导出 C 兼容的函数，让 C 代码可以调用 Rust 的雷达协议实现

use super::common::*;
use super::airtouch::*;

// ============================================================================
// C 兼容的数据结构
// ============================================================================

#[repr(C)]
pub struct CBhrDetInfo {
    pub det_result: u8,
    pub br_val: u8,
    pub hr_val: u8,
    pub angle_val: u8,
    pub range_val: u16,
    pub padding: u16,
}

impl From<BhrDetInfo> for CBhrDetInfo {
    fn from(info: BhrDetInfo) -> Self {
        Self {
            det_result: info.det_result,
            br_val: info.br_val,
            hr_val: info.hr_val,
            angle_val: info.angle_val,
            range_val: info.range_val,
            padding: info.padding,
        }
    }
}

// ============================================================================
// FFI 函数导出
// ============================================================================

/// 解析 BhrDetInfo 数据（8字节）
/// 
/// # 参数
/// - data: 输入数据指针（必须至少8字节）
/// - len: 数据长度
/// - out: 输出结构体指针
/// 
/// # 返回值
/// - 0: 成功
/// - -1: 参数错误
/// - -2: 数据长度不足
#[no_mangle]
pub extern "C" fn rust_radar_parse_bhr_det_info(
    data: *const u8,
    len: usize,
    out: *mut CBhrDetInfo
) -> i32 {
    // 参数检查
    if data.is_null() || out.is_null() {
        return -1;
    }

    // 转换为切片
    let data_slice = unsafe { std::slice::from_raw_parts(data, len) };

    // 解析
    match BhrDetInfo::from_bytes(data_slice) {
        Ok(bhr_info) => {
            unsafe {
                *out = bhr_info.into();
            }
            0
        }
        Err(_) => -2,
    }
}

/// 获取检测状态描述字符串
/// 
/// # 参数
/// - det_result: 检测状态值
/// - buf: 输出缓冲区
/// - buf_len: 缓冲区长度
/// 
/// # 返回值
/// 写入的字节数（不包括'\0'）
#[no_mangle]
pub extern "C" fn rust_radar_get_detection_status(
    det_result: u8,
    buf: *mut u8,
    buf_len: usize
) -> i32 {
    if buf.is_null() || buf_len == 0 {
        return -1;
    }

    let bhr_info = BhrDetInfo {
        det_result,
        br_val: 0,
        hr_val: 0,
        angle_val: 0,
        range_val: 0,
        padding: 0,
    };

    let status = bhr_info.get_detection_status();
    let status_bytes = status.as_bytes();
    let copy_len = std::cmp::min(status_bytes.len(), buf_len - 1);

    unsafe {
        std::ptr::copy_nonoverlapping(status_bytes.as_ptr(), buf, copy_len);
        *buf.add(copy_len) = 0; // 添加 null terminator
    }

    copy_len as i32
}

// ============================================================================
// 单元测试
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ffi_parse_bhr_det_info() {
        let data = vec![0x18, 20, 75, 0, 0xDC, 0x05, 0x00, 0x00];
        let mut out = CBhrDetInfo {
            det_result: 0,
            br_val: 0,
            hr_val: 0,
            angle_val: 0,
            range_val: 0,
            padding: 0,
        };

        let result = rust_radar_parse_bhr_det_info(
            data.as_ptr(),
            data.len(),
            &mut out as *mut CBhrDetInfo
        );

        assert_eq!(result, 0);
        assert_eq!(out.det_result, 0x18);
        assert_eq!(out.br_val, 20);
        assert_eq!(out.hr_val, 75);
        assert_eq!(out.range_val, 1500);
    }

    #[test]
    fn test_ffi_get_detection_status() {
        let mut buf = [0u8; 64];
        let len = rust_radar_get_detection_status(
            0x18, // 微动 + 存在
            buf.as_mut_ptr(),
            buf.len()
        );

        assert!(len > 0);
        let status = std::str::from_utf8(&buf[..len as usize]).unwrap();
        assert!(status.contains("微动"));
        assert!(status.contains("存在"));
    }
}

