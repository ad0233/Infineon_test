#!/usr/bin/env python3

import argparse
import re
from pathlib import Path

CLEAR_CALL_TEMPLATE = r'^(?P<indent>\s*){name}\.data\s*=\s*UI_LOAD_IMAGE\([\s\S]*?\);'
CLEAR_CALL_RE = re.compile(
    CLEAR_CALL_TEMPLATE,
    re.MULTILINE,
)


def ensure_externs(content: str, symbol_base: str) -> str:
    symbol_start = f"_binary_{symbol_base}_start"
    extern_pattern = f"extern const uint8_t {symbol_start}[];"
    if extern_pattern in content:
        return content

    symbol_end = f"_binary_{symbol_base}_end"
    extern_declaration = (
        f'extern const uint8_t {symbol_start}[];\n'
        f'extern const uint8_t {symbol_end}[];\n'
    )

    include_match = re.search(r'#include\s+"../ui\.h"\s*\n', content)
    if not include_match:
        return content

    insert_pos = include_match.end()
    before = content[:insert_pos]
    after = content[insert_pos:]

    if not before.endswith("\n\n"):
        extern_block = "\n" + extern_declaration + "\n"
    else:
        extern_block = extern_declaration + "\n"

    return before + extern_block + after


def process_file(path: Path) -> bool:
    original = path.read_text(encoding="utf-8")
    stem = path.stem
    symbol_base = f"{stem}_bin"

    data_size_match = re.search(r'\.data_size\s*=\s*(\d+);', original)
    if not data_size_match:
        return False
    data_size = data_size_match.group(1)

    call_pattern = re.compile(
        CLEAR_CALL_TEMPLATE.format(name=re.escape(stem)),
        CLEAR_CALL_RE.flags,
    )
    call_match = call_pattern.search(original)
    if not call_match:
        return False

    indent = call_match.group("indent")
    prefix = f"{indent}{stem}.data = "
    alias_start = f"_binary_{symbol_base}_start"
    alias_end = f"_binary_{symbol_base}_end"
    new_call = (
        f"{prefix}UI_LOAD_IMAGE(\n"
        f"{indent}    {alias_start},\n"
        f"{indent}    (uint32_t)({alias_end} - {alias_start}),\n"
        f"{indent}    {data_size});"
    )

    updated = original[:call_match.start()] + new_call + original[call_match.end():]
    updated = ensure_externs(updated, symbol_base)

    if updated != original:
        path.write_text(updated, encoding="utf-8")
        return True
    return False


def main():
    parser = argparse.ArgumentParser(description="Update UI_LOAD_IMAGE calls to use embedded assets")
    parser.add_argument(
        "--root",
        default="components/Lunawake/images",
        help="Directory containing generated image source files",
    )
    args = parser.parse_args()

    root_dir = Path(args.root)
    if not root_dir.is_dir():
        raise SystemExit(f"Directory not found: {root_dir}")

    updated = 0
    for path in sorted(root_dir.glob("ui_img_*.c")):
        if process_file(path):
            updated += 1

    print(f"Updated {updated} files.")


if __name__ == "__main__":
    main()


