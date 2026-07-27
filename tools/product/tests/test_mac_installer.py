import importlib.util
import json
import os
import plistlib
import shutil
import stat
import subprocess
import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[3]
INSTALLER = ROOT / "scripts/mac_installer.py"
WRAPPER = ROOT / "scripts/mac_installer.sh"
PIN = "87654321"


def make_app(
    tmp_path: Path,
    *,
    name: str = "source",
    driver_payload: bytes = b"driver",
    bridge_payload: bytes = b"bridge",
) -> Path:
    app = tmp_path / name / "CardputerCompanion.app"
    executable = app / "Contents/MacOS/cardputer-companion"
    resources = app / "Contents/Resources"
    executable.parent.mkdir(parents=True)
    resources.mkdir(parents=True)
    executable.write_text(
        "#!/bin/sh\n"
        'if [ "${1:-}" = "--version" ]; then\n'
        '  echo "cardputer-companion 1.1.5"\n'
        "fi\n"
    )
    executable.chmod(0o755)
    (app / "Contents/Info.plist").write_bytes(
        plistlib.dumps(
            {
                "CFBundleIdentifier": "com.lynx.cardputer-companion",
                "CFBundleVersion": "1.1.5",
                "CFBundleShortVersionString": "1.1.5",
                "CFBundleExecutable": "cardputer-companion",
            }
        )
    )

    driver = resources / "CardputerCodexMicrophone.driver"
    driver_executable = (
        driver / "Contents/MacOS/CardputerCodexMicrophone"
    )
    driver_executable.parent.mkdir(parents=True)
    driver_executable.write_bytes(driver_payload)
    driver_executable.chmod(0o755)
    (driver / "Contents/Info.plist").write_bytes(
        plistlib.dumps(
            {
                "CFBundleIdentifier": (
                    "com.lynx.cardputer-codex-microphone.driver"
                ),
                "CFBundleVersion": "1.1.5",
                "CFBundleShortVersionString": "1.1.5",
                "CFBundleExecutable": "CardputerCodexMicrophone",
            }
        )
    )
    bridge = resources / "CardputerAudioBridge"
    bridge.write_bytes(bridge_payload)
    bridge.chmod(0o755)
    (resources / "com.lynx.cardputer-audio-bridge.plist").write_bytes(
        plistlib.dumps(
            {
                "Label": "com.lynx.cardputer-audio-bridge",
                "ProgramArguments": [
                    (
                        "/Library/PrivilegedHelperTools/"
                        "com.lynx.cardputer-audio-bridge"
                    )
                ],
                "MachServices": {
                    "com.lynx.cardputer-codex-microphone.ipc": True
                },
            }
        )
    )
    helper = resources / "install_audio_driver.sh"
    shutil.copy2(ROOT / "scripts/install_audio_driver.sh", helper)
    helper.chmod(0o755)
    return app


def make_config(
    tmp_path: Path,
    *,
    device: str = "https://192.168.1.192",
    pairing: str = PIN,
    mode: int = 0o600,
) -> Path:
    config = tmp_path / "input/config.json"
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text(
        json.dumps(
            {
                "device": device,
                "pairing": pairing,
                "pin_revision": 0,
            }
        )
    )
    config.chmod(mode)
    return config


def environment(test_root: Path) -> dict[str, str]:
    value = dict(os.environ)
    value["CARDPUTER_MAC_INSTALL_TEST_ROOT"] = str(test_root)
    value["CARDPUTER_MAC_INSTALL_SKIP_SIGNATURE_CHECK"] = "1"
    return value


def load_installer():
    sys.path.insert(0, str(ROOT / "scripts"))
    spec = importlib.util.spec_from_file_location("mac_installer", INSTALLER)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def run_installer(
    test_root: Path,
    *arguments: str,
    check: bool = True,
) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["/usr/bin/python3", str(INSTALLER), *arguments],
        cwd=ROOT,
        env=environment(test_root),
        check=check,
        capture_output=True,
        text=True,
    )


