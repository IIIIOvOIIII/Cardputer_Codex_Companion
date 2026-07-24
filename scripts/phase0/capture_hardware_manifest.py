#!/usr/bin/env python3
"""Capture and validate a hardware manifest from one probe runtime event."""

from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.phase0.validate_hardware_manifest import validate_manifest


def _read_runtime_event(payload: dict[str, Any]) -> dict[str, Any]:
  if "hardware_runtime" in payload and isinstance(payload["hardware_runtime"], dict):
    runtime = payload["hardware_runtime"]
  elif (
      payload.get("type") == "hardware_runtime"
      and isinstance(payload.get("value"), dict)
  ):
    runtime = payload["value"]
  else:
    runtime = payload

  if not isinstance(runtime, dict):
    raise ValueError("hardware_runtime payload is not a JSON object")
  if "chip_model" not in runtime:
    raise ValueError("hardware_runtime payload missing chip_model")
  if "chip_revision" not in runtime:
    raise ValueError("hardware_runtime payload missing chip_revision")
  if "flash_jedec_id" not in runtime:
    raise ValueError("hardware_runtime payload missing flash_jedec_id")
  if "flash_bytes" not in runtime:
    raise ValueError("hardware_runtime payload missing flash_bytes")
  if "psram_bytes" not in runtime:
    raise ValueError("hardware_runtime payload missing psram_bytes")
  return runtime


def _read_json_event(path: Path | None, stream) -> dict[str, Any]:
  if path is not None:
    raw_text = path.read_text(encoding="utf-8")
  else:
    raw_text = stream.read()

  lines = [line.strip() for line in raw_text.splitlines() if line.strip()]
  if not lines:
    raise ValueError("no hardware event data")

  for text in lines:
    try:
      payload = json.loads(text)
    except json.JSONDecodeError:
      continue
    if isinstance(payload, dict):
      return payload
  raise ValueError("hardware input is not valid JSON")

def _normalize_flash_jedec_id(value: Any) -> str:
  if isinstance(value, int):
    return f"{value:06x}"
  if not isinstance(value, str):
    raise ValueError("flash_jedec_id must be a string or integer")
  try:
    return f"{int(value, 16):06x}"
  except ValueError as exc:
    raise ValueError("flash_jedec_id must be hex") from exc

def _normalize_int(value: Any, field_name: str) -> int:
  if isinstance(value, bool):
    raise ValueError(f"{field_name} must be an integer")
  if isinstance(value, int):
    return value
  raise ValueError(f"{field_name} must be an integer")


def _read_product_and_pcb_revision(product_revision: str | None, pcb_revision: str | None):
  product_revision_value = _ask("product revision", product_revision)
  pcb_revision_value = _ask("PCB revision", pcb_revision)
  return product_revision_value, pcb_revision_value


def _build_payload(runtime: dict[str, Any], *, product_revision: str, pcb_revision: str, usb_serial_sha256: str) -> dict[str, Any]:
  return {
    "manifest_version": 1,
    "model": "M5Stack Cardputer",
    "product_revision": product_revision,
    "pcb_revision": pcb_revision,
    "chip_model": runtime["chip_model"],
    "chip_revision": _normalize_int(runtime["chip_revision"], "chip_revision"),
    "flash_jedec_id": _normalize_flash_jedec_id(runtime["flash_jedec_id"]),
    "flash_bytes": _normalize_int(runtime["flash_bytes"], "flash_bytes"),
    "psram_bytes": _normalize_int(runtime["psram_bytes"], "psram_bytes"),
    "usb_serial_sha256": usb_serial_sha256,
    "keyboard_matrix_source": {
      "repository": "m5stack/M5Cardputer",
      "commit": "2d4fa6646e4e5b47e0af96214b003aa7b15b8d81",
      "outputs": [8, 9, 11],
      "inputs": [13, 15, 3, 4, 5, 6, 7],
      "physically_verified": True,
    },
    "captured_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
  }


def _ask(label: str, arg_value: str | None = None) -> str:
  value = arg_value.strip() if arg_value else ""
  if value:
    return value
  if not sys.stdin.isatty():
    raise RuntimeError(
      f"{label} is required and no tty is available; pass --{label.replace(' ', '-')}"
    )
  while True:
    value = input(f"{label}: ").strip()
    if value:
      return value


def capture_from_event(raw_payload: dict[str, Any], *, product_revision: str, pcb_revision: str, usb_serial: str) -> dict[str, Any]:
  runtime = _read_runtime_event(raw_payload)
  manifest = _build_payload(
    runtime,
    product_revision=product_revision,
    pcb_revision=pcb_revision,
    usb_serial_sha256=hashlib.sha256(usb_serial.encode("utf-8")).hexdigest(),
  )
  errors = validate_manifest(manifest)
  if errors:
    raise ValueError("hardware manifest validation failed: " + "; ".join(errors))
  return manifest


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser()
  parser.add_argument("--input", type=Path, default=None)
  parser.add_argument("--output", type=Path, default=Path("build/phase0/hardware-manifest.json"))
  parser.add_argument("--product-revision", dest="product_revision")
  parser.add_argument("--pcb-revision", dest="pcb_revision")
  parser.add_argument("--usb-serial", dest="usb_serial")
  return parser.parse_args()


def main() -> int:
  args = parse_args()
  try:
    payload = _read_json_event(args.input, sys.stdin)
    product_revision, pcb_revision = _read_product_and_pcb_revision(
        args.product_revision, args.pcb_revision
    )
    usb_serial = _ask("USB serial", args.usb_serial)
    manifest = capture_from_event(
      payload,
      product_revision=product_revision,
      pcb_revision=pcb_revision,
      usb_serial=usb_serial,
    )
  except ValueError as exc:
    print(f"error: {exc}", file=sys.stderr)
    return 2

  args.output.parent.mkdir(parents=True, exist_ok=True)
  args.output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
  print(f"wrote {args.output}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
