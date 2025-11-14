#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def resolve_generator(idf_path: Path) -> Path:
    script = idf_path / "components" / "nvs_flash" / "nvs_partition_generator" / "nvs_partition_gen.py"
    if not script.exists():
        raise FileNotFoundError(f"未找到 nvs_partition_gen.py: {script}")
    return script


def write_csv(json_text: str, csv_path: Path) -> None:
    csv_content = (
        "key,type,encoding,value\n"
        "iot_config,namespace,,\n"
        f'json,string,string,"{json_text.replace(\'"\', \'""\')}"\n'
    )
    csv_path.write_text(csv_content, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(
        description="将 iot_config.json 转换为 NVS 分区镜像 (iot_config 分区专用)"
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

    args = parser.parse_args()

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
        subprocess.check_call(cmd)
        print(f"已生成: {args.output.resolve()}")
    finally:
        if not args.csv and csv_path.exists():
            try:
                csv_path.unlink()
            except OSError:
                pass


if __name__ == "__main__":
    raise SystemExit(main())

