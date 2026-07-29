#!/usr/bin/env python3
from __future__ import annotations

import argparse
import getpass
import ipaddress
import json
import os
import plistlib
import shutil
import stat
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Optional
from urllib.parse import urlparse

from install_companion_launch_agent import (
    LABEL,
    install_launch_agent,
    uninstall_launch_agent,
)


APP_NAME = "CardputerCompanion.app"
APP_ID = "com.lynx.cardputer-companion"
DRIVER_NAME = "CardputerCodexMicrophone.driver"
BRIDGE_NAME = "com.lynx.cardputer-audio-bridge"
CONFIG_DIRECTORY = "CardputerCodexCompanion"
EXPECTED_VERSION = "1.3.4"
SUDO_PROMPTS = {
    "install": (
        "macOS administrator password "
        "(required to install the microphone driver): "
    ),
    "uninstall": (
        "macOS administrator password "
        "(required to remove the microphone driver): "
    ),
    "restart": (
        "macOS administrator password "
        "(required to restart Core Audio): "
    ),
}
PRIVATE_NETWORKS = tuple(
    ipaddress.ip_network(value)
    for value in ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16")
)


class InstallerError(RuntimeError):
    pass


@dataclass(frozen=True)
class InstallerPaths:
    home: Path
    system_root: Path
    test_root: Optional[Path]
    app: Path
    config: Path
    logs: Path
    launch_agent: Path
    driver: Path
    bridge: Path
    bridge_daemon: Path

    @classmethod
    def current(cls) -> "InstallerPaths":
        test_value = os.environ.get("CARDPUTER_MAC_INSTALL_TEST_ROOT")
        test_root = Path(test_value).resolve() if test_value else None
        home = test_root / "home" if test_root else Path.home()
        system_root = test_root if test_root else Path("/")
        return cls(
            home=home,
            system_root=system_root,
            test_root=test_root,
            app=home / "Applications" / APP_NAME,
            config=(
                home
                / "Library/Application Support"
                / CONFIG_DIRECTORY
                / "config.json"
            ),
            logs=home / "Library/Logs" / CONFIG_DIRECTORY,
            launch_agent=(
                home / "Library/LaunchAgents" / f"{LABEL}.plist"
            ),
            driver=(
                system_root
                / "Library/Audio/Plug-Ins/HAL"
                / DRIVER_NAME
            ),
            bridge=(
                system_root
                / "Library/PrivilegedHelperTools"
                / BRIDGE_NAME
            ),
            bridge_daemon=(
                system_root
                / "Library/LaunchDaemons"
                / f"{BRIDGE_NAME}.plist"
            ),
        )


def default_source_app() -> Path:
    script_dir = Path(__file__).resolve().parent
    candidates = (
        script_dir.parent / APP_NAME,
        script_dir.parent / "dist" / APP_NAME,
        script_dir.parent.parent / APP_NAME,
    )
    for candidate in candidates:
        if candidate.is_dir():
            return candidate
    return candidates[0]


def device_url_from_ipv4(value: object) -> str:
    if not isinstance(value, str):
        raise InstallerError("device IP must be a string")
    try:
        address = ipaddress.ip_address(value.strip())
    except ValueError as error:
        raise InstallerError("device IP must be an IPv4 address") from error
    if not isinstance(address, ipaddress.IPv4Address):
        raise InstallerError("device IP must be an IPv4 address")
    if not any(address in network for network in PRIVATE_NETWORKS):
        raise InstallerError("device IP must be an RFC1918 LAN address")
    return f"https://{address}"


def validate_device_url(value: object) -> str:
    if not isinstance(value, str):
        raise InstallerError("device URL must be a string")
    parsed = urlparse(value)
    if (
        parsed.scheme.lower() != "https"
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or parsed.query
        or parsed.fragment
    ):
        raise InstallerError("device URL must be an HTTPS LAN URL")
    host = parsed.hostname.lower()
    if host.endswith(".local"):
        return value.rstrip("/")
    try:
        address = ipaddress.ip_address(host)
    except ValueError as error:
        raise InstallerError("device URL must use a LAN IP or .local") from error
    if not any(address in network for network in PRIVATE_NETWORKS):
        raise InstallerError("device URL must use an RFC1918 LAN address")
    return value.rstrip("/")


