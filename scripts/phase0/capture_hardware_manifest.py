#!/usr/bin/env python3
"""Capture and validate a hardware manifest from one probe runtime event."""

from __future__ import annotations

import argparse
import hashlib
import json
import getpass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable
from typing import Any
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.phase0.validate_hardware_manifest import validate_manifest


def _coerce_physical_verification(raw_value: str | None) -> bool:
  if raw_value is None:
    return False
  normalized = raw_value.strip().lower()
  if normalized in {"yes", "y", "true", "1"}:
    return True
  if normalized in {"no", "n", "false", "0", ""}:
    return False
  raise ValueError(
    "--physically-verified must be one of yes/no, true/false, y/n, or 1/0"
  )


def _read_runtime_payload(raw_payload: Any) -> dict[str, Any]:
  if not isinstance(raw_payload, dict):
    raise ValueError("hardware_runtime payload is not a JSON object")
  if "chip_model" not in raw_payload:
    raise ValueError("hardware_runtime payload missing chip_model")
  if "chip_revision" not in raw_payload:
    raise ValueError("hardware_runtime payload missing chip_revision")
  if "flash_jedec_id" not in raw_payload:
    raise ValueError("hardware_runtime payload missing flash_jedec_id")
  if "flash_bytes" not in raw_payload:
    raise ValueError("hardware_runtime payload missing flash_bytes")
  if "psram_bytes" not in raw_payload:
    raise ValueError("hardware_runtime payload missing psram_bytes")
  return raw_payload


def _extract_runtime_candidates(payload: Any) -> Iterable[Any]:
  if isinstance(payload, list):
    return payload
  return [payload]


def _extract_hardware_runtime(payload: dict[str, Any]) -> dict[str, Any] | None:
  if payload.get("type") == "hardware_runtime":
    if "value" in payload:
      runtime = payload["value"]
      if not isinstance(runtime, dict):
        raise ValueError("hardware_runtime event value must be a JSON object")
      return runtime
    return payload

  if "hardware_runtime" in payload:
    runtime = payload["hardware_runtime"]
    if not isinstance(runtime, dict):
      raise ValueError("hardware_runtime event must be a JSON object")
    return runtime

  return None


def _read_runtime_event(payload: dict[str, Any]) -> dict[str, Any]:
  runtime = _extract_hardware_runtime(payload)
  if runtime is None:
    raise ValueError("no hardware_runtime event found")
  return _read_runtime_payload(runtime)


def _count_hardware_runtime_events(text: str) -> list[dict[str, Any]]:
  events: list[dict[str, Any]] = []
  for line in text.splitlines():
    line = line.strip()
    if not line:
      continue
    try:
      payload = json.loads(line)
    except json.JSONDecodeError:
      continue
    for candidate in _extract_runtime_candidates(payload):
      if not isinstance(candidate, dict):
        continue
      runtime = _extract_hardware_runtime(candidate)
      if runtime is None:
        continue
      events.append(_read_runtime_payload(runtime))
  return events


def _read_json_event(path: Path | None, stream) -> dict[str, Any]:
  if path is not None:
    raw_text = path.read_text(encoding="utf-8")
  else:
    raw_text = stream.read()

  events = _count_hardware_runtime_events(raw_text)
  if not events:
    raise ValueError("expected exactly one hardware_runtime event, found 0")
  if len(events) != 1:
    raise ValueError(
      f"expected exactly one hardware_runtime event, found {len(events)}"
    )
  return events[0]

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


def _read_usb_serial_sha256(raw_value: str | None) -> str:
  if raw_value is None:
    if not sys.stdin.isatty():
      raise RuntimeError(
        "--usb-serial-sha256 is required in non-interactive mode"
      )
    raw_value = getpass.getpass("USB serial: ").strip()
    if not raw_value:
      raise RuntimeError("usb serial is required")
    return hashlib.sha256(raw_value.encode("utf-8")).hexdigest()

  if len(raw_value) != 64:
    raise ValueError("usb_serial_sha256 must be 64 hex chars")
  if any(c not in "0123456789abcdefABCDEF" for c in raw_value):
    raise ValueError("usb_serial_sha256 must be hex")
  return raw_value.lower()


def _ask_physical_verification() -> bool:
  if not sys.stdin.isatty():
    raise RuntimeError(
      "--physically-verified is required in non-interactive mode"
    )
  while True:
    response = input("Has the keyboard matrix source been physically verified? [y/N]: ")
    normalized = response.strip().lower()
    if normalized in {"", "n", "no", "0", "false"}:
      return False
    if normalized in {"y", "yes", "1", "true"}:
      return True
    print("Please answer y/yes or n/no", file=sys.stderr)


def _read_product_and_pcb_revision(product_revision: str | None, pcb_revision: str | None):
  product_revision_value = _ask("product revision", product_revision)
  pcb_revision_value = _ask("PCB revision", pcb_revision)
  return product_revision_value, pcb_revision_value


def _build_payload(
  runtime: dict[str, Any],
  *,
  product_revision: str,
  pcb_revision: str,
  usb_serial_sha256: str,
) -> dict[str, Any]:
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


def _hash_usb_serial(raw_value: str) -> str:
  return hashlib.sha256(raw_value.encode("utf-8")).hexdigest()


def capture_from_event(
  raw_payload: dict[str, Any],
  *,
  product_revision: str,
  pcb_revision: str,
  usb_serial_sha256: str,
  physically_verified: bool,
) -> dict[str, Any]:
  raw_payload = _read_runtime_payload(raw_payload)
  if not physically_verified:
    raise ValueError(
      "keyboard matrix source must be explicitly confirmed as physically verified"
    )
  manifest = _build_payload(
    raw_payload,
    product_revision=product_revision,
    pcb_revision=pcb_revision,
    usb_serial_sha256=usb_serial_sha256,
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
  parser.add_argument("--usb-serial-sha256", dest="usb_serial_sha256")
  parser.add_argument("--physically-verified", dest="physically_verified")
  return parser.parse_args()


def main() -> int:
  args = parse_args()
  try:
    payload = _read_json_event(args.input, sys.stdin)
    product_revision, pcb_revision = _read_product_and_pcb_revision(
        args.product_revision, args.pcb_revision
    )
    usb_serial_sha256 = _read_usb_serial_sha256(args.usb_serial_sha256)
    if args.physically_verified is not None:
      physically_verified = _coerce_physical_verification(args.physically_verified)
    else:
      physically_verified = _ask_physical_verification()
    manifest = capture_from_event(
      payload,
      product_revision=product_revision,
      pcb_revision=pcb_revision,
      usb_serial_sha256=usb_serial_sha256,
      physically_verified=physically_verified,
    )
  except ValueError as exc:
    print(f"error: {exc}", file=sys.stderr)
    return 2
  except RuntimeError as exc:
    print(f"error: {exc}", file=sys.stderr)
    return 3

  args.output.parent.mkdir(parents=True, exist_ok=True)
  args.output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
  print(f"wrote {args.output}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
