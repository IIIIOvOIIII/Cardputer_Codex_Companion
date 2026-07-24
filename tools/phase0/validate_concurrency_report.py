"""Validation helpers for concurrency evidence identity and status sanitization."""

from __future__ import annotations


def same_run_errors(events: list[dict]) -> list[str]:
  errors: list[str] = []

  if not events:
    return ["no evidence events"]

  if any(
    item.get("kind") == "gatt_replay_result" and item.get("producer") != "macos_companion"
    for item in events
  ):
    errors.append(
      "gatt_replay_result must be produced by macos_companion"
    )

  for key in (
    "run_id",
    "boot_id",
    "app_elf_sha256",
    "firmware_image_sha256",
    "device_id_sha256",
  ):
    values: set[object] = set()
    for item in events:
      if key not in item:
        values.add(None)
      else:
        values.add(item[key])

    if any(value is None for value in values):
      errors.append(f"{key} missing in evidence")
      continue
    if len(values) != 1:
      errors.append(f"{key} differs across evidence")

  return errors


def forbidden_verdict_fields(value: object, path: str = "") -> list[str]:
  forbidden = {"status", "reported_status", "overall_status"}
  found: list[str] = []

  if isinstance(value, dict):
    for key, child in value.items():
      child_path = f"{path}.{key}" if path else key
      if key in forbidden:
        found.append(child_path)
      found.extend(forbidden_verdict_fields(child, child_path))
  elif isinstance(value, list):
    for index, child in enumerate(value):
      found.extend(forbidden_verdict_fields(child, f"{path}[{index}]"))

  return sorted(found)
