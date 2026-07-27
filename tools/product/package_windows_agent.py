#!/usr/bin/env python3
import argparse
import datetime
import os
import stat
import zipfile
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-date-epoch", type=int, required=True)
    return parser.parse_args()


def zip_timestamp(epoch: int) -> tuple[int, int, int, int, int, int]:
    value = datetime.datetime.fromtimestamp(epoch, datetime.UTC)
    if value.year < 1980:
        value = value.replace(year=1980, month=1, day=1)
    return (
        value.year,
        value.month,
        value.day,
        value.hour,
        value.minute,
        value.second - value.second % 2,
    )


def add_file(
    archive: zipfile.ZipFile,
    name: str,
    data: bytes,
    timestamp: tuple[int, int, int, int, int, int],
    mode: int,
) -> None:
    info = zipfile.ZipInfo(name, timestamp)
    info.create_system = 3
    info.external_attr = (stat.S_IFREG | mode) << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    archive.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def main() -> int:
    arguments = parse_args()
    root = arguments.root.resolve()
    timestamp = zip_timestamp(arguments.source_date_epoch)
    output = root / "dist"
    output.mkdir(parents=True, exist_ok=True)
    readme = (root / "windows-agent/README.txt").read_bytes()

    for architecture in ("amd64", "arm64"):
        executable = (
            root
            / "build"
            / "windows"
            / arguments.version
            / architecture
            / "cardputer-agent.exe"
        )
        if not executable.is_file():
            raise SystemExit(f"missing Windows binary: {executable}")
        destination = (
            output
            / f"CardputerCompanion-{arguments.version}-windows-{architecture}.zip"
        )
        prefix = (
            f"CardputerCompanion-{arguments.version}-windows-{architecture}/"
        )
        with zipfile.ZipFile(destination, "w") as archive:
            add_file(archive, prefix + "README.txt", readme, timestamp, 0o644)
            add_file(
                archive,
                prefix + "cardputer-agent.exe",
                executable.read_bytes(),
                timestamp,
                0o755,
            )
        os.chmod(destination, 0o644)
        print(destination)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
