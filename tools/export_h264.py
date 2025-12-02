#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path
import shutil


def find_ffmpeg() -> str:
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        raise RuntimeError("ffmpeg executable not found in PATH")
    return ffmpeg


def export_file(ffmpeg: str, src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        ffmpeg,
        "-y",
        "-i",
        str(src),
        "-r",
        "15",
        "-c:v",
        "libx264",
        "-profile:v",
        "baseline",
        "-level",
        "3.0",
        "-x264-params",
        "cabac=0:ref=1",
        "-pix_fmt",
        "yuv420p",
        str(dst),
    ]
    print(f"Exporting {src} -> {dst}")
    subprocess.run(cmd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Batch export H.264 streams from MP4 files.")
    parser.add_argument(
        "--src",
        type=Path,
        default=Path("components/my_h264/mp4"),
        help="Source directory containing MP4 files.",
    )
    parser.add_argument(
        "--dst",
        type=Path,
        default=Path("components/my_h264/h264"),
        help="Destination directory for H.264 files.",
    )
    args = parser.parse_args()

    ffmpeg = find_ffmpeg()

    if not args.src.is_dir():
        print(f"Source directory not found: {args.src}", file=sys.stderr)
        return 1

    mp4_files = sorted(args.src.glob("*.mp4"))
    if not mp4_files:
        print(f"No MP4 files found in {args.src}")
        return 0

    for src_path in mp4_files:
        dst_path = args.dst / (src_path.stem + ".h264")
        try:
            export_file(ffmpeg, src_path, dst_path)
        except subprocess.CalledProcessError as exc:
            print(f"ffmpeg failed for {src_path} (exit code {exc.returncode})", file=sys.stderr)
            return exc.returncode
        except Exception as exc:  # pylint: disable=broad-except
            print(f"Error exporting {src_path}: {exc}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())

