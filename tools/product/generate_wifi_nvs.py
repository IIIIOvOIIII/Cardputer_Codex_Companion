#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import os
from pathlib import Path
import subprocess
import tempfile


PARTITION_SIZE = 0x6000


def validate(ssid: str, password: str) -> None:
    if not ssid or len(ssid.encode("utf-8")) > 32:
        raise ValueError("SSID must contain 1..32 UTF-8 bytes")
    password_bytes = len(password.encode("utf-8"))
    if password_bytes not in range(8, 64) and password_bytes != 0:
        raise ValueError("Wi-Fi password must be empty or contain 8..63 bytes")


def write_wifi_csv(path: Path, ssid: str, password: str) -> None:
    validate(ssid, password)
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["key", "type", "encoding", "value"])
        writer.writerow(["wifi", "namespace", "", ""])
        writer.writerow(["ssid", "data", "string", ssid])
        writer.writerow(["password", "data", "string", password])


def generate(
    output: Path,
    generator: Path,
    idf_python: Path,
    ssid: str,
    password: str,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cardputer-wifi-nvs.") as raw:
        temporary = Path(raw)
        os.chmod(temporary, 0o700)
        csv_path = temporary / "wifi.csv"
        write_wifi_csv(csv_path, ssid, password)
        subprocess.run(
            [
                str(idf_python),
                str(generator),
                "generate",
                "--outdir",
                str(output.parent),
                str(csv_path),
                output.name,
                str(PARTITION_SIZE),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
    os.chmod(output, 0o600)
    if output.stat().st_size != PARTITION_SIZE:
        raise RuntimeError("generated Wi-Fi NVS has the wrong size")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--idf-python", type=Path, required=True)
    args = parser.parse_args()
    ssid = os.environ.get("CARDPUTER_WIFI_SSID", "")
    password = os.environ.get("CARDPUTER_WIFI_PASSWORD", "")
    generate(args.output, args.generator, args.idf_python, ssid, password)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
