#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime
import json
import os
import stat
import zipfile
from pathlib import Path, PurePosixPath


STATIC_ENTRIES = ("index.html", "manifest.json")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--source-date-epoch",
        type=int,
        default=None,
        help="ZIP timestamp; defaults to SOURCE_DATE_EPOCH",
    )
    return parser.parse_args()


def zip_timestamp(epoch: int) -> tuple[int, int, int, int, int, int]:
    value = datetime.datetime.fromtimestamp(max(epoch, 315532800), datetime.UTC)
    return (
        value.year,
        value.month,
        value.day,
        value.hour,
        value.minute,
        value.second - value.second % 2,
    )


def manifest_firmware_name(source: Path) -> str:
    manifest = json.loads((source / "manifest.json").read_text())
    try:
        path = manifest["builds"][0]["parts"][0]["path"]
    except (IndexError, KeyError, TypeError) as error:
        raise SystemExit("manifest factory path is missing") from error
    if not isinstance(path, str):
        raise SystemExit("manifest factory path must be a string")
    candidate = PurePosixPath(path)
    if candidate.is_absolute() or len(candidate.parts) != 1:
        raise SystemExit("manifest factory path must be same-origin")
    return candidate.name


def package(source: Path, firmware: Path, output: Path, epoch: int) -> None:
    missing = [
        name for name in STATIC_ENTRIES if not (source / name).is_file()
    ]
    if missing:
        raise SystemExit(f"missing web installer file: {', '.join(missing)}")
    firmware_name = manifest_firmware_name(source)
    if not firmware.is_file():
        raise SystemExit(f"missing factory firmware: {firmware}")
    if firmware.name != firmware_name:
        raise SystemExit("factory firmware name does not match manifest")

    output.parent.mkdir(parents=True, exist_ok=True)
    timestamp = zip_timestamp(epoch)
    with zipfile.ZipFile(output, "w") as archive:
        entries = [
            *((name, source / name) for name in STATIC_ENTRIES),
            (firmware_name, firmware),
        ]
        for name, path in entries:
            info = zipfile.ZipInfo(name, timestamp)
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(
                info,
                path.read_bytes(),
                compress_type=zipfile.ZIP_DEFLATED,
                compresslevel=9,
            )
    os.chmod(output, 0o644)


def main() -> int:
    arguments = parse_args()
    raw_epoch = arguments.source_date_epoch
    if raw_epoch is None:
        raw_epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "315532800"))
    package(
        arguments.source.resolve(),
        arguments.firmware.resolve(),
        arguments.output.resolve(),
        raw_epoch,
    )
    print(arguments.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
