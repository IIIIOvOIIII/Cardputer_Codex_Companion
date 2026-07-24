from pathlib import Path
import plistlib


ROOT = Path(__file__).resolve().parents[3]


def test_companion_bundle_metadata_and_build_script():
    info_path = ROOT / "companion/AppBundle/Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    assert info["CFBundleIdentifier"] == "com.lynx.cardputer-companion"
    assert info["NSBluetoothAlwaysUsageDescription"]
    assert info["NSLocalNetworkUsageDescription"]
    script = (ROOT / "scripts/build_companion.sh").read_text()
    assert "swift build" in script
    assert "cardputer-companion" in script
    assert "codesign" in script


def test_companion_does_not_use_clipboard_or_command_v():
    source = "\n".join(
        path.read_text()
        for path in (ROOT / "companion/Sources").rglob("*.swift")
    ).lower()
    assert "nspasteboard" not in source
    assert "command-v" not in source


def test_companion_run_supports_secret_config_file():
    configuration = (
        ROOT / "companion/Sources/cardputer-companion/Configuration.swift"
    ).read_text()
    main = (
        ROOT / "companion/Sources/cardputer-companion/CardputerCompanionMain.swift"
    ).read_text()
    assert '"--config"' in configuration
    assert "CompanionConfigFile" in configuration
    assert "cardputer-companion run --config" in main


def test_launch_agent_installation_does_not_put_pin_in_plist_or_arguments():
    script_path = ROOT / "scripts/install_companion_launch_agent.py"
    script = script_path.read_text()
    assert "com.lynx.cardputer-companion" in script
    assert "--config" in script
    assert "--pairing" not in script
    assert "pairing" not in script.lower()


def test_launch_agent_includes_codex_cli_search_path():
    script = (ROOT / "scripts/install_companion_launch_agent.py").read_text()
    assert "EnvironmentVariables" in script
    assert "/opt/homebrew/bin" in script
    assert "/usr/local/bin" in script


def test_lan_bridge_uses_curl_config_stdin_for_launchd_local_network():
    bridge = (ROOT / "companion/Sources/cardputer-companion/LANBridge.swift").read_text()
    assert '"/usr/bin/curl"' in bridge
    assert '"--config"' in bridge
    assert "LocalTLSDelegate" not in bridge
    assert "URLSession" not in bridge


def test_cardputer_display_uses_larger_body_text():
    display = (ROOT / "firmware/main/product/display.cpp").read_text()
    assert "kDisplayBodyTextSize = 2" in display
    assert "setTextSize(kDisplayBodyTextSize)" in display
