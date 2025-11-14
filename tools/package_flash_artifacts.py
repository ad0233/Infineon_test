#!/usr/bin/env python3
"""
将编译产物拷贝到固定offset目录，并生成烧录命令/清单
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path


FLASH_SEGMENTS = [
    ("bootloader", Path("bootloader") / "bootloader.bin", 0x0000),
    ("partition-table", Path("partition_table") / "partition-table.bin", 0x8000),
    ("ota-data-initial", Path("ota_data_initial.bin"), 0xE000),
    ("app", Path("Lunawake.bin"), 0x10000),
]


def format_offset(offset: int) -> str:
    return f"0x{offset:06X}"


def ensure_file(path: Path) -> Path:
    if not path.exists():
        raise FileNotFoundError(f"缺少文件: {path}")
    return path


def build_flash_command(args, segments_info):
    cmd_parts = [
        args.python_bin,
        "-m",
        "esptool",
        "--chip",
        args.chip,
        "-b",
        str(args.baud),
        "--before",
        args.before,
        "--after",
        args.after,
        "write_flash",
        "--flash_mode",
        args.flash_mode,
        "--flash_size",
        args.flash_size,
        "--flash_freq",
        args.flash_freq,
    ]
    for seg in segments_info:
        cmd_parts.append(seg["offset"])
        cmd_parts.append(seg["filename"])
    return " ".join(cmd_parts)


def copy_segments(build_dir: Path, output_dir: Path):
    copied = []
    for name, relative_path, offset in FLASH_SEGMENTS:
        src = ensure_file(build_dir / relative_path)
        dest_name = f"{name}_{format_offset(offset)}.bin"
        dest = output_dir / dest_name
        shutil.copy2(src, dest)
        copied.append(
            {
                "name": name,
                "offset": format_offset(offset),
                "filename": dest_name,
                "source": str(src.resolve()),
                "size": dest.stat().st_size,
            }
        )
    return copied


def write_manifest(output_dir: Path, manifest: dict):
    manifest_path = output_dir / "flash_manifest.json"
    with manifest_path.open("w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
    return manifest_path


def write_command_file(output_dir: Path, command: str):
    cmd_path = output_dir / "flash_command.txt"
    with cmd_path.open("w", encoding="utf-8") as f:
        f.write(command + "\n")
    return cmd_path


def get_git_hash() -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return result.stdout.strip() or "unknown"
    except Exception:
        return "unknown"


def parse_args(argv):
    parser = argparse.ArgumentParser(description="打包烧录所需bin文件")
    parser.add_argument("--build-dir", default="build", help="编译输出目录")
    parser.add_argument("--output-dir", default="tools/offset", help="输出目录")
    parser.add_argument("--python-bin", default="python", help="调用esptool的python解释器")
    parser.add_argument("--chip", default="esp32s3")
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--before", default="default_reset")
    parser.add_argument("--after", default="hard_reset")
    parser.add_argument("--flash-mode", default="dout")
    parser.add_argument("--flash-size", default="detect")
    parser.add_argument("--flash-freq", default="80m")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv or sys.argv[1:])
    build_dir = Path(args.build_dir).resolve()
    output_root = Path(args.output_dir).resolve()
    output_root.mkdir(parents=True, exist_ok=True)

    git_hash = get_git_hash()
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    package_dir_name = f"{timestamp}_{git_hash}"
    output_dir = output_root / package_dir_name
    output_dir.mkdir(parents=True, exist_ok=True)

    segments_info = copy_segments(build_dir, output_dir)
    flash_command = build_flash_command(args, segments_info)

    manifest = {
        "timestamp": timestamp,
        "git_hash": git_hash,
        "chip": args.chip,
        "baud": args.baud,
        "before": args.before,
        "after": args.after,
        "flash_mode": args.flash_mode,
        "flash_size": args.flash_size,
        "flash_freq": args.flash_freq,
        "segments": segments_info,
        "flash_command": flash_command,
    }
    manifest_path = write_manifest(output_dir, manifest)
    cmd_path = write_command_file(output_dir, flash_command)

    print(f"输出目录: {output_dir}")
    print(f"拷贝完成，生成 {len(segments_info)} 个bin文件")
    print(f"清单: {manifest_path}")
    print(f"烧录命令: {cmd_path}")


if __name__ == "__main__":
    main()


