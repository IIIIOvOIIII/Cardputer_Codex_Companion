import json
from pathlib import Path

import pytest

from tools.product.verify_firmware_memory import validate_firmware_memory

REPO_ROOT = Path(__file__).parents[3]


def test_accepts_release_with_ample_diram_headroom(tmp_path):
    report = tmp_path / "size.json"
    report.write_text(json.dumps({"diram_remain": 149_581}), encoding="utf-8")

    validate_firmware_memory(report, minimum_diram_bytes=96 * 1024)


def test_rejects_release_that_would_starve_runtime_services(tmp_path):
    report = tmp_path / "size.json"
    report.write_text(json.dumps({"diram_remain": 8_909}), encoding="utf-8")

    with pytest.raises(ValueError, match="DIRAM headroom"):
        validate_firmware_memory(report, minimum_diram_bytes=96 * 1024)


def test_product_release_enforces_target_diram_budget():
    release = (REPO_ROOT / "scripts/verify_product_release.sh").read_text(
        encoding="utf-8"
    )

    assert "-m esp_idf_size --format json" in release
    assert "tools/product/verify_firmware_memory.py" in release
