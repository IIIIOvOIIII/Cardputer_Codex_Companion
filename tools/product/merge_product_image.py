#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import subprocess


@dataclass(frozen=True)
class ProductPaths:
    bootloader: Path
    partition_table: Path
    ota_data: Path
    application: Path
    wifi_nvs: Path | None = None


def segments(paths: ProductPaths) -> list[tuple[str, Path]]:
    values = [
        ("0x0", paths.bootloader),
        ("0x8000", paths.partition_table),
        ("0xf000", paths.ota_data),
    ]
    if paths.wifi_nvs is not None:
        values.append(("0x12000", paths.wifi_nvs))
    values.append(("0x20000", paths.application))
    return values


def merge(output: Path, idf_python: Path, paths: ProductPaths) -> None:
    for _, path in segments(paths):
        if not path.is_file():
            raise FileNotFoundError(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(idf_python),
        "-m",
        "esptool",
        "--chip",
        "esp32s3",
        "merge_bin",
        "--output",
        str(output),
        "--flash_mode",
        "dio",
        "--flash_freq",
        "80m",
        "--flash_size",
        "8MB",
    ]
    for offset, path in segments(paths):
        command.extend([offset, str(path)])
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    os.chmod(output, 0o600)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--idf-python", type=Path, required=True)
    parser.add_argument("--wifi-nvs", type=Path)
    args = parser.parse_args()
    build = args.build_dir
    merge(
        args.output,
        args.idf_python,
        ProductPaths(
            bootloader=build / "bootloader/bootloader.bin",
            partition_table=build / "partition_table/partition-table.bin",
            ota_data=build / "ota_data_initial.bin",
            application=build / "cardputer_codex_companion.bin",
            wifi_nvs=args.wifi_nvs,
        ),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
