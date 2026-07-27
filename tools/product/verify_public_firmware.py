#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from pathlib import Path
import sys


def number(value: str) -> int:
    normalized = value.strip().lower()
    multiplier = 1
    if normalized.endswith("k"):
        normalized = normalized[:-1]
        multiplier = 1024
    elif normalized.endswith("m"):
        normalized = normalized[:-1]
        multiplier = 1024 * 1024
    return int(normalized, 0) * multiplier


def wifi_partition(layout: Path) -> tuple[int, int]:
    for row in csv.reader(layout.read_text(encoding="utf-8").splitlines()):
        if not row or row[0].lstrip().startswith("#"):
            continue
        if row[0].strip() == "wifi_cfg":
            return number(row[3]), number(row[4])
    raise ValueError("wifi_cfg partition is missing")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--layout", type=Path, required=True)
    arguments = parser.parse_args()
    offset, size = wifi_partition(arguments.layout)
    with arguments.image.open("rb") as image:
        image.seek(offset)
        payload = image.read(size)
    if len(payload) != size:
        print("public firmware does not cover the Wi-Fi NVS partition", file=sys.stderr)
        return 2
    if any(value != 0xFF for value in payload):
        print("public firmware Wi-Fi NVS partition is not erased", file=sys.stderr)
        return 2
    print(
        "public firmware verified: wifi_cfg is erased "
        f"at {offset:#x}+{size:#x}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
