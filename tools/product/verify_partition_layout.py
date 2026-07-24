#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
import subprocess
import sys


@dataclass(frozen=True)
class Partition:
    name: str
    type: str
    subtype: str
    offset: int
    size: int
    flags: str


def parse_number(value: str) -> int:
    normalized = value.strip().lower()
    multiplier = 1
    if normalized.endswith("k"):
        multiplier = 1024
        normalized = normalized[:-1]
    elif normalized.endswith("m"):
        multiplier = 1024 * 1024
        normalized = normalized[:-1]
    return int(normalized, 0) * multiplier


def parse_layout(text: str) -> list[Partition]:
    partitions: list[Partition] = []
    for row in csv.reader(text.splitlines()):
        if not row or not row[0].strip() or row[0].lstrip().startswith("#"):
            continue
        if len(row) < 5:
            raise ValueError(f"invalid partition row: {row!r}")
        values = [value.strip() for value in row]
        partitions.append(
            Partition(
                name=values[0],
                type=values[1],
                subtype=values[2],
                offset=parse_number(values[3]),
                size=parse_number(values[4]),
                flags=values[5] if len(values) > 5 else "",
            )
        )
    return partitions


def compare_layouts(
    expected: list[Partition], actual: list[Partition]
) -> list[str]:
    errors: list[str] = []
    expected_by_name = {partition.name: partition for partition in expected}
    actual_by_name = {partition.name: partition for partition in actual}

    for name in expected_by_name.keys() - actual_by_name.keys():
        errors.append(f"missing partition: {name}")
    for name in actual_by_name.keys() - expected_by_name.keys():
        errors.append(f"unexpected partition: {name}")

    for name in expected_by_name.keys() & actual_by_name.keys():
        wanted = expected_by_name[name]
        found = actual_by_name[name]
        for field in ("type", "subtype", "offset", "size", "flags"):
            expected_value = getattr(wanted, field)
            actual_value = getattr(found, field)
            if expected_value != actual_value:
                if field in ("offset", "size"):
                    expected_value = hex(expected_value)
                    actual_value = hex(actual_value)
                errors.append(
                    f"{name} {field}: expected {expected_value}, "
                    f"found {actual_value}"
                )

    expected_order = [partition.name for partition in expected]
    actual_order = [partition.name for partition in actual]
    if expected_order != actual_order:
        errors.append(
            "partition order: expected "
            f"{','.join(expected_order)}, found {','.join(actual_order)}"
        )
    return sorted(errors)


def dump_binary(parser: Path, binary: Path) -> str:
    result = subprocess.run(
        [sys.executable, str(parser), str(binary)],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    argument_parser = argparse.ArgumentParser(
        description="Verify the built ESP-IDF partition table exactly."
    )
    argument_parser.add_argument(
        "--expected",
        type=Path,
        default=repo_root / "firmware/partitions_product.csv",
    )
    argument_parser.add_argument(
        "--actual-bin",
        type=Path,
        default=repo_root
        / "firmware/build/partition_table/partition-table.bin",
    )
    argument_parser.add_argument(
        "--idf-parser",
        type=Path,
        default=repo_root
        / ".tools/esp-idf/components/partition_table/gen_esp32part.py",
    )
    args = argument_parser.parse_args()

    expected = parse_layout(args.expected.read_text(encoding="utf-8"))
    actual = parse_layout(dump_binary(args.idf_parser, args.actual_bin))
    errors = compare_layouts(expected, actual)
    if errors:
        print("product partition layout verification failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 2

    print(
        "product partition layout verified: "
        + ", ".join(partition.name for partition in actual)
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