def test_installer_rejects_unsafe_or_invalid_config(tmp_path):
    app = make_app(tmp_path)
    test_root = tmp_path / "root"

    unsafe = make_config(tmp_path, mode=0o644)
    result = run_installer(
        test_root,
        "install",
        "--app",
        str(app),
        "--config",
        str(unsafe),
        check=False,
    )
    assert result.returncode != 0
    assert "0600" in result.stderr
    assert PIN not in result.stdout + result.stderr

    invalid_url = make_config(
        tmp_path, device="https://example.com", mode=0o600
    )
    result = run_installer(
        test_root,
        "install",
        "--app",
        str(app),
        "--config",
        str(invalid_url),
        check=False,
    )
    assert result.returncode != 0
    assert "LAN" in result.stderr

    invalid_pin = make_config(tmp_path, pairing="1234abcd", mode=0o600)
    result = run_installer(
        test_root,
        "install",
        "--app",
        str(app),
        "--config",
        str(invalid_pin),
        check=False,
    )
    assert result.returncode != 0
    assert "eight digits" in result.stderr


def test_install_uninstall_and_purge_are_exact_and_idempotent(tmp_path):
    app = make_app(tmp_path)
    config = make_config(tmp_path)
    test_root = tmp_path / "root"

    first = run_installer(
        test_root,
        "install",
        "--app",
        str(app),
        "--config",
        str(config),
    )
    assert PIN not in first.stdout + first.stderr
    run_installer(
        test_root,
        "install",
        "--app",
        str(app),
        "--config",
        str(config),
    )

    home = test_root / "home"
    installed_app = home / "Applications/CardputerCompanion.app"
    installed_config = (
        home
        / "Library/Application Support/CardputerCodexCompanion/config.json"
    )
    logs = home / "Library/Logs/CardputerCodexCompanion"
    agent = (
        home / "Library/LaunchAgents/com.lynx.cardputer-companion.plist"
    )
    driver = (
        test_root
        / "Library/Audio/Plug-Ins/HAL"
        / "CardputerCodexMicrophone.driver"
    )
    bridge = (
        test_root
        / "Library/PrivilegedHelperTools"
        / "com.lynx.cardputer-audio-bridge"
    )
    daemon = (
        test_root
        / "Library/LaunchDaemons"
        / "com.lynx.cardputer-audio-bridge.plist"
    )

    assert installed_app.is_dir()
    assert installed_config.is_file()
    assert stat.S_IMODE(installed_config.stat().st_mode) == 0o600
    assert logs.is_dir()
    assert agent.is_file()
    payload = plistlib.loads(agent.read_bytes())
    assert payload["ProgramArguments"][0] == str(
        installed_app / "Contents/MacOS/cardputer-companion"
    )
    assert "WorkingDirectory" not in payload
    assert PIN.encode() not in agent.read_bytes()
    assert driver.is_dir()
    assert bridge.is_file()
    assert daemon.is_file()
    assert not list(installed_app.parent.glob(".*.stage.*"))
    assert not list(installed_app.parent.glob(".*.backup.*"))

    unrelated = [
        home / "Applications/Unrelated.app/keep",
        home / "Library/LaunchAgents/com.example.keep.plist",
        test_root / "Library/Audio/Plug-Ins/HAL/Unrelated.driver/keep",
        test_root / "Library/PrivilegedHelperTools/com.example.keep",
        test_root / "Library/LaunchDaemons/com.example.keep.plist",
    ]
    for path in unrelated:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("keep")

    uninstalled = run_installer(test_root, "uninstall")
    assert PIN not in uninstalled.stdout + uninstalled.stderr
    assert not installed_app.exists()
    assert not agent.exists()
    assert not driver.exists()
    assert not bridge.exists()
    assert not daemon.exists()
    assert installed_config.is_file()
    assert logs.is_dir()
    assert all(path.is_file() for path in unrelated)
    run_installer(test_root, "uninstall")

    run_installer(
        test_root,
        "install",
        "--app",
        str(app),
        "--config",
        str(config),
    )
    run_installer(test_root, "uninstall", "--purge")
    assert not (
        home / "Library/Application Support/CardputerCodexCompanion"
    ).exists()
    assert not logs.exists()
    assert all(path.is_file() for path in unrelated)


