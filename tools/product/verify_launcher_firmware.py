#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import subprocess


PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_SIZE = 0x1000
PARTITION_ENTRY_SIZE = 32
WIFI_CONFIG_OFFSET = 0x12000
WIFI_CONFIG_SIZE = 0x6000
STORAGE_BOUNDARY = 0x620000
STORAGE_MINIMUM_SIZE = 0x1E0000


@dataclass(frozen=True)
class PartitionEntry:
    partition_type: int
    subtype: int
    offset: int
    size: int
    label: str


def parse_partition_table(image: bytes) -> list[PartitionEntry]:
    table_end = PARTITION_TABLE_OFFSET + PARTITION_TABLE_SIZE
    if len(image) < table_end:
        raise ValueError("partition table is truncated")
    entries: list[PartitionEntry] = []
    for offset in range(
        PARTITION_TABLE_OFFSET,
        table_end,
        PARTITION_ENTRY_SIZE,
    ):
        raw = image[offset : offset + PARTITION_ENTRY_SIZE]
        if raw[:2] in (b"\xff\xff", b"\xeb\xeb"):
            break
        if raw[:2] != b"\xaa\x50":
            raise ValueError("partition table entry has invalid magic")
        label = raw[12:28].split(b"\0", 1)[0].decode("ascii")
        entries.append(
            PartitionEntry(
                partition_type=raw[2],
                subtype=raw[3],
                offset=int.from_bytes(raw[4:8], "little"),
                size=int.from_bytes(raw[8:12], "little"),
                label=label,
            )
        )
    if not entries:
        raise ValueError("partition table has no entries")
    return entries


def validate_launcher_image(image: bytes) -> list[PartitionEntry]:
    if len(image) != STORAGE_BOUNDARY:
        raise ValueError(
            "Launcher image must end exactly at storage boundary"
        )
    wifi = image[
        WIFI_CONFIG_OFFSET : WIFI_CONFIG_OFFSET + WIFI_CONFIG_SIZE
    ]
    if len(wifi) != WIFI_CONFIG_SIZE or any(value != 0xFF for value in wifi):
        raise ValueError("Wi-Fi configuration range is not erased")
    entries = parse_partition_table(image)
    storage = next(
        (entry for entry in entries if entry.label == "storage"),
        None,
    )
    if (
        storage is None
        or storage.partition_type != 1
        or storage.subtype != 0x82
        or storage.offset != STORAGE_BOUNDARY
        or storage.size < STORAGE_MINIMUM_SIZE
    ):
        raise ValueError("storage partition declaration is incompatible")
    application = next(
        (
            entry
            for entry in entries
            if entry.partition_type == 0
            and entry.subtype in (0x00, 0x10, 0x20)
        ),
        None,
    )
    if (
        application is None
        or application.offset >= STORAGE_BOUNDARY
        or image[application.offset : application.offset + 1] != b"\xe9"
    ):
        raise ValueError("application partition payload is missing")
    return entries


def verify_application(
    image: bytes,
    entries: list[PartitionEntry],
    application_path: Path,
    idf_python: Path,
    expected_version: str,
) -> None:
    application = next(
        entry
        for entry in entries
        if entry.partition_type == 0
        and entry.subtype in (0x00, 0x10, 0x20)
    )
    application_bytes = application_path.read_bytes()
    embedded = image[
        application.offset : application.offset + len(application_bytes)
    ]
    if embedded != application_bytes:
        raise ValueError("embedded application does not match build output")
    result = subprocess.run(
        [
            str(idf_python),
            "-m",
            "esptool",
            "image_info",
            "--version",
            "2",
            str(application_path),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    if "Detected image type: ESP32-S3" not in result.stdout:
        raise ValueError("application is not an ESP32-S3 image")
    if f"App version: {expected_version}" not in result.stdout:
        raise ValueError("application version does not match Launcher release")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--app-image", type=Path, required=True)
    parser.add_argument("--idf-python", type=Path, required=True)
    parser.add_argument("--expected-version", required=True)
    arguments = parser.parse_args()
    image = arguments.image.read_bytes()
    entries = validate_launcher_image(image)
    verify_application(
        image,
        entries,
        arguments.app_image,
        arguments.idf_python,
        arguments.expected_version,
    )
    print(
        "Launcher firmware verified: "
        f"{arguments.expected_version}, {len(image)} bytes, storage declared"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
