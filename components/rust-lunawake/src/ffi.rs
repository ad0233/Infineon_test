use crate::single_parse::SingleParse;
use std::ptr;
use std::slice;

/// Opaque struct that wraps SingleParse with output buffer
pub struct SingleParseWrapper {
    parser: SingleParse,
    output_buffer: Vec<u8>,
}

/// Create a new SingleParse instance
/// max_size: maximum frame size for parsing
/// Returns: opaque pointer to the parser, or NULL on failure
#[no_mangle]
pub extern "C" fn rust_single_parse_new(max_size: usize) -> *mut SingleParseWrapper {
    if max_size == 0 {
        return ptr::null_mut();
    }
    
    let wrapper = Box::new(SingleParseWrapper {
        parser: SingleParse::new(max_size),
        output_buffer: vec![0u8; max_size / 2],
    });
    
    Box::into_raw(wrapper)
}

/// Free a SingleParse instance
/// parser: pointer returned from single_parse_new
#[no_mangle]
pub extern "C" fn rust_single_parse_free(parser: *mut SingleParseWrapper) {
    if !parser.is_null() {
        unsafe {
            let _ = Box::from_raw(parser);
        }
    }
}

/// Unpack a single byte
/// parser: pointer to the parser
/// byte: input byte to parse
/// out_len: pointer to store the output length (set to 0 if no complete frame)
/// Returns: pointer to decoded data if frame complete, NULL otherwise
#[no_mangle]
pub extern "C" fn rust_single_parse_unpack(
    parser: *mut SingleParseWrapper,
    byte: u8,
    out_len: *mut usize,
) -> *const u8 {
    if parser.is_null() || out_len.is_null() {
        return ptr::null();
    }
    
    unsafe {
        let wrapper = &mut *parser;
        
        if let Some(data) = wrapper.parser.unpack(byte) {
            let len = data.len().min(wrapper.output_buffer.len());
            wrapper.output_buffer[..len].copy_from_slice(&data[..len]);
            *out_len = len;
            wrapper.output_buffer.as_ptr()
        } else {
            *out_len = 0;
            ptr::null()
        }
    }
}

/// Pack data into a frame
/// data: input data to encode
/// data_len: length of input data
/// out_buf: output buffer provided by C
/// out_buf_len: size of output buffer
/// Returns: actual encoded length on success, -1 on failure
#[no_mangle]
pub extern "C" fn rust_single_parse_pack(
    data: *const u8,
    data_len: usize,
    out_buf: *mut u8,
    out_buf_len: usize,
) -> isize {
    if data.is_null() || out_buf.is_null() || data_len == 0 || out_buf_len == 0 {
        return -1;
    }
    
    unsafe {
        let input_slice = slice::from_raw_parts(data, data_len);
        let encoded = SingleParse::pack(input_slice);
        
        if encoded.len() > out_buf_len {
            return -1;
        }
        
        let output_slice = slice::from_raw_parts_mut(out_buf, out_buf_len);
        output_slice[..encoded.len()].copy_from_slice(&encoded);
        
        encoded.len() as isize
    }
}

