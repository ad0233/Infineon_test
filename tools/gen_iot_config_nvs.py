#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
import csv
import re


def resolve_generator(idf_path: Path) -> Path:
    script = idf_path / "components" / "nvs_flash" / "nvs_partition_generator" / "nvs_partition_gen.py"
    if not script.exists():
        raise FileNotFoundError(f"未找到 nvs_partition_gen.py: {script}")
    return script


def write_csv(json_text: str, csv_path: Path) -> None:
    # 使用 csv.writer 正确转义，避免 f-string 内部转义带来的语法问题
    with csv_path.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f)
        w.writerow(["key", "type", "encoding", "value"])
        w.writerow(["iot_config", "namespace", "", ""]) 
        # 将 JSON 文本作为字符串写入，编码类型为 string
        w.writerow(["json", "string", "string", json_text])


def parse_partition_table_csv(csv_path: Path) -> dict:
    offsets = {}
    if not csv_path.exists():
        return offsets
    with csv_path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 5:
                continue
            name, ptype, subtype, offset, size = parts[:5]
            offsets[name] = offset
    return offsets


def run_cmd_capture(cmd: list[str]) -> str:
    return subprocess.check_output(cmd, text=True).strip()


def run_cmd(cmd: list[str]) -> None:
    subprocess.check_call(cmd)


def ensure_partition_table(project_root: Path, idf_path: Path) -> Path:
    # 确保生成 build/partition_table/partition-table.csv
    build_csv = project_root / "build" / "partition_table" / "partition-table.csv"
    if build_csv.exists():
        return build_csv
    # 运行 idf.py 生成分区表
    try:
        run_cmd(["idf.py", "partition-table"])  # 在当前工程目录执行
    except FileNotFoundError:
        raise SystemExit("未找到 idf.py，请先执行: source ~/esp/esp-adf/esp-idf/export.sh")
    return build_csv


def parse_offset_from_partition_output(output: str, part_name: str) -> str:
    # 匹配如：iot_config,data,nvs,0x15000,16K,
    for line in output.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.lower().startswith(part_name + ","):
            m = re.search(r",(0x[0-9a-fA-F]+),", line)
            if m:
                return m.group(1)
    return ""


def resolve_iot_config_offset_from_device(port: str, pt_offset: str | None) -> str:
    # 优先使用用户指定的分区表偏移
    candidates = []
    if pt_offset:
        candidates.append(pt_offset)
    # 常见偏移尝试
    for off in ("0x8000", "0xC000"):
        if off not in candidates:
            candidates.append(off)
    for off in candidates:
        try:
            out = run_cmd_capture([
                "esptool.py", "--port", port, "parttool",
                "--partition-table-offset", off,
                "get_partition_info", "--partition-name", "iot_config", "--info", "offset",
            ])
            val = out.strip()
            if val.lower().startswith("0x"):
                return val
        except subprocess.CalledProcessError:
            continue
    return ""


def resolve_iot_config_offset(project_root: Path, idf_path: Path) -> str:
    # 优先使用构建后的 partition-table.csv；没有则尝试生成
    build_csv = ensure_partition_table(project_root, idf_path)
    offsets = parse_partition_table_csv(build_csv)
    off = offsets.get("iot_config")
    if off and off.lower().startswith("0x"):
        return off
    # 解析 idf.py partition-table 标准输出
    try:
        out = run_cmd_capture(["idf.py", "partition-table"])
        off = parse_offset_from_partition_output(out, "iot_config")
        if off:
            return off
    except Exception:
        pass
    # 其次尝试项目根 partitions.csv（若填了偏移）
    root_csv = project_root / "partitions.csv"
    offsets = parse_partition_table_csv(root_csv)
    off = offsets.get("iot_config")
    if off and off.lower().startswith("0x"):
        return off
    return ""


def main():
    parser = argparse.ArgumentParser(
        description="将 iot_config.json 转换为 NVS 分区镜像 (iot_config 分区专用) 并可选自动烧录"
    )
    parser.add_argument("json", type=Path, help="iot_config.json 路径")
    parser.add_argument(
        "-o", "--output", type=Path, default=Path("build/iot_config_nvs.bin"),
        help="输出 NVS bin 路径 (默认: build/iot_config_nvs.bin)"
    )
    parser.add_argument(
        "--csv", type=Path,
        help="如需保留中间 CSV，请指定路径；若不指定则使用临时文件"
    )
    parser.add_argument(
        "--size", default="0x4000",
        help="生成的 NVS 分区大小，默认 0x4000"
    )
    parser.add_argument(
        "--idf-path", type=Path,
        help="ESP-IDF 路径，未指定则读取环境变量 IDF_PATH"
    )
    parser.add_argument(
        "--flash", action="store_true", help="生成后自动烧录到 iot_config 分区"
    )
    parser.add_argument(
        "--port", help="串口，如 /dev/ttyUSB0"
    )
    parser.add_argument(
        "--offset", help="iot_config 分区偏移（例如 0x15000）。若不提供则自动解析"
    )
    parser.add_argument(
        "--from-device", action="store_true", help="从设备分区表读取 iot_config 偏移（无需 build）"
    )
    parser.add_argument(
        "--pt-offset", help="分区表偏移（与 --from-device 搭配，可不填，自动尝试 0x8000/0xC000）"
    )

    args = parser.parse_args()

    project_root = Path.cwd()

    if not args.json.exists():
        print(f"输入 JSON 不存在: {args.json}", file=sys.stderr)
        return 1

    idf_path = args.idf_path or os.environ.get("IDF_PATH")
    if not idf_path:
        print("请通过 --idf-path 或环境变量 IDF_PATH 指定 ESP-IDF 路径", file=sys.stderr)
        return 1
    idf_path = Path(idf_path).resolve()

    generator = resolve_generator(idf_path)

    json_text = json.dumps(json.loads(args.json.read_text(encoding="utf-8")), separators=(",", ":"), ensure_ascii=False)

    if args.csv:
        csv_path = args.csv.resolve()
    else:
        tmp = tempfile.NamedTemporaryFile(prefix="iot_config_", suffix=".csv", delete=False)
        tmp.close()
        csv_path = Path(tmp.name)

    try:
        write_csv(json_text, csv_path)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        cmd = [
            sys.executable,
            str(generator),
            "generate",
            str(csv_path),
            str(args.output.resolve()),
            args.size,
        ]
        run_cmd(cmd)
        print(f"已生成: {args.output.resolve()}")

        if args.flash:
            if not args.port:
                print("--flash 需要指定 --port", file=sys.stderr)
                return 2
            # 解析偏移：优先 --offset ；否则 --from-device；否则 build/输出解析
            offset = args.offset or ""
            if not offset and args.from_device:
                offset = resolve_iot_config_offset_from_device(args.port, args.pt_offset)
            if not offset:
                offset = resolve_iot_config_offset(project_root, idf_path)
            if not offset:
                print("无法解析 iot_config 偏移，请检查分区表或通过 --offset 指定（如 0x15000）", file=sys.stderr)
                return 3
            print(f"烧录到 iot_config 分区, offset={offset}, port={args.port}")
            run_cmd([
                "esptool.py", "--port", args.port, "write_flash", offset, str(args.output.resolve())
            ])
            print("烧录完成")
    finally:
        if not args.csv and csv_path.exists():
            try:
                csv_path.unlink()
            except OSError:
                pass

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

