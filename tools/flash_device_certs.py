#!/usr/bin/env python3
"""
烧录设备证书和 IoT 配置到 ESP32
用法: python tools/flash_device_certs.py <设备目录> [选项]
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path


def find_script(name: str) -> Path:
    """查找脚本路径"""
    project_root = Path(__file__).parent.parent
    script = project_root / name
    if not script.exists():
        raise FileNotFoundError(f"未找到脚本: {script}")
    return script


def run_cmd(cmd: list, check=True):
    """执行命令"""
    print(f"执行: {' '.join(cmd)}")
    result = subprocess.run(cmd, check=check)
    return result.returncode == 0


def main():
    parser = argparse.ArgumentParser(
        description="烧录设备证书和 IoT 配置到 ESP32",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python tools/flash_device_certs.py tools/LW25BQ7JFEAL -p /dev/ttyUSB0
  python tools/flash_device_certs.py tools/LW25BQ7JFEAL -p /dev/ttyUSB0 --skip-ds --skip-iot
        """
    )
    parser.add_argument(
        "device_dir",
        type=Path,
        help="设备目录路径（包含证书和 iot_config.json）"
    )
    parser.add_argument(
        "-p", "--port",
        default="/dev/ttyUSB0",
        help="串口路径，如 /dev/ttyUSB0"
    )
    parser.add_argument(
        "--target-chip",
        default="esp32s3",
        choices=["esp32", "esp32s2", "esp32c3", "esp32s3", "esp32c6", "esp32h2", "esp32p4"],
        help="目标芯片类型（默认: esp32s3）"
    )
    parser.add_argument(
        "--efuse-key-id",
        type=int,
        default=2,
        help="eFuse key ID"
    )
    parser.add_argument(
        "--priv-key-algo",
        default="RSA 2048",
        nargs=2,
        metavar=("ALGO", "SIZE"),
        help="私钥算法和大小（默认: RSA 2048）"
    )
    parser.add_argument(
        "--skip-ds",
        action="store_true",
        help="跳过 DS 证书烧录"
    )
    parser.add_argument(
        "--skip-iot",
        action="store_true",
        help="跳过 IoT 配置烧录"
    )
    parser.add_argument(
        "--skip-flash",
        action="store_true",
        help="只生成文件，不烧录（仅对 DS 证书有效）"
    )

    args = parser.parse_args()

    device_dir = args.device_dir.resolve()
    if not device_dir.exists():
        print(f"错误: 设备目录不存在: {device_dir}", file=sys.stderr)
        return 1

    # 检查必需文件
    private_key = device_dir / "private_key.pem"
    device_cert = device_dir / "certificate.pem"
    ca_cert = device_dir / "AmazonRootCA1.pem"
    iot_config = device_dir / "iot_config.json"

    missing_files = []
    if not args.skip_ds:
        if not private_key.exists():
            missing_files.append("private_key.pem")
        if not device_cert.exists():
            missing_files.append("certificate.pem")
        if not ca_cert.exists():
            missing_files.append("AmazonRootCA1.pem")
    if not args.skip_iot and not iot_config.exists():
        missing_files.append("iot_config.json")

    if missing_files:
        print(f"错误: 缺少必需文件: {', '.join(missing_files)}", file=sys.stderr)
        return 1

    project_root = Path(__file__).parent.parent
    os.chdir(project_root)

    # 1. 烧录 DS 证书
    if not args.skip_ds:
        print("\n" + "="*60)
        print("步骤 1: 烧录 DS 证书")
        print("="*60)
        
        configure_script = find_script("dependencies/espressif__esp_secure_cert_mgr/tools/configure_esp_secure_cert.py")
        
        cmd = [
            sys.executable,
            str(configure_script),
            "-p", args.port,
            "--device-cert", str(device_cert),
            "--private-key", str(private_key),
            "--ca-cert", str(ca_cert),
            "--target_chip", args.target_chip,
            "--configure_ds",
            "--priv_key_algo", *args.priv_key_algo,
            "--efuse_key_id", str(args.efuse_key_id),
        ]
        
        if args.skip_flash:
            cmd.append("--skip_flash")
        
        if not run_cmd(cmd):
            print("错误: DS 证书烧录失败", file=sys.stderr)
            return 1
        print("✓ DS 证书烧录完成\n")

    # 2. 烧录 IoT 配置
    if not args.skip_iot:
        print("="*60)
        print("步骤 2: 烧录 IoT 配置")
        print("="*60)
        
        gen_iot_script = find_script("tools/gen_iot_config_nvs.py")
        
        cmd = [
            sys.executable,
            str(gen_iot_script),
            str(iot_config),
            "--flash",
            "--port", args.port,
        ]
        
        if not run_cmd(cmd):
            print("错误: IoT 配置烧录失败", file=sys.stderr)
            return 1
        print("✓ IoT 配置烧录完成\n")

    print("="*60)
    print("所有烧录任务完成！")
    print("="*60)
    return 0


if __name__ == "__main__":
    sys.exit(main())

