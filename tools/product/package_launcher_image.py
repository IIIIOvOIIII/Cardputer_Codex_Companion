#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import shutil

try:
    from tools.product.merge_product_image import ProductPaths, merge
    from tools.product.verify_launcher_firmware import (
        STORAGE_BOUNDARY,
        STORAGE_PAYLOAD_BYTES,
        validate_launcher_image,
        verify_application,
    )
except ModuleNotFoundError as error:
    if error.name != "tools":
        raise
    from merge_product_image import ProductPaths, merge
    from verify_launcher_firmware import (
        STORAGE_BOUNDARY,
        STORAGE_PAYLOAD_BYTES,
        validate_launcher_image,
        verify_application,
    )


def pad_launcher_image(
    source: Path,
    output: Path,
    boundary: int = STORAGE_BOUNDARY,
) -> None:
    size = source.stat().st_size
    if size >= boundary:
        raise ValueError("merged image reaches storage boundary")
    output.parent.mkdir(parents=True, exist_ok=True)
    with source.open("rb") as reader, output.open("wb") as writer:
        shutil.copyfileobj(reader, writer)
        writer.write(
            b"\xff"
            * (boundary - size + STORAGE_PAYLOAD_BYTES)
        )
    output.chmod(0o600)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--idf-python", type=Path, required=True)
    parser.add_argument("--expected-version", default="1.3.4l")
    arguments = parser.parse_args()
    build = arguments.build_dir
    merged = arguments.output.with_suffix(
        arguments.output.suffix + ".merged"
    )
    try:
        merge(
            merged,
            arguments.idf_python,
            ProductPaths(
                bootloader=build / "bootloader/bootloader.bin",
                partition_table=(
                    build / "partition_table/partition-table.bin"
                ),
                ota_data=build / "ota_data_initial.bin",
                application=build / "cardputer_codex_companion.bin",
            ),
        )
        pad_launcher_image(merged, arguments.output)
    finally:
        merged.unlink(missing_ok=True)
    image = arguments.output.read_bytes()
    entries = validate_launcher_image(image)
    verify_application(
        image,
        entries,
        build / "cardputer_codex_companion.bin",
        arguments.idf_python,
        arguments.expected_version,
    )
    print(f"Launcher image: {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
