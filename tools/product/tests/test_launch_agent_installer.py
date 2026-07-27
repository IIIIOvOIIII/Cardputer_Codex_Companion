import importlib.util
import plistlib
import stat
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "scripts/install_companion_launch_agent.py"


def load_installer():
    spec = importlib.util.spec_from_file_location(
        "install_companion_launch_agent", SCRIPT
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_launch_agent_uses_stable_paths_without_pin_or_worktree(tmp_path):
    module = load_installer()
    home = tmp_path / "home"
    binary = (
        home
        / "Applications/CardputerCompanion.app/Contents/MacOS"
        / "cardputer-companion"
    )
    config = (
        home
        / "Library/Application Support/CardputerCodexCompanion/config.json"
    )
    logs = home / "Library/Logs/CardputerCodexCompanion"
    payload = module.plist_payload(binary, config, logs)
    serialized = plistlib.dumps(payload)

    assert payload["RunAtLoad"] is True
    assert payload["KeepAlive"] is True
    assert payload["ProgramArguments"] == [
        str(binary),
        "run",
        "--config",
        str(config),
    ]
    assert "WorkingDirectory" not in payload
    assert b"12345678" not in serialized
    assert b"pairing" not in serialized.lower()
    assert str(ROOT).encode() not in serialized


def test_launch_agent_install_and_uninstall_are_idempotent(tmp_path):
    module = load_installer()
    home = tmp_path / "home"
    binary = (
        home
        / "Applications/CardputerCompanion.app/Contents/MacOS"
        / "cardputer-companion"
    )
    binary.parent.mkdir(parents=True)
    binary.write_bytes(b"binary")
    binary.chmod(0o755)
    config = (
        home
        / "Library/Application Support/CardputerCodexCompanion/config.json"
    )
    config.parent.mkdir(parents=True)
    config.write_text(
        '{"device":"https://192.168.1.192","pairing":"12345678"}'
    )
    config.chmod(0o600)
    logs = home / "Library/Logs/CardputerCodexCompanion"
    plist_path = (
        home / "Library/LaunchAgents/com.lynx.cardputer-companion.plist"
    )

    result = module.install_launch_agent(
        binary, config, plist_path, logs, load=False
    )
    assert result == plist_path
    assert plist_path.is_file()
    assert stat.S_IMODE(plist_path.stat().st_mode) == 0o600
    assert logs.is_dir()

    module.install_launch_agent(
        binary, config, plist_path, logs, load=False
    )
    module.uninstall_launch_agent(plist_path, unload=False)
    module.uninstall_launch_agent(plist_path, unload=False)
    assert not plist_path.exists()
