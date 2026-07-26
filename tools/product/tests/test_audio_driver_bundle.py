import plistlib
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
DRIVER = REPO_ROOT / "dist/CardputerCodexMicrophone.driver"
INFO = DRIVER / "Contents/Info.plist"
EXECUTABLE = (
    DRIVER
    / "Contents/MacOS/CardputerCodexMicrophone"
)


def test_driver_bundle_manifest_is_input_only():
    assert INFO.exists()
    info = plistlib.loads(INFO.read_bytes())
    assert info["CFBundleIdentifier"] == (
        "com.lynx.cardputer-codex-microphone.driver"
    )
    assert info["CFBundleExecutable"] == "CardputerCodexMicrophone"
    assert info["CFBundleShortVersionString"] == "1.1.0"
    assert info["CFBundleVersion"] == "1.1.0"
    assert info["AudioServerPlugIn_MachServices"] == [
        "com.lynx.cardputer-codex-microphone.ipc"
    ]
    assert "output" not in INFO.read_text(encoding="utf-8").lower()


def test_driver_binary_is_signed_and_links_required_frameworks():
    assert EXECUTABLE.is_file()
    subprocess.run(
        ["codesign", "--verify", "--strict", str(DRIVER)],
        check=True,
        capture_output=True,
        text=True,
    )
    linked = subprocess.run(
        ["otool", "-L", str(EXECUTABLE)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    for framework in ["CoreAudio", "CoreFoundation", "Security"]:
        assert f"/{framework}.framework/" in linked
    assert "/usr/lib/libSystem.B.dylib" in linked


def test_driver_bundle_marks_development_signing():
    info = plistlib.loads(INFO.read_bytes())
    assert info["CardputerAudioDevelopmentBuild"] is True
