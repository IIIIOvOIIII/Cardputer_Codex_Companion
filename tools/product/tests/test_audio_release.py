import plistlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
VERSION = "1.1.0"


def test_release_version_is_consistent():
    firmware_cmake = (ROOT / "firmware/CMakeLists.txt").read_text()
    product_types = (
        ROOT / "firmware/main/product/product_types.hpp"
    ).read_text()
    companion_main = (
        ROOT
        / "companion/Sources/cardputer-companion/CardputerCompanionMain.swift"
    ).read_text()
    companion_info = plistlib.loads(
        (ROOT / "companion/AppBundle/Info.plist").read_bytes()
    )
    driver_info = plistlib.loads(
        (ROOT / "companion/AudioDriver/Info.plist").read_bytes()
    )
    assert f'set(PROJECT_VER "{VERSION}")' in firmware_cmake
    assert f'kProductVersion = "{VERSION}"' in product_types
    assert f"cardputer-companion {VERSION}" in companion_main
    assert companion_info["CFBundleShortVersionString"] == VERSION
    assert companion_info["CFBundleVersion"] == VERSION
    assert driver_info["CFBundleShortVersionString"] == VERSION
    assert driver_info["CFBundleVersion"] == VERSION


def test_release_gate_covers_audio_components_and_content_exclusion():
    release = (ROOT / "scripts/verify_product_release.sh").read_text()
    for marker in (
        "test_audio_vectors.py",
        "product-audio-tests",
        "product-gatt-tests",
        "product-configuration-tests",
        "test_audio_ring.sh",
        "build_audio_driver.sh --test",
        "test_audio_driver_bundle.py",
        "test_audio_driver_installer.py",
        "CardputerCodexMicrophone.driver",
        "CardputerAudioBridge",
        "com.lynx.cardputer-audio-bridge.plist",
        "install_audio_driver.sh",
    ):
        assert marker in release
    assert "audio content artifact" in release


def test_runtime_recovery_wiring_stops_remote_sink_before_reconnect():
    main = (
        ROOT
        / "companion/Sources/cardputer-companion/CardputerCompanionMain.swift"
    ).read_text()
    gatt = (
        ROOT
        / "companion/Sources/ProductGATT/ProductGATTConnection.swift"
    ).read_text()
    assert "AudioBridgeCoordinator" in main
    assert "reconnectIfNeeded" in main
    assert "suspendAudioSink" in main
    assert "resumeAudioSink" in main
    assert "beginAudioSuspension" in gatt
    assert "resumeAudio" in gatt


def test_readme_documents_local_driver_and_g0_privacy_boundary():
    readme = (ROOT / "README.md").read_text()
    assert "install-audio-driver" in readme
    assert "uninstall-audio-driver" in readme
    assert "doctor audio" in readme
    assert "Cardputer Codex Microphone" in readme
    assert "短按 G0" in readme
    assert "不会自动恢复录音" in readme