def test_status_reports_each_installation_layer(tmp_path):
    test_root = tmp_path / "root"
    before = run_installer(test_root, "status", check=False)
    assert before.returncode != 0
    for marker in (
        "APP MISSING",
        "CONFIG MISSING",
        "AGENT UNLOADED",
        "HAL MISSING",
        "BRIDGE MISSING",
        "AUDIO MISSING",
        "LAN UNCONFIGURED",
    ):
        assert marker in before.stdout

    app = make_app(tmp_path)
    config = make_config(tmp_path)
    run_installer(
        test_root,
        "install",
        "--app",
        str(app),
        "--config",
        str(config),
    )
    after = run_installer(test_root, "status", check=False)
    assert PIN not in after.stdout + after.stderr
    for marker in (
        "APP OK",
        "CONFIG OK",
        "AGENT CONFIGURED",
        "HAL OK",
        "BRIDGE OK",
        "AUDIO TEST-SKIPPED",
        "LAN TEST-SKIPPED",
    ):
        assert marker in after.stdout


def test_public_cli_has_no_pin_argument():
    source = INSTALLER.read_text()
    wrapper = WRAPPER.read_text()
    assert '"--pin"' not in source
    assert '"--pairing"' not in source
    assert "getpass.getpass" in source
    assert "/usr/bin/python3" in wrapper


def test_launch_agent_status_uses_top_level_state_and_pid(monkeypatch, tmp_path):
    module = load_installer()
    paths = module.InstallerPaths(
        home=tmp_path / "home",
        system_root=tmp_path / "system",
        test_root=None,
        app=tmp_path / "app",
        config=tmp_path / "config",
        logs=tmp_path / "logs",
        launch_agent=tmp_path / "agent.plist",
        driver=tmp_path / "driver",
        bridge=tmp_path / "bridge",
        bridge_daemon=tmp_path / "bridge.plist",
    )
    paths.launch_agent.write_text("configured")

    class Result:
        returncode = 0
        stdout = (
            "com.lynx.cardputer-companion = {\n"
            "\tstate = running\n"
            "\tpid = 4242\n"
            "\tcoalition = {\n"
            "\t\tstate = active\n"
            "\t}\n"
            "}\n"
        )

    monkeypatch.setattr(module.subprocess, "run", lambda *args, **kwargs: Result())

    line, healthy = module.launch_agent_status(paths)

    assert line == "AGENT RUNNING pid=4242"
    assert healthy


def test_launch_agent_running_ignores_nested_running_state(monkeypatch):
    module = load_installer()

    class Result:
        returncode = 0
        stdout = (
            "com.lynx.cardputer-companion = {\n"
            "\tstate = exited\n"
            "\tcoalition = {\n"
            "\t\tstate = running\n"
            "\t\tpid = 4242\n"
            "\t}\n"
            "}\n"
        )

    monkeypatch.setattr(module.subprocess, "run", lambda *args, **kwargs: Result())

    assert not module.launch_agent_running()


