from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys

import pytest


ROOT = Path(__file__).resolve().parents[3]
FIXTURE_PATH = ROOT / "protocol/audio-v1/fixtures/audio-v1.json"
VALIDATOR_PATH = ROOT / "tools/product/validate_audio_vectors.py"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


audio_vectors = load_module("validate_audio_vectors", VALIDATOR_PATH)


@pytest.fixture
def audio_fixture() -> dict:
    return json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))


def test_audio_fixture_validates_without_errors(audio_fixture):
    assert audio_vectors.validate_fixture(audio_fixture) == []


def test_audio_fixture_covers_exact_packet_sizes(audio_fixture):
    packets = {item["name"]: item for item in audio_fixture["packets"]}

    assert len(bytes.fromhex(packets["24khz"]["packet_hex"])) == 132
    assert len(bytes.fromhex(packets["16khz"]["packet_hex"])) == 92


def test_audio_fixture_covers_sequence_wrap(audio_fixture):
    sequence = audio_fixture["sequence_wrap"]

    assert sequence == {"before": 65535, "after": 0}


def test_audio_fixture_rejects_malformed_length_and_version(audio_fixture):
    invalid = {item["name"]: item for item in audio_fixture["invalid_packets"]}

    assert invalid["malformed_length"]["error"] == "payload_length"
    assert audio_vectors.validate_packet(
        invalid["malformed_length"]["packet_hex"]
    ) == [
        "payload length does not match sample rate",
        "packet length does not match payload length",
    ]
    assert invalid["unsupported_version"]["error"] == "unsupported_version"
    assert audio_vectors.validate_packet(
        invalid["unsupported_version"]["packet_hex"]
    ) == ["unsupported protocol version"]


def test_audio_fixture_has_no_remote_start_opcode(audio_fixture):
    opcodes = {item["name"] for item in audio_fixture["control_opcodes"]}

    assert opcodes == {
        "hello",
        "sink_ready",
        "sink_not_ready",
        "set_preferred_rate",
        "reset_statistics",
    }
    assert audio_vectors.decode_control_opcode(0x06) is None
