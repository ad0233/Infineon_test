#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成BLE命令的16进制字符串
用法: python gen_ble_cmd.py <json_file>
"""

import json
import sys

# 协议常量
FRAMEHEAD = 0xDA
FRAMECTRL = 0xFA
FRAMETAIL = 0x0A

def crc16_ibm_sdlc(data):
    """CRC16-IBM-SDLC (reversed)"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc = crc >> 1
    return ~crc & 0xFFFF

def single_parse_pack(data):
    """打包数据为协议格式"""
    data_len = len(data)
    if data_len == 0:
        return None
    
    crc16 = crc16_ibm_sdlc(data)
    out_buf = []
    
    # 帧头
    out_buf.append(FRAMEHEAD)
    out_buf.append(FRAMEHEAD)
    
    # 数据（带转义）
    for byte in data:
        if byte == FRAMEHEAD or byte == FRAMETAIL or byte == FRAMECTRL:
            out_buf.append(FRAMECTRL)
        out_buf.append(byte)
    
    # CRC16 高字节（带转义）
    crc16_h = (crc16 >> 8) & 0xFF
    if crc16_h == FRAMEHEAD or crc16_h == FRAMETAIL or crc16_h == FRAMECTRL:
        out_buf.append(FRAMECTRL)
    out_buf.append(crc16_h)
    
    # CRC16 低字节（带转义）
    crc16_l = crc16 & 0xFF
    if crc16_l == FRAMEHEAD or crc16_l == FRAMETAIL or crc16_l == FRAMECTRL:
        out_buf.append(FRAMECTRL)
    out_buf.append(crc16_l)
    
    # 帧尾
    out_buf.append(FRAMETAIL)
    out_buf.append(FRAMETAIL)
    
    return bytes(out_buf)

def main():
    if len(sys.argv) < 2:
        print("用法: python gen_ble_cmd.py <json_file>")
        sys.exit(1)
    
    json_file = sys.argv[1]
    
    try:
        # 读取JSON文件
        with open(json_file, 'r', encoding='utf-8') as f:
            json_data = json.load(f)
        
        # 转换为JSON字符串
        json_str = json.dumps(json_data, ensure_ascii=False, separators=(',', ':'))
        
        # 构建命令：命令类型(0x01) + JSON字符串
        cmd_data = bytes([0x01]) + json_str.encode('utf-8')
        
        # 打包
        packed = single_parse_pack(cmd_data)
        
        if packed:
            # 输出16进制字符串
            hex_str = ''.join(f'{b:02X}' for b in packed)
            print(hex_str)
        else:
            print("打包失败", file=sys.stderr)
            sys.exit(1)
            
    except FileNotFoundError:
        print(f"文件不存在: {json_file}", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"JSON解析错误: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"错误: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()