def test_install_failure_restores_previous_audio_components(
    monkeypatch, tmp_path
):
    old_app = make_app(
        tmp_path,
        name="old",
        driver_payload=b"old-driver",
        bridge_payload=b"old-bridge",
    )
    new_app = make_app(
        tmp_path,
        name="new",
        driver_payload=b"new-driver",
        bridge_payload=b"new-bridge",
    )
    config = make_config(tmp_path)
    test_root = tmp_path / "root"
    run_installer(
        test_root,
        "install",
        "--app",
        str(old_app),
        "--config",
        str(config),
    )

    monkeypatch.setenv("CARDPUTER_MAC_INSTALL_TEST_ROOT", str(test_root))
    monkeypatch.setenv("CARDPUTER_MAC_INSTALL_SKIP_SIGNATURE_CHECK", "1")
    module = load_installer()
    paths = module.InstallerPaths.current()
    validated = module.read_config(config, require_private=True)
    monkeypatch.setattr(
        module,
        "install_launch_agent",
        lambda *args, **kwargs: (_ for _ in ()).throw(
            RuntimeError("bootstrap failed")
        ),
    )

    with pytest.raises(RuntimeError, match="bootstrap failed"):
        module.install(paths, new_app, validated)

    driver = (
        paths.driver / "Contents/MacOS/CardputerCodexMicrophone"
    )
    assert driver.read_bytes() == b"old-driver"
    assert paths.bridge.read_bytes() == b"old-bridge"
    assert (
        paths.app
        / "Contents/Resources/CardputerCodexMicrophone.driver"
        / "Contents/MacOS/CardputerCodexMicrophone"
    ).read_bytes() == b"old-driver"


def test_fresh_install_failure_removes_new_audio_components(
    monkeypatch, tmp_path
):
    new_app = make_app(
        tmp_path,
        name="new",
        driver_payload=b"new-driver",
        bridge_payload=b"new-bridge",
    )
    config = make_config(tmp_path)
    test_root = tmp_path / "root"

    monkeypatch.setenv("CARDPUTER_MAC_INSTALL_TEST_ROOT", str(test_root))
    monkeypatch.setenv("CARDPUTER_MAC_INSTALL_SKIP_SIGNATURE_CHECK", "1")
    module = load_installer()
    paths = module.InstallerPaths.current()
    validated = module.read_config(config, require_private=True)
    monkeypatch.setattr(
        module,
        "install_launch_agent",
        lambda *args, **kwargs: (_ for _ in ()).throw(
            RuntimeError("bootstrap failed")
        ),
    )

    with pytest.raises(RuntimeError, match="bootstrap failed"):
        module.install(paths, new_app, validated)

    assert not paths.driver.exists()
    assert not paths.bridge.exists()
    assert not paths.bridge_daemon.exists()


def test_lan_status_uses_protected_profiles_url_and_keeps_pin_off_argv(tmp_path):
    module = load_installer()
    arguments_file = tmp_path / "arguments"
    config_file = tmp_path / "curl-config"
    fake_curl = tmp_path / "curl"
    fake_curl.write_text(
        "#!/bin/sh\n"
        f'printf "%s\\n" "$@" > "{arguments_file}"\n'
        f'cat > "{config_file}"\n'
        'printf "200"\n'
    )
    fake_curl.chmod(0o755)

    line, healthy = module.authenticated_lan_status(
        {
            "device": "https://192.168.1.192",
            "pairing": PIN,
            "pin_revision": 0,
        },
        curl_path=fake_curl,
    )

    assert line == "LAN AUTHENTICATED"
    assert healthy
    arguments = arguments_file.read_text()
    assert arguments.splitlines() == ["--config", "-"]
    assert PIN not in arguments
    curl_config = config_file.read_text()
    assert 'url = "https://192.168.1.192/api/v1/profiles"' in curl_config
    assert "/api/v1/status" not in curl_config
    assert PIN not in line


def test_lan_status_reports_401_without_exposing_pin(tmp_path):
    module = load_installer()
    arguments_file = tmp_path / "arguments"
    fake_curl = tmp_path / "curl"
    fake_curl.write_text(
        "#!/bin/sh\n"
        f'printf "%s\\n" "$@" > "{arguments_file}"\n'
        "cat >/dev/null\n"
        'printf "401"\n'
    )
    fake_curl.chmod(0o755)

    line, healthy = module.authenticated_lan_status(
        {
            "device": "https://192.168.1.192",
            "pairing": PIN,
            "pin_revision": 0,
        },
        curl_path=fake_curl,
    )

    assert line == "LAN HTTP-401"
    assert not healthy
    assert PIN not in arguments_file.read_text()
    assert PIN not in line
