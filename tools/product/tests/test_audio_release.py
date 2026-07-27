import plistlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
VERSION = "1.2.0"


def test_release_version_is_consistent():
    firmware_cmake = (ROOT / "firmware/CMakeLists.txt").read_text()
    product_types = (
        ROOT / "firmware/main/product/product_types.hpp"
    ).read_text()
    companion_main = (
        ROOT
        / "companion/Sources/cardputer-companion/CardputerCompanionMain.swift"
    ).read_text()
    codex_rpc = (
        ROOT / "companion/Sources/CodexAppServer/JSONRPCProcess.swift"
    ).read_text()
    companion_info = plistlib.loads(
        (ROOT / "companion/AppBundle/Info.plist").read_bytes()
    )
    driver_info = plistlib.loads(
        (ROOT / "companion/AudioDriver/Info.plist").read_bytes()
    )
    release_manifest = json.loads(
        (ROOT / "release/product-release.json").read_text()
    )
    windows_build = (
        ROOT / "scripts/build_windows_agent.sh"
    ).read_text()
    windows_installer = (
        ROOT / "windows-agent/installer/CardputerCompanion.nsi"
    ).read_text()
    assert f'set(PROJECT_VER "{VERSION}")' in firmware_cmake
    assert f'kProductVersion = "{VERSION}"' in product_types
    assert f"cardputer-companion {VERSION}" in companion_main
    assert f'"version": "{VERSION}"' in codex_rpc
    assert companion_info["CFBundleShortVersionString"] == VERSION
    assert companion_info["CFBundleVersion"] == VERSION
    assert driver_info["CFBundleShortVersionString"] == VERSION
    assert driver_info["CFBundleVersion"] == VERSION
    assert release_manifest["product"] == "Cardputer Codex Companion"
    assert release_manifest["version"] == VERSION
    assert release_manifest["protocol"] == "product-v1"
    assert release_manifest["artifacts"]["windows_amd64"].endswith(
        f"{VERSION}-windows-amd64.zip"
    )
    assert release_manifest["artifacts"]["windows_arm64"].endswith(
        f"{VERSION}-windows-arm64.zip"
    )
    assert f'WINDOWS_AGENT_VERSION:-{VERSION}' in windows_build
    assert 'VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"' in (
        windows_installer
    )


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
        "test_launch_agent_installer.py",
        "test_mac_installer.py",
        "package_mac_installer.sh",
        "CardputerCompanion-mac-installer",
        "CardputerCodexMicrophone.driver",
        "CardputerAudioBridge",
        "com.lynx.cardputer-audio-bridge.plist",
        "install_audio_driver.sh",
        "package_windows_agent.sh",
        "test_windows_agent_packaging.py",
        "audit_public_release.py",
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
    assert "CardputerCompanion-mac-installer/install.sh install" in readme
    assert "CardputerCompanion-mac-installer/install.sh status" in readme
    assert "CardputerCompanion-mac-installer/install.sh uninstall" in readme
    assert "uninstall --purge" in readme
    assert "B/W/M" in readme
    assert "隐藏输入" in readme
