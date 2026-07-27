from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PROTOCOL = ROOT / "protocol/product-v1"


def load(name: str):
    return json.loads((PROTOCOL / "fixtures" / name).read_text(encoding="utf-8"))


def walk_values(value):
    if isinstance(value, dict):
        for child in value.values():
            yield from walk_values(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk_values(child)
    else:
        yield value


def test_status_contract_uses_stable_snapshot_keys_and_omission():
    fixture = load("status.json")
    snapshot = fixture["snapshot_with_limits"]
    absent = fixture["snapshot_without_optional_telemetry"]

    assert fixture["schema_version"] == 1
    assert fixture["method"] == "POST"
    assert fixture["path"] == "/api/v1/companion/status"
    assert fixture["auth_header"] == "X-Cardputer-Pairing"
    assert snapshot["type"] == "snapshot"
    assert snapshot["session_id"] == "thread-1"
    assert snapshot["thinking_level"] == "high"
    assert snapshot["fast"] is True
    assert len(snapshot["limits"]) <= 4
    assert set(snapshot["limits"][0]) == {"scope", "window", "used_percent"}
    for key in ("model", "thinking_level", "fast", "limits"):
        assert key not in absent
    assert None not in set(walk_values(fixture))
    assert "NA" not in set(walk_values(fixture))
    assert "N/A" not in set(walk_values(fixture))


def test_action_contract_lists_every_supported_action_and_migration():
    fixture = load("actions.json")

    assert fixture["method"] == "GET"
    assert fixture["path"] == "/api/v1/companion/action"
    assert fixture["auth_header"] == "X-Cardputer-Pairing"
    assert fixture["actions"] == [
        "none",
        "select_next",
        "select_previous",
        "new",
        "interrupt",
        "approve",
        "reject",
        "provide_input",
    ]
    assert set(fixture["response"]) == {
        "sequence",
        "action",
        "needs_snapshot",
    }
    assert set(fixture["migration_response"]) == {
        "sequence",
        "action",
        "needs_snapshot",
        "next_pairing",
        "pin_revision",
    }


def test_pet_contract_fixes_chunk_limit_and_resume_fields():
    fixture = load("pet-sync.json")

    assert fixture["schema_version"] == 1
    assert fixture["auth_header"] == "X-Cardputer-Pairing"
    assert fixture["chunk_max_bytes"] == 8192
    assert fixture["endpoints"] == {
        "status": "/api/v1/companion/pet",
        "begin": "/api/v1/companion/pet/begin",
        "chunk": "/api/v1/companion/pet/chunk",
        "commit": "/api/v1/companion/pet/commit",
    }
    assert set(fixture["begin_request"]) == {
        "pet_id",
        "format_version",
        "length",
        "sha256",
    }
    assert set(fixture["status"]["transaction"]) == {
        "active",
        "id",
        "received",
        "expected",
    }


def test_contract_is_referenced_by_all_three_peers():
    firmware = (ROOT / "firmware/main/product/product_web.hpp").read_text(
        encoding="utf-8"
    )
    swift = (
        ROOT / "companion/Sources/ProductContracts/CompanionDTO.swift"
    ).read_text(encoding="utf-8")
    windows_plan = (
        ROOT
        / "docs/superpowers/plans/"
        "2026-07-27-public-release-onboarding-windows-agent.md"
    ).read_text(encoding="utf-8")

    assert "kProductPairingHeader" in firmware
    assert "kProductPetChunkMaximumBytes" in firmware
    assert 'sessionID = "session_id"' in swift
    assert 'thinkingLevel = "thinking_level"' in swift
    assert "protocol/product-v1" in windows_plan
