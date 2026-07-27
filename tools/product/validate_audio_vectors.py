#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


PROTOCOL_VERSION = 1
HEADER_LENGTH = 8
VALID_FLAGS = 0x07
RATE_PAYLOAD_LENGTHS = {1: 232, 2: 228}
RATE_DURATIONS_MS = {1: 19, 2: 28}
CONTROL_OPCODES = {
    1: "hello",
    2: "sink_ready",
    3: "sink_not_ready",
    4: "set_preferred_rate",
    5: "reset_statistics",
}


def decode_control_opcode(value: int) -> str | None:
    return CONTROL_OPCODES.get(value)


def validate_packet(packet_hex: str) -> list[str]:
    errors: list[str] = []
    try:
        packet = bytes.fromhex(packet_hex)
    except ValueError:
        return ["packet is not valid hexadecimal"]

    if len(packet) < HEADER_LENGTH:
        return ["packet is shorter than the 8-byte header"]
    if packet[0] != PROTOCOL_VERSION:
        errors.append("unsupported protocol version")
    if packet[1] & ~VALID_FLAGS:
        errors.append("packet contains unknown flags")
    expected_payload = RATE_PAYLOAD_LENGTHS.get(packet[4])
    if expected_payload is None:
        errors.append("invalid sample-rate code")
    expected_duration = RATE_DURATIONS_MS.get(packet[4])
    if expected_duration is not None and packet[5] != expected_duration:
        errors.append("frame duration does not match sample rate")
    declared_payload = int.from_bytes(packet[6:8], "little")
    if expected_payload is not None and declared_payload != expected_payload:
        errors.append("payload length does not match sample rate")
    if len(packet) != HEADER_LENGTH + declared_payload:
        errors.append("packet length does not match payload length")
    return errors


def validate_fixture(fixture: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if fixture.get("protocol_version") != PROTOCOL_VERSION:
        errors.append("fixture protocol_version must be 1")

    packets = fixture.get("packets")
    if not isinstance(packets, list):
        return errors + ["packets must be a list"]
    expected_packet_sizes = {"24khz": 240, "16khz": 236}
    names: set[str] = set()
    for item in packets:
        if not isinstance(item, dict) or not isinstance(item.get("name"), str):
            errors.append("packet entry must have a name")
            continue
        name = item["name"]
        names.add(name)
        packet_hex = item.get("packet_hex")
        if not isinstance(packet_hex, str):
            errors.append(f"{name}: packet_hex must be a string")
            continue
        packet_errors = validate_packet(packet_hex)
        errors.extend(f"{name}: {error}" for error in packet_errors)
        try:
            actual_size = len(bytes.fromhex(packet_hex))
        except ValueError:
            continue
        if name in expected_packet_sizes and actual_size != expected_packet_sizes[name]:
            errors.append(
                f"{name}: expected {expected_packet_sizes[name]} bytes, "
                f"found {actual_size}"
            )
    if names != set(expected_packet_sizes):
        errors.append("packets must contain exactly 24khz and 16khz")

    sequence_wrap = fixture.get("sequence_wrap")
    if sequence_wrap != {"before": 65535, "after": 0}:
        errors.append("sequence_wrap must specify 65535 -> 0")

    controls = fixture.get("control_opcodes")
    if not isinstance(controls, list):
        errors.append("control_opcodes must be a list")
    else:
        actual_controls = {
            item.get("value"): item.get("name")
            for item in controls
            if isinstance(item, dict)
        }
        if actual_controls != CONTROL_OPCODES:
            errors.append("control opcodes differ from the v1 allowlist")
        if decode_control_opcode(6) is not None:
            errors.append("remote-start opcode must not exist")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "fixture",
        nargs="?",
        type=Path,
        default=Path("protocol/audio-v1/fixtures/audio-v1.json"),
    )
    args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text(encoding="utf-8"))
    errors = validate_fixture(fixture)
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