def validate_config(value: object) -> Dict[str, object]:
    if not isinstance(value, dict):
        raise InstallerError("config must be a JSON object")
    device = validate_device_url(value.get("device"))
    pairing = value.get("pairing")
    if (
        not isinstance(pairing, str)
        or len(pairing) != 8
        or not pairing.isascii()
        or not pairing.isdigit()
    ):
        raise InstallerError("device PIN must contain exactly eight digits")
    revision = value.get("pin_revision", 0)
    if not isinstance(revision, int) or isinstance(revision, bool) or revision < 0:
        raise InstallerError("pin_revision must be a non-negative integer")
    return {
        "device": device,
        "pairing": pairing,
        "pin_revision": revision,
    }


def read_config(path: Path, *, require_private: bool) -> Dict[str, object]:
    try:
        metadata = path.stat()
    except FileNotFoundError as error:
        raise InstallerError(f"config is missing: {path}") from error
    if not stat.S_ISREG(metadata.st_mode):
        raise InstallerError("config must be a regular file")
    if require_private and stat.S_IMODE(metadata.st_mode) != 0o600:
        raise InstallerError("config permissions must be 0600")
    if require_private and metadata.st_uid != os.getuid():
        raise InstallerError("config must be owned by the current user")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise InstallerError("config is not valid UTF-8 JSON") from error
    return validate_config(value)


def interactive_config() -> Dict[str, object]:
    device = device_url_from_ipv4(input("Cardputer IP: "))
    pairing = getpass.getpass("Cardputer device PIN: ")
    return validate_config(
        {
            "device": device,
            "pairing": pairing,
            "pin_revision": 0,
        }
    )


def write_config(value: Dict[str, object], target: Path) -> None:
    validated = validate_config(value)
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_value = tempfile.mkstemp(
        prefix=f".{target.name}.stage.",
        dir=target.parent,
    )
    temporary = Path(temporary_value)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(
                validated,
                output,
                sort_keys=True,
                separators=(",", ":"),
            )
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, target)
        os.chmod(target, 0o600)
    finally:
        temporary.unlink(missing_ok=True)


def signature_bypass_allowed(paths: InstallerPaths) -> bool:
    requested = (
        os.environ.get("CARDPUTER_MAC_INSTALL_SKIP_SIGNATURE_CHECK") == "1"
    )
    if requested and paths.test_root is None:
        raise InstallerError("signature bypass is test-only")
    return requested


def app_resources(app: Path) -> Dict[str, Path]:
    resources = app / "Contents/Resources"
    return {
        "executable": app / "Contents/MacOS/cardputer-companion",
        "info": app / "Contents/Info.plist",
        "helper": resources / "install_audio_driver.sh",
        "driver": resources / DRIVER_NAME,
        "bridge": resources / "CardputerAudioBridge",
        "daemon": resources / f"{BRIDGE_NAME}.plist",
    }


