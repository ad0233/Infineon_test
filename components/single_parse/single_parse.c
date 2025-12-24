#include "single_parse.h"
#include <string.h>

#define FRAMEHEAD 0xDA
#define FRAMECTRL 0xFA
#define FRAMETAIL 0x0A

// CRC16-IBM-SDLC (reversed)
// Polynomial: 0x8408 (reversed 0x1021)
// Initial value: 0xFFFF
// Final operation: ~crc (bitwise NOT)
static uint16_t crc16_ibm_sdlc(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0x8408;
            } else {
                crc = (crc >> 1);
            }
        }
    }
    
    return ~crc;
}

// Structure definition moved to header file

void single_parse_lib_init(void) {
    // 空函数，保持接口兼容
}

int single_parse_init(single_parse_handle_t parser, uint8_t* rec_buffer, uint8_t* output_buffer, size_t max_size) {
    if (!parser || max_size == 0 || !rec_buffer || !output_buffer) {
        return -1;
    }
    
    parser->rec_buffer = rec_buffer;
    parser->output_buffer = output_buffer;
    parser->max_size = max_size;
    parser->recv_flag = 0;
    parser->ctrl_flag = 0;
    parser->offset = 0;
    parser->last_byte = 0;
    parser->error_count = 0;
    
    return 0;
}

void single_parse_deinit(single_parse_handle_t parser) {
    // 只重置状态，不释放缓冲区（由调用者管理）
    if (parser) {
        parser->recv_flag = 0;
        parser->ctrl_flag = 0;
        parser->offset = 0;
        parser->last_byte = 0;
        parser->error_count = 0;
    }
}

const uint8_t* single_parse_unpack(single_parse_handle_t parser, uint8_t byte, size_t* out_len) {
    if (!parser || !out_len) {
        return NULL;
    }
    
    struct single_parse_t* p = (struct single_parse_t*)parser;
    
    if (byte == FRAMEHEAD && p->last_byte == FRAMEHEAD) {
        p->offset = 0;
        p->recv_flag = 1;
        p->last_byte = byte;
        *out_len = 0;
        return NULL;
    }
    
    if (p->recv_flag) {
        if (byte == FRAMETAIL && p->last_byte == FRAMETAIL) {
            p->recv_flag = 0;
            
            // 检测到帧尾 0x0A 0x0A
            // 此时 offset 指向第一个 0x0A 之后的位置
            // 需要减去 3：跳过第一个 0x0A 和 CRC16 的 2 个字节
            if (p->offset < 4) {
                p->error_count++;
                p->last_byte = byte;
                *out_len = 0;
                return NULL;
            }
            p->offset -= 3;
            
            uint16_t crc16 = crc16_ibm_sdlc(p->rec_buffer, p->offset);
            uint16_t crc16_recv = ((uint16_t)p->rec_buffer[p->offset] << 8) | p->rec_buffer[p->offset + 1];
            
            if (crc16 == crc16_recv) {
                size_t len = p->offset;
                if (len > (p->max_size / 2)) {
                    len = p->max_size / 2;
                }
                memcpy(p->output_buffer, p->rec_buffer, len);
                *out_len = len;
                p->last_byte = byte;
                return p->output_buffer;
            } else {
                // CRC16 校验失败
                p->error_count++;
                p->last_byte = byte;
                *out_len = 0;
                return NULL;
            }
        }
        
        if (p->ctrl_flag) {
            p->ctrl_flag = 0;
            
            if (byte == FRAMEHEAD || byte == FRAMETAIL || byte == FRAMECTRL) {
                p->rec_buffer[p->offset] = byte;
                p->offset++;
                byte = FRAMECTRL;
            } else {
                p->recv_flag = 0;
                p->error_count++;
            }
        } else {
            if (byte == FRAMECTRL) {
                p->ctrl_flag = 1;
            } else {
                p->rec_buffer[p->offset] = byte;
                p->offset++;
            }
        }
        
        if (p->offset >= p->max_size) {
            p->recv_flag = 0;
            p->error_count++;
        }
    }
    
    p->last_byte = byte;
    *out_len = 0;
    return NULL;
}

int single_parse_pack(const uint8_t* data, size_t data_len, uint8_t* out_buf, size_t out_buf_len) {
    if (!data || !out_buf || data_len == 0 || out_buf_len == 0) {
        return -1;
    }
    
    uint16_t crc16 = crc16_ibm_sdlc(data, data_len);
    size_t out_idx = 0;
    
    // 帧头
    if (out_idx >= out_buf_len) return -1;
    out_buf[out_idx++] = FRAMEHEAD;
    if (out_idx >= out_buf_len) return -1;
    out_buf[out_idx++] = FRAMEHEAD;
    
    // 数据（带转义）
    for (size_t i = 0; i < data_len; i++) {
        uint8_t byte = data[i];
        if (byte == FRAMEHEAD || byte == FRAMETAIL || byte == FRAMECTRL) {
            if (out_idx >= out_buf_len) return -1;
            out_buf[out_idx++] = FRAMECTRL;
        }
        if (out_idx >= out_buf_len) return -1;
        out_buf[out_idx++] = byte;
    }
    
    // CRC16 高字节（带转义）
    uint8_t crc16_h = (uint8_t)(crc16 >> 8);
    if (crc16_h == FRAMEHEAD || crc16_h == FRAMETAIL || crc16_h == FRAMECTRL) {
        if (out_idx >= out_buf_len) return -1;
        out_buf[out_idx++] = FRAMECTRL;
    }
    if (out_idx >= out_buf_len) return -1;
    out_buf[out_idx++] = crc16_h;
    
    // CRC16 低字节（带转义）
    uint8_t crc16_l = (uint8_t)(crc16 & 0xFF);
    if (crc16_l == FRAMEHEAD || crc16_l == FRAMETAIL || crc16_l == FRAMECTRL) {
        if (out_idx >= out_buf_len) return -1;
        out_buf[out_idx++] = FRAMECTRL;
    }
    if (out_idx >= out_buf_len) return -1;
    out_buf[out_idx++] = crc16_l;
    
    // 帧尾
    if (out_idx >= out_buf_len) return -1;
    out_buf[out_idx++] = FRAMETAIL;
    if (out_idx >= out_buf_len) return -1;
    out_buf[out_idx++] = FRAMETAIL;
    
    return (int)out_idx;
}

