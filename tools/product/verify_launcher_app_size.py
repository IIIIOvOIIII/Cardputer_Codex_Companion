#!/usr/bin/env python3
"""Verify compatibility with the existing M5Launcher cardpu partition."""

from __future__ import annotations

import argparse
from pathlib import Path


M5LAUNCHER_CARDPU_PARTITION_SIZE = 0x190000


def validate_launcher_app_size(image: Path) -> None:
    size = image.stat().st_size
    if size > M5LAUNCHER_CARDPU_PARTITION_SIZE:
        raise ValueError(
            "Launcher app does not fit the M5Launcher cardpu partition: "
            f"{size} > {M5LAUNCHER_CARDPU_PARTITION_SIZE} bytes"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True, type=Path)
    args = parser.parse_args()
    try:
        validate_launcher_app_size(args.image)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(
        "Launcher app size OK: "
        f"{args.image.stat().st_size}/"
        f"{M5LAUNCHER_CARDPU_PARTITION_SIZE} bytes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
