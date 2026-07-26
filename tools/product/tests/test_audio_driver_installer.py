import os
import plistlib
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "scripts/install_audio_driver.sh"
BUILD_SCRIPT = ROOT / "scripts/build_companion.sh"
APP_DRIVER = (
    ROOT
    / "dist/CardputerCompanion.app/Contents/Resources"
    / "CardputerCodexMicrophone.driver"
)
APP_HELPER = (
    ROOT
    / "dist/CardputerCompanion.app/Contents/Resources"
    / "install_audio_driver.sh"
)


def test_companion_bundles_driver_and_root_helper():
    source = BUILD_SCRIPT.read_text()
    assert "scripts/build_audio_driver.sh" in source
    assert "Contents/Resources" in source
    assert "CardputerCodexMicrophone.driver" in source
    assert "install_audio_driver.sh" in source


def test_installer_rejects_non_root_without_test_root():
    source = SCRIPT.read_text()
    assert "requires sudo" in source
    assert "EUID" in source
    assert "/Library/Audio/Plug-Ins/HAL" in source


def test_installer_stages_validated_bundle_and_uninstalls_exact_target(tmp_path):
    source_driver = tmp_path / "CardputerCodexMicrophone.driver"
    info = source_driver / "Contents/Info.plist"
    executable = (
        source_driver / "Contents/MacOS/CardputerCodexMicrophone"
    )
    executable.parent.mkdir(parents=True)
    executable.write_bytes(b"test")
    info.write_bytes(
        plistlib.dumps(
            {
                "CFBundleIdentifier": (
                    "com.lynx.cardputer-codex-microphone.driver"
                ),
                "CFBundleVersion": "1.0.0",
                "CFBundleShortVersionString": "1.0.0",
            }
        )
    )
    test_root = tmp_path / "root"
    environment = dict(os.environ)
    environment["CARDPUTER_AUDIO_INSTALL_TEST_ROOT"] = str(test_root)
    environment["CARDPUTER_AUDIO_SKIP_SIGNATURE_CHECK"] = "1"

    subprocess.run(
        [str(SCRIPT), "install", str(source_driver)],
        check=True,
        env=environment,
        capture_output=True,
        text=True,
    )
    target = (
        test_root
        / "Library/Audio/Plug-Ins/HAL"
        / "CardputerCodexMicrophone.driver"
    )
    assert target.is_dir()
    assert plistlib.loads(
        (target / "Contents/Info.plist").read_bytes()
    )["CFBundleIdentifier"] == (
        "com.lynx.cardputer-codex-microphone.driver"
    )
    assert not list(target.parent.glob(".CardputerCodexMicrophone.*"))

    unrelated = target.parent / "Unrelated.driver"
    unrelated.mkdir()
    subprocess.run(
        [str(SCRIPT), "uninstall"],
        check=True,
        env=environment,
        capture_output=True,
        text=True,
    )
    assert not target.exists()
    assert unrelated.is_dir()


def test_built_app_contains_only_its_bundled_driver_source():
    subprocess.run(
        [str(ROOT / "scripts/build_audio_driver.sh")],
        check=True,
        cwd=ROOT,
    )
    subprocess.run([str(BUILD_SCRIPT)], check=True, cwd=ROOT)
    assert APP_DRIVER.is_dir()
    assert APP_HELPER.is_file()
    assert os.access(APP_HELPER, os.X_OK)
    source_info = plistlib.loads(
        (
            ROOT
            / "dist/CardputerCodexMicrophone.driver/Contents/Info.plist"
        ).read_bytes()
    )
    bundled_info = plistlib.loads(
        (APP_DRIVER / "Contents/Info.plist").read_bytes()
    )
    assert bundled_info["CFBundleIdentifier"] == source_info[
        "CFBundleIdentifier"
    ]
    assert bundled_info["CFBundleVersion"] == source_info["CFBundleVersion"]
    result = subprocess.run(
        [
            str(
                ROOT
                / "dist/CardputerCompanion.app/Contents/MacOS"
                / "cardputer-companion"
            ),
            "install-audio-driver",
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 77
    assert "requires sudo" in result.stderr
