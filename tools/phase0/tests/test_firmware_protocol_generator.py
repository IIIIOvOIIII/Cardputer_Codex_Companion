from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[3]
GENERATOR = REPO_ROOT / "tools/phase0/generate_firmware_protocol_vectors.py"
SOURCE_PATHS = [
    "pairing-v1.md",
    "gatt-auth-v1.md",
    "wss-auth-v1.md",
    "fixtures/pairing-v1.json",
    "fixtures/gatt-auth-v1.json",
    "fixtures/wss-auth-v1.json",
]


def run_generator(
    protocol_root: Path, output: Path, *, check: bool = False
) -> subprocess.CompletedProcess[str]:
    command = [
        "uv",
        "run",
        "python",
        str(GENERATOR),
        "--protocol-root",
        str(protocol_root),
        "--output",
        str(output),
    ]
    if check:
        command.append("--check")
    return subprocess.run(
        command,
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def test_generation_is_deterministic_and_checkable(tmp_path: Path) -> None:
    protocol_root = REPO_ROOT / "protocol/phase0"
    first = tmp_path / "first.hpp"
    second = tmp_path / "second.hpp"

    assert run_generator(protocol_root, first).returncode == 0
    assert run_generator(protocol_root, second).returncode == 0
    assert first.read_bytes() == second.read_bytes()
    assert run_generator(protocol_root, first, check=True).returncode == 0


@pytest.mark.parametrize("relative_source", SOURCE_PATHS)
def test_check_rejects_each_stale_canonical_source(
    tmp_path: Path, relative_source: str
) -> None:
    protocol_root = tmp_path / "phase0"
    shutil.copytree(REPO_ROOT / "protocol/phase0", protocol_root)
    output = tmp_path / "vectors.hpp"
    assert run_generator(protocol_root, output).returncode == 0
    original_output = output.read_bytes()

    source = protocol_root / relative_source
    source.write_text(source.read_text(encoding="utf-8") + "\n", encoding="utf-8")
    result = run_generator(protocol_root, output, check=True)

    assert result.returncode != 0
    assert "generated protocol header is stale" in result.stderr
    assert output.read_bytes() == original_output
