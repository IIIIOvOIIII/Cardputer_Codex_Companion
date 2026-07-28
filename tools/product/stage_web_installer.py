#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
import shutil
from pathlib import Path, PurePosixPath


STATIC_FILES = ("index.html", "manifest.json")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


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


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def stage(
    source: Path,
    firmware: Path,
    expected_sha256: str,
    output: Path,
) -> None:
    for name in STATIC_FILES:
        if not (source / name).is_file():
            raise SystemExit(f"missing web installer file: {name}")
    if not firmware.is_file():
        raise SystemExit(f"missing factory firmware: {firmware}")

    firmware_name = manifest_firmware_name(source)
    if firmware.name != firmware_name:
        raise SystemExit("factory firmware name does not match manifest")
    actual_sha256 = file_sha256(firmware)
    if not hmac.compare_digest(actual_sha256, expected_sha256.lower()):
        raise SystemExit(
            "firmware sha256 mismatch: "
            f"expected {expected_sha256.lower()}, got {actual_sha256}"
        )
    if output.exists():
        raise SystemExit(f"web installer output already exists: {output}")

    output.mkdir(parents=True)
    for name in STATIC_FILES:
        shutil.copyfile(source / name, output / name)
        os.chmod(output / name, 0o644)
    shutil.copyfile(firmware, output / firmware_name)
    os.chmod(output / firmware_name, 0o644)


def main() -> int:
    arguments = parse_args()
    stage(
        arguments.source.resolve(),
        arguments.firmware.resolve(),
        arguments.expected_sha256,
        arguments.output.resolve(),
    )
    print(arguments.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
