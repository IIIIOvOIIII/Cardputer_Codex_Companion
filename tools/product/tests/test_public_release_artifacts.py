import subprocess
from pathlib import Path

from tools.product.verify_public_artifacts import ALLOWED


ROOT = Path(__file__).resolve().parents[3]
ARTIFACT_TOOL = ROOT / "tools/product/verify_public_artifacts.py"
FIRMWARE_TOOL = ROOT / "tools/product/verify_public_firmware.py"
LAYOUT = ROOT / "firmware/partitions_product.csv"


def test_artifact_allowlist_rejects_stale_without_reading_contents(tmp_path):
    (tmp_path / "1.1.8-SHA256SUMS").write_text("sensitive candidate")
    result = subprocess.run(
        ["python3", str(ARTIFACT_TOOL), "--dist", str(tmp_path)],
        capture_output=True,
        text=True,
    )
    assert result.returncode != 0
    assert "1.1.8-SHA256SUMS" in result.stderr
    assert "sensitive candidate" not in result.stdout + result.stderr


def test_artifact_allowlist_requires_exact_complete_set(tmp_path):
    for name in ALLOWED:
        path = tmp_path / name
        if "." not in name or name.endswith((".app", ".driver")):
            path.mkdir()
        else:
            path.touch()
    subprocess.run(
        [
            "python3",
            str(ARTIFACT_TOOL),
            "--dist",
            str(tmp_path),
            "--require-complete",
        ],
        check=True,
    )


def test_public_firmware_requires_erased_wifi_partition(tmp_path):
    image = tmp_path / "full.bin"
    payload = bytearray(b"\xff" * 0x18000)
    image.write_bytes(payload)
    subprocess.run(
        [
            "python3",
            str(FIRMWARE_TOOL),
            "--image",
            str(image),
            "--layout",
            str(LAYOUT),
        ],
        check=True,
    )
    payload[0x12000] = 0
    image.write_bytes(payload)
    result = subprocess.run(
        [
            "python3",
            str(FIRMWARE_TOOL),
            "--image",
            str(image),
            "--layout",
            str(LAYOUT),
        ],
    )
    assert result.returncode != 0
