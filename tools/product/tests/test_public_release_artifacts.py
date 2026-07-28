import re
import subprocess
from pathlib import Path

from tools.product.verify_public_artifacts import ALLOWED


ROOT = Path(__file__).resolve().parents[3]
ARTIFACT_TOOL = ROOT / "tools/product/verify_public_artifacts.py"
FIRMWARE_TOOL = ROOT / "tools/product/verify_public_firmware.py"
LAYOUT = ROOT / "firmware/partitions_product.csv"


def test_dual_firmware_artifacts_are_public() -> None:
    assert {
        "Cardputer-Codex-Companion-1.3.0-factory.bin",
        "Cardputer-Codex-Companion-1.3.0-app.bin",
        "Cardputer-Codex-Companion-1.3.0l-launcher.bin",
        "CardputerCompanion-1.3.0-web-installer.zip",
    } <= ALLOWED


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


def test_public_readme_is_english_first_and_bilingual():
    readme = (ROOT / "README.md").read_text()
    assert readme.splitlines()[0] == "[简体中文](README.zh-CN.md)"
    for section in (
        "## Overview",
        "## Features",
        "## Firmware Installation",
        "## First-Run Setup",
        "## Machine Agent",
        "## Build and Verification",
        "## Author",
        "## License",
    ):
        assert section in readme
    chinese = (ROOT / "README.zh-CN.md").read_text()
    assert chinese.splitlines()[0] == "[English](README.md)"
    for document in (readme, chinese):
        assert "1.2.3" not in document
        assert "1.3.0" in document
        assert "1.3.0l" in document
        assert "2.8.0" in document
        assert "Cardputer-Codex-Companion-1.3.0-factory.bin" in document
        assert "Cardputer-Codex-Companion-1.3.0l-launcher.bin" in document
    assert (
        "Created and maintained by **Lynx** "
        "([hi@iam.lc](mailto:hi@iam.lc))."
    ) in readme
    assert "**Lynx**（[hi@iam.lc](mailto:hi@iam.lc)）" in chinese


def test_public_license_is_apache_2():
    license_text = (ROOT / "LICENSE").read_text()
    assert "Apache License" in license_text
    assert "Version 2.0, January 2004" in license_text
    assert "http://www.apache.org/licenses/" in license_text


def test_root_readme_relative_links_resolve():
    for document in (ROOT / "README.md", ROOT / "README.zh-CN.md"):
        for target in re.findall(r"\[[^]]+\]\(([^)]+)\)", document.read_text()):
            if re.match(
                r"^[a-z][a-z0-9+.-]*:", target
            ) or target.startswith("#"):
                continue
            relative = target.split("#", 1)[0]
            if relative:
                assert (document.parent / relative).exists(), (
                    f"{document.name}: broken link {target}"
                )
