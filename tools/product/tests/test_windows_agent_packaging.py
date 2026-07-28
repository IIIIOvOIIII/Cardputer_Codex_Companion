import hashlib
import os
import stat
import struct
import subprocess
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
VERSION = "1.3.1"
WINDOWS = ROOT / "windows-agent"
INSTALLER = WINDOWS / "installer/CardputerCompanion.nsi"
TASK_XML = WINDOWS / "installer/install_task.xml.in"
REGISTER_TASK = WINDOWS / "installer/register_task.ps1"
BUILD_SCRIPT = ROOT / "scripts/build_windows_agent.sh"
PACKAGE_SCRIPT = ROOT / "scripts/package_windows_agent.sh"
BUILD = ROOT / f"build/windows/{VERSION}"
DIST = ROOT / "dist"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def pe_machine(path: Path) -> int:
    data = path.read_bytes()
    assert data[:2] == b"MZ"
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[pe_offset : pe_offset + 4] == b"PE\0\0"
    return struct.unpack_from("<H", data, pe_offset + 4)[0]


def test_installer_is_per_user_and_has_symmetric_login_task():
    source = read(INSTALLER)
    task = read(TASK_XML)
    registration = read(REGISTER_TASK)

    assert "RequestExecutionLevel user" in source
    assert "SetDateSave off" in source
    assert '$LOCALAPPDATA\\CardputerCodexCompanion' in source
    assert "Register-ScheduledTask" in registration
    assert "Unregister-ScheduledTask" in source
    assert "<LogonTrigger>" in task
    assert "<LogonType>InteractiveToken</LogonType>" in task
    assert "<RunLevel>LeastPrivilege</RunLevel>" in task
    assert "<RestartOnFailure>" in task
    assert "__AGENT_EXE__" in task
    assert 'CreateDirectory "$SMPROGRAMS\\Cardputer Codex Companion"' in source
    assert "Pair Device.lnk" in source
    assert "Status.lnk" in source
    assert "Doctor.lnk" in source

    lowered = (source + task + registration).lower()
    assert "requestexecutionlevel admin" not in lowered
    assert "cardputercodexmicrophone.driver" not in lowered
    assert "new-service" not in lowered
    assert "sc.exe create" not in lowered


def test_uninstaller_removes_task_binary_config_logs_and_start_menu():
    source = read(INSTALLER)
    uninstall = source.split("Section \"Uninstall\"", 1)[1]
    assert "Unregister-ScheduledTask" in uninstall
    assert 'RMDir /r "$LOCALAPPDATA\\CardputerCodexCompanion"' in uninstall
    assert 'RMDir /r "$SMPROGRAMS\\Cardputer Codex Companion"' in uninstall
    assert 'Delete "$INSTDIR\\cardputer-agent.exe"' in uninstall
    assert 'Delete "$INSTDIR\\install_task.xml.in"' in uninstall
    assert 'Delete "$INSTDIR\\register_task.ps1"' in uninstall


def test_cli_and_build_scripts_expose_required_public_operations():
    main = read(WINDOWS / "cmd/cardputer-agent/main.go")
    assert 'case "status":' in main
    assert 'case "doctor":' in main
    assert 'case "run":' in main
    assert "Device PIN" in main
    assert "ReadPassword" in main
    assert "pairingPIN" not in "\n".join(
        line for line in main.splitlines() if "doctor" in line.lower()
    )

    for script in [BUILD_SCRIPT, PACKAGE_SCRIPT]:
        mode = stat.S_IMODE(script.stat().st_mode)
        assert mode & stat.S_IXUSR
        assert "GOOS=windows" in read(BUILD_SCRIPT)
    assert "for architecture in amd64 arm64" in read(BUILD_SCRIPT)
    assert 'GOARCH="${architecture}"' in read(BUILD_SCRIPT)
    assert "-trimpath" in read(BUILD_SCRIPT)
    assert "SOURCE_DATE_EPOCH" in read(PACKAGE_SCRIPT)


def test_built_binaries_and_packages_have_expected_architecture_and_contents():
    amd64 = BUILD / "amd64/cardputer-agent.exe"
    arm64 = BUILD / "arm64/cardputer-agent.exe"
    archives = {
        "amd64": DIST / f"CardputerCompanion-{VERSION}-windows-amd64.zip",
        "arm64": DIST / f"CardputerCompanion-{VERSION}-windows-arm64.zip",
    }
    setup = DIST / f"CardputerCompanion-{VERSION}-windows-x64-setup.exe"

    assert pe_machine(amd64) == 0x8664
    assert pe_machine(arm64) == 0xAA64
    assert pe_machine(setup) == 0x014C
    for architecture, archive in archives.items():
        assert archive.is_file()
        with zipfile.ZipFile(archive) as package:
            names = package.namelist()
            prefix = (
                f"CardputerCompanion-{VERSION}-windows-{architecture}/"
            )
            assert names == [
                prefix + "README.txt",
                prefix + "cardputer-agent.exe",
            ]
            info = package.getinfo(prefix + "cardputer-agent.exe")
            assert info.date_time == (2020, 1, 1, 0, 0, 0)
            payload = package.read(prefix + "cardputer-agent.exe")
            assert b"192.168." not in payload
            assert b".esxi" not in payload
            assert b"dist/private" not in payload


def test_portable_archives_are_reproducible():
    archives = [
        DIST / f"CardputerCompanion-{VERSION}-windows-amd64.zip",
        DIST / f"CardputerCompanion-{VERSION}-windows-arm64.zip",
    ]
    before = {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in archives
    }
    environment = dict(os.environ)
    environment["SOURCE_DATE_EPOCH"] = "1577836800"
    subprocess.run(
        [str(PACKAGE_SCRIPT), "--archives-only"],
        cwd=ROOT,
        env=environment,
        check=True,
        capture_output=True,
        text=True,
    )
    after = {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in archives
    }
    assert before == after
