#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


DEFAULT_MINIMUM_DIRAM_BYTES = 96 * 1024


def validate_firmware_memory(
    report_path: Path,
    *,
    minimum_diram_bytes: int = DEFAULT_MINIMUM_DIRAM_BYTES,
) -> int:
    report = json.loads(report_path.read_text(encoding="utf-8"))
    remaining = report.get("diram_remain")
    if not isinstance(remaining, int) or isinstance(remaining, bool):
        raise ValueError("size report is missing integer diram_remain")
    if remaining < minimum_diram_bytes:
        raise ValueError(
            "DIRAM headroom "
            f"{remaining} is below required {minimum_diram_bytes} bytes"
        )
    return remaining


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    parser.add_argument(
        "--minimum-diram-bytes",
        type=int,
        default=DEFAULT_MINIMUM_DIRAM_BYTES,
    )
    args = parser.parse_args()
    remaining = validate_firmware_memory(
        args.report,
        minimum_diram_bytes=args.minimum_diram_bytes,
    )
    print(f"Firmware DIRAM headroom: {remaining} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