def validate_app(app: Path, paths: InstallerPaths) -> None:
    resources = app_resources(app)
    if not app.is_dir() or app.name != APP_NAME:
        raise InstallerError(f"invalid application bundle: {app}")
    for name, path in resources.items():
        if name in ("driver",):
            present = path.is_dir()
        else:
            present = path.is_file()
        if not present:
            raise InstallerError(f"application resource is missing: {name}")
    if not os.access(resources["executable"], os.X_OK):
        raise InstallerError("Companion executable is not executable")
    if not os.access(resources["helper"], os.X_OK):
        raise InstallerError("audio installer is not executable")
    try:
        info = plistlib.loads(resources["info"].read_bytes())
    except (OSError, plistlib.InvalidFileException) as error:
        raise InstallerError("application Info.plist is invalid") from error
    if info.get("CFBundleIdentifier") != APP_ID:
        raise InstallerError("application bundle identifier is invalid")
    if info.get("CFBundleShortVersionString") != EXPECTED_VERSION:
        raise InstallerError(
            f"application version is not {EXPECTED_VERSION}"
        )
    if not signature_bypass_allowed(paths):
        result = subprocess.run(
            [
                "/usr/bin/codesign",
                "--verify",
                "--deep",
                "--strict",
                str(app),
            ],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise InstallerError("application signature verification failed")


def replace_app(source: Path, target: Path) -> Optional[Path]:
    target.parent.mkdir(parents=True, exist_ok=True)
    stage_root = Path(
        tempfile.mkdtemp(
            prefix=f".{APP_NAME}.stage.",
            dir=target.parent,
        )
    )
    staged_app = stage_root / APP_NAME
    backup = target.parent / f".{APP_NAME}.backup.{os.getpid()}"
    try:
        shutil.copytree(source, staged_app, symlinks=True)
        if backup.exists() or backup.is_symlink():
            remove_exact(backup)
        if target.exists() or target.is_symlink():
            os.replace(target, backup)
        os.replace(staged_app, target)
    except Exception:
        if not (target.exists() or target.is_symlink()) and (
            backup.exists() or backup.is_symlink()
        ):
            os.replace(backup, target)
        raise
    finally:
        shutil.rmtree(stage_root, ignore_errors=True)
    return backup if backup.exists() or backup.is_symlink() else None


def remove_exact(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink(missing_ok=True)
    elif path.is_dir():
        shutil.rmtree(path)


def audio_environment(paths: InstallerPaths) -> Dict[str, str]:
    environment = dict(os.environ)
    if paths.test_root is not None:
        environment["CARDPUTER_AUDIO_INSTALL_TEST_ROOT"] = str(
            paths.test_root
        )
        environment["CARDPUTER_AUDIO_SKIP_SIGNATURE_CHECK"] = "1"
    return environment


def run_audio_helper(
    app: Path,
    operation: str,
    paths: InstallerPaths,
) -> None:
    resources = app_resources(app)
    helper = resources["helper"]
    if not helper.is_file():
        raise InstallerError("audio installer is unavailable")
    arguments = [str(helper), operation]
    if operation == "install":
        arguments.extend(
            [
                str(resources["driver"]),
                str(resources["bridge"]),
                str(resources["daemon"]),
            ]
        )
    if paths.test_root is None:
        arguments[0:0] = [
            "/usr/bin/sudo",
            "-p",
            SUDO_PROMPTS[operation],
        ]
    result = subprocess.run(
        arguments,
        env=audio_environment(paths),
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise InstallerError(
            f"audio {operation} failed with status {result.returncode}"
        )


def core_audio_state(app: Path) -> Optional[bool]:
    executable = app_resources(app)["executable"]
    if not executable.is_file() or not os.access(executable, os.X_OK):
        return None
    try:
        result = subprocess.run(
            [str(executable), "audio-device-status"],
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    output = result.stdout.strip()
    if result.returncode == 0 and output == "PRESENT":
        return True
    if result.returncode == 1 and output == "ABSENT":
        return False
    return None


def core_audio_pid() -> Optional[int]:
    result = subprocess.run(
        ["/usr/bin/pgrep", "-x", "coreaudiod"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return None
    for line in result.stdout.splitlines():
        try:
            return int(line.strip())
        except ValueError:
            continue
    return None


def restart_core_audio(
    paths: InstallerPaths,
    *,
    probe_app: Path,
    expect_present: bool,
) -> None:
    if paths.test_root is not None:
        return
    previous_pid = core_audio_pid()
    if previous_pid is not None:
        result = subprocess.run(
            [
                "/usr/bin/sudo",
                "-p",
                SUDO_PROMPTS["restart"],
                "/usr/bin/killall",
                "coreaudiod",
            ],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise InstallerError(
                "failed to restart Core Audio "
                f"(status {result.returncode})"
            )
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        current_pid = core_audio_pid()
        if current_pid is None:
            core_audio_state(probe_app)
            time.sleep(0.25)
            continue
        if previous_pid is not None and current_pid == previous_pid:
            time.sleep(0.25)
            continue
        if core_audio_state(probe_app) is expect_present:
            return
        time.sleep(0.5)
    expected = "enumerate" if expect_present else "unload"
    raise InstallerError(f"Core Audio did not {expected} the microphone")


def launch_agent_running() -> bool:
    result = subprocess.run(
        ["/bin/launchctl", "print", f"gui/{os.getuid()}/{LABEL}"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return False
    state = None
    pid = None
    for raw_line in result.stdout.splitlines():
        if not raw_line.startswith("\t") or raw_line.startswith("\t\t"):
            continue
        line = raw_line.strip()
        if line.startswith("state ="):
            state = line.split("=", 1)[1].strip()
        elif line.startswith("pid ="):
            pid = line.split("=", 1)[1].strip()
    return state == "running" and pid is not None


def wait_for_launch_agent() -> None:
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        if launch_agent_running():
            return
        time.sleep(0.5)
    raise InstallerError("LaunchAgent did not reach running state")


def install(
    paths: InstallerPaths,
    source_app: Path,
    config: Dict[str, object],
) -> None:
    validate_app(source_app, paths)
    validated = validate_config(config)
    audio_was_installed = (
        paths.driver.exists()
        or paths.bridge.exists()
        or paths.bridge_daemon.exists()
    )
    old_config = paths.config.read_bytes() if paths.config.is_file() else None
    old_config_mode = (
        stat.S_IMODE(paths.config.stat().st_mode)
        if paths.config.is_file()
        else 0o600
    )
    backup = replace_app(source_app, paths.app)
    audio_installed = False
    try:
        validate_app(paths.app, paths)
        write_config(validated, paths.config)
        paths.logs.mkdir(parents=True, exist_ok=True)
        run_audio_helper(paths.app, "install", paths)
        audio_installed = True
        restart_core_audio(
            paths,
            probe_app=paths.app,
            expect_present=True,
        )
        executable = app_resources(paths.app)["executable"]
        install_launch_agent(
            executable,
            paths.config,
            paths.launch_agent,
            paths.logs,
            load=paths.test_root is None,
        )
        if paths.test_root is None:
            wait_for_launch_agent()
    except Exception:
        if audio_installed:
            try:
                if (
                    audio_was_installed
                    and backup is not None
                    and backup.is_dir()
                ):
                    run_audio_helper(backup, "install", paths)
                    restart_core_audio(
                        paths,
                        probe_app=paths.app,
                        expect_present=True,
                    )
                else:
                    run_audio_helper(paths.app, "uninstall", paths)
                    restart_core_audio(
                        paths,
                        probe_app=paths.app,
                        expect_present=False,
                    )
            except Exception:
                print(
                    "mac installer: audio rollback failed",
                    file=sys.stderr,
                )
        if old_config is None:
            paths.config.unlink(missing_ok=True)
        else:
            paths.config.parent.mkdir(parents=True, exist_ok=True)
            paths.config.write_bytes(old_config)
            os.chmod(paths.config, old_config_mode)
        remove_exact(paths.app)
        if backup is not None and backup.exists():
            os.replace(backup, paths.app)
        raise
    if backup is not None:
        remove_exact(backup)
    print(f"Installed Cardputer Companion {EXPECTED_VERSION}.")


def locate_uninstall_app(paths: InstallerPaths) -> Optional[Path]:
    if paths.app.is_dir():
        return paths.app
    candidate = default_source_app()
    return candidate if candidate.is_dir() else None


def bundle_version(app: Path) -> Optional[str]:
    info = app_resources(app)["info"]
    try:
        value = plistlib.loads(info.read_bytes())
    except (OSError, plistlib.InvalidFileException):
        return None
    version = value.get("CFBundleShortVersionString")
    return version if isinstance(version, str) else None


def locate_audio_probe_app(
    paths: InstallerPaths,
    fallback: Optional[Path],
) -> Optional[Path]:
    candidates = (default_source_app(), paths.app, fallback)
    for candidate in candidates:
        if (
            candidate is not None
            and candidate.is_dir()
            and bundle_version(candidate) == EXPECTED_VERSION
        ):
            return candidate
    return fallback


def uninstall(paths: InstallerPaths, *, purge: bool) -> None:
    uninstall_launch_agent(
        paths.launch_agent,
        unload=paths.test_root is None,
    )
    audio_installed = (
        paths.driver.exists()
        or paths.bridge.exists()
        or paths.bridge_daemon.exists()
    )
    source_app = locate_uninstall_app(paths)
    probe_app = locate_audio_probe_app(paths, source_app)
    stale_audio_state = (
        core_audio_state(probe_app)
        if paths.test_root is None and probe_app is not None
        else False
    )
    if audio_installed:
        if source_app is None:
            raise InstallerError(
                "audio components remain but no exact uninstaller is available"
            )
        run_audio_helper(source_app, "uninstall", paths)
        restart_core_audio(
            paths,
            probe_app=probe_app or source_app,
            expect_present=False,
        )
    elif stale_audio_state is True and probe_app is not None:
        restart_core_audio(
            paths,
            probe_app=probe_app,
            expect_present=False,
        )
    remove_exact(paths.app)
    if purge:
        remove_exact(paths.config.parent)
        remove_exact(paths.logs)
    print(
        "Uninstalled Cardputer Companion"
        + (" and purged configuration/logs." if purge else ".")
    )


def launch_agent_status(paths: InstallerPaths) -> tuple[str, bool]:
    if not paths.launch_agent.is_file():
        return "AGENT UNLOADED", False
    if paths.test_root is not None:
        return "AGENT CONFIGURED", True
    result = subprocess.run(
        ["/bin/launchctl", "print", f"gui/{os.getuid()}/{LABEL}"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return "AGENT UNLOADED", False
    state = "UNKNOWN"
    pid = "-"
    last_exit = None
    for raw_line in result.stdout.splitlines():
        if not raw_line.startswith("\t") or raw_line.startswith("\t\t"):
            continue
        line = raw_line.strip()
        if line.startswith("state ="):
            state = line.split("=", 1)[1].strip()
        elif line.startswith("pid ="):
            pid = line.split("=", 1)[1].strip()
        elif line.startswith("last exit code ="):
            last_exit = line.split("=", 1)[1].strip()
    if state == "running" and pid != "-":
        return f"AGENT RUNNING pid={pid}", True
    suffix = f" last_exit={last_exit}" if last_exit is not None else ""
    return f"AGENT {state.upper()}{suffix}", False


def authenticated_lan_status(
    config: Dict[str, object],
    *,
    curl_path: Path = Path("/usr/bin/curl"),
) -> tuple[str, bool]:
    device = str(config["device"])
    pairing = str(config["pairing"])
    curl_config = "\n".join(
        (
            "silent",
            "show-error",
            "insecure",
            "max-time = 5",
            'output = "/dev/null"',
            'write-out = "%{http_code}"',
            f'header = "X-Cardputer-Pairing: {pairing}"',
            f'url = "{device}/api/v1/profiles"',
            "",
        )
    )
    result = subprocess.run(
        [str(curl_path), "--config", "-"],
        input=curl_config,
        capture_output=True,
        text=True,
    )
    code = result.stdout.strip()
    if result.returncode != 0:
        return "LAN UNREACHABLE", False
    if code != "200":
        return f"LAN HTTP-{code or 'ERROR'}", False
    return "LAN AUTHENTICATED", True


def status(paths: InstallerPaths) -> int:
    healthy = True
    if paths.app.is_dir():
        try:
            validate_app(paths.app, paths)
        except InstallerError:
            print("APP INVALID")
            healthy = False
        else:
            print("APP OK")
    else:
        print("APP MISSING")
        healthy = False

    config = None
    if paths.config.is_file():
        try:
            config = read_config(paths.config, require_private=True)
        except InstallerError:
            print("CONFIG INVALID")
            healthy = False
        else:
            print("CONFIG OK")
    else:
        print("CONFIG MISSING")
        healthy = False

    agent_line, agent_ok = launch_agent_status(paths)
    print(agent_line)
    healthy = healthy and agent_ok

    driver_ok = paths.driver.is_dir()
    print("HAL OK" if driver_ok else "HAL MISSING")
    healthy = healthy and driver_ok

    bridge_ok = paths.bridge.is_file() and paths.bridge_daemon.is_file()
    print("BRIDGE OK" if bridge_ok else "BRIDGE MISSING")
    healthy = healthy and bridge_ok

    if not driver_ok or not bridge_ok:
        print("AUDIO MISSING")
        healthy = False
    elif paths.test_root is not None:
        print("AUDIO TEST-SKIPPED")
    else:
        audio_ok = core_audio_state(paths.app) is True
        print("AUDIO OK" if audio_ok else "AUDIO MISSING")
        healthy = healthy and audio_ok

    if config is None:
        print("LAN UNCONFIGURED")
        healthy = False
    elif paths.test_root is not None:
        print("LAN TEST-SKIPPED")
    else:
        lan_line, lan_ok = authenticated_lan_status(config)
        print(lan_line)
        healthy = healthy and lan_ok
    return 0 if healthy else 1


def parse_arguments(arguments: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Install and remove Cardputer Companion for macOS"
    )
    commands = parser.add_subparsers(dest="command", required=True)
    install_parser = commands.add_parser("install")
    install_parser.add_argument("--app", type=Path)
    install_parser.add_argument("--config", type=Path)
    commands.add_parser("status")
    uninstall_parser = commands.add_parser("uninstall")
    uninstall_parser.add_argument("--purge", action="store_true")
    return parser.parse_args(arguments)


def main(arguments: Optional[list[str]] = None) -> int:
    args = parse_arguments(sys.argv[1:] if arguments is None else arguments)
    paths = InstallerPaths.current()
    try:
        if args.command == "status":
            return status(paths)
        if args.command == "uninstall":
            uninstall(paths, purge=args.purge)
            return 0
        source_app = (
            args.app.expanduser().resolve()
            if args.app is not None
            else default_source_app().resolve()
        )
        config = (
            read_config(
                args.config.expanduser().resolve(),
                require_private=True,
            )
            if args.config is not None
            else interactive_config()
        )
        install(paths, source_app, config)
        return 0
    except InstallerError as error:
        print(f"mac installer: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
