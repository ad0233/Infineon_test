#!/usr/bin/env python3
"""
自动化编译和加密脚本
功能：
1. 编译ESP32项目
2. 加密编译后的二进制文件
3. 生成包含分支名称和hash码的文件名
"""

import os
import sys
import subprocess
import hashlib
import argparse
from datetime import datetime

def run_command(cmd, cwd=None):
    """执行命令并返回结果"""
    print(f"执行命令: {cmd}")
    try:
        result = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"命令执行失败: {result.stderr}")
            return False
        print(f"命令执行成功")
        return True
    except Exception as e:
        print(f"执行命令时出错: {e}")
        return False

def get_git_info():
    """获取git分支和commit hash信息"""
    try:
        # 获取当前分支
        branch_result = subprocess.run(['git', 'branch', '--show-current'], 
                                     capture_output=True, text=True)
        branch = branch_result.stdout.strip() if branch_result.returncode == 0 else "unknown"
        
        # 获取短hash
        hash_result = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], 
                                   capture_output=True, text=True)
        commit_hash = hash_result.stdout.strip() if hash_result.returncode == 0 else "unknown"
        
        return branch, commit_hash
    except Exception as e:
        print(f"获取git信息失败: {e}")
        return "unknown", "unknown"

def get_file_hash(file_path):
    """计算文件的MD5 hash"""
    try:
        with open(file_path, 'rb') as f:
            file_hash = hashlib.md5(f.read()).hexdigest()[:8]
        return file_hash
    except Exception as e:
        print(f"计算文件hash失败: {e}")
        return "unknown"

def main():
    parser = argparse.ArgumentParser(description='自动化编译和加密ESP32项目')
    parser.add_argument('--device-key', '-k', default='components/my_ota/rsa_pub_key.pem', 
                       help='设备公钥文件路径 (rsa_pub_key.pem)')
    parser.add_argument('--output-dir', '-o', default='./build', 
                       help='输出目录 (默认: ./build)')
    parser.add_argument('--clean', '-c', action='store_true', 
                       help='编译前清理项目')
    parser.add_argument('--no-build', action='store_true', 
                       help='跳过编译，直接加密现有的bin文件')
    
    args = parser.parse_args()
    
    # 检查设备公钥文件
    if not os.path.exists(args.device_key):
        print(f"错误: 设备公钥文件不存在: {args.device_key}")
        sys.exit(1)
    
    # 创建输出目录
    os.makedirs(args.output_dir, exist_ok=True)
    
    # 获取git信息
    branch, commit_hash = get_git_info()
    print(f"当前分支: {branch}")
    print(f"Commit Hash: {commit_hash}")
    
    # 二进制文件路径
    bin_file = "build/Lunawake.bin"
    encrypted_file = None
    
    if not args.no_build:
        print("\n=== 开始编译项目 ===")
        
        # 清理项目（如果需要）
        if args.clean:
            print("清理项目...")
            if not run_command("idf.py fullclean"):
                print("清理失败，继续执行...")
        
        # 编译项目
        print("编译项目...")
        if not run_command("idf.py build"):
            print("编译失败!")
            sys.exit(1)
        
        print("编译完成!")
    
    # 检查二进制文件是否存在
    if not os.path.exists(bin_file):
        print(f"错误: 二进制文件不存在: {bin_file}")
        sys.exit(1)
    
    # 计算文件hash
    file_hash = get_file_hash(bin_file)
    print(f"文件Hash: {file_hash}")
    
    # 生成输出文件名
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    encrypted_filename = f"Lunawake_{branch}_{commit_hash}_{file_hash}_{timestamp}.bin"
    encrypted_file = os.path.join(args.output_dir, encrypted_filename)
    
    print(f"\n=== 开始加密文件 ===")
    print(f"输入文件: {bin_file}")
    print(f"输出文件: {encrypted_file}")
    
    # 执行加密命令
    encrypt_cmd = f"python3 dependencies/espressif__esp_encrypted_img/tools/esp_enc_img_gen.py encrypt {bin_file} {args.device_key} {encrypted_file}"
    
    if not run_command(encrypt_cmd):
        print("加密失败!")
        sys.exit(1)
    
    print(f"\n=== 完成! ===")
    print(f"加密后的文件: {encrypted_file}")
    print(f"文件大小: {os.path.getsize(encrypted_file)} bytes")
    
    # 显示文件信息
    print(f"\n文件信息:")
    print(f"- 分支: {branch}")
    print(f"- Commit: {commit_hash}")
    print(f"- 文件Hash: {file_hash}")
    print(f"- 时间戳: {timestamp}")

if __name__ == "__main__":
    main()
