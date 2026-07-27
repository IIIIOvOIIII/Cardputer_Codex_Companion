#!/usr/bin/env python3
import argparse
import os
import plistlib
import subprocess
from pathlib import Path


LABEL = "com.lynx.cardputer-companion"
DEFAULT_BINARY = (
    Path.home()
    / "Applications/CardputerCompanion.app/Contents/MacOS/cardputer-companion"
)
DEFAULT_CONFIG = (
    Path.home()
    / "Library/Application Support/CardputerCodexCompanion/config.json"
)
DEFAULT_LOG_DIR = Path.home() / "Library/Logs/CardputerCodexCompanion"
DEFAULT_PLIST = Path.home() / "Library/LaunchAgents" / f"{LABEL}.plist"
DEFAULT_AGENT_PATH = (
    "/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:"
    "/System/Cryptexes/App/usr/bin:/usr/bin:/bin:/usr/sbin:/sbin"
)


def plist_payload(binary: Path, config: Path, log_dir: Path) -> dict:
    return {
        "Label": LABEL,
        "ProgramArguments": [
            str(binary),
            "run",
            "--config",
            str(config),
        ],
        "RunAtLoad": True,
        "KeepAlive": True,
        "StandardOutPath": str(log_dir / "agent.out.log"),
        "StandardErrorPath": str(log_dir / "agent.err.log"),
        "EnvironmentVariables": {
            "PATH": DEFAULT_AGENT_PATH,
        },
    }


def launchctl(*args: str, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["/bin/launchctl", *args],
        check=check,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def install_launch_agent(
    binary: Path,
    config: Path,
    plist_path: Path,
    log_dir: Path,
    *,
    load: bool,
) -> Path:
    if not binary.is_file():
        raise FileNotFoundError(f"missing binary: {binary}")
    if not config.is_file():
        raise FileNotFoundError(f"missing config: {config}")

    log_dir.mkdir(parents=True, exist_ok=True)
    plist_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = plist_path.with_name(f".{plist_path.name}.tmp")
    data = plistlib.dumps(
        plist_payload(binary, config, log_dir), sort_keys=False
    )
    descriptor = os.open(
        temporary,
        os.O_WRONLY | os.O_CREAT | os.O_TRUNC,
        0o600,
    )
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, plist_path)
        os.chmod(plist_path, 0o600)
    finally:
        temporary.unlink(missing_ok=True)

    if load:
        domain = f"gui/{os.getuid()}"
        launchctl("bootout", domain, str(plist_path), check=False)
        launchctl("bootstrap", domain, str(plist_path))
        launchctl("kickstart", "-k", f"{domain}/{LABEL}")
    return plist_path


def uninstall_launch_agent(plist_path: Path, *, unload: bool) -> None:
    if unload:
        domain = f"gui/{os.getuid()}"
        launchctl("bootout", domain, str(plist_path), check=False)
    plist_path.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--plist", type=Path, default=DEFAULT_PLIST)
    parser.add_argument("--log-dir", type=Path, default=DEFAULT_LOG_DIR)
    parser.add_argument("--load", action="store_true")
    parser.add_argument("--uninstall", action="store_true")
    args = parser.parse_args()

    binary = args.binary.expanduser().resolve()
    config = args.config.expanduser().resolve()
    plist_path = args.plist.expanduser().resolve()
    log_dir = args.log_dir.expanduser().resolve()

    if args.uninstall:
        uninstall_launch_agent(plist_path, unload=args.load)
        print(plist_path)
        return 0
    try:
        installed = install_launch_agent(
            binary, config, plist_path, log_dir, load=args.load
        )
    except FileNotFoundError as error:
        raise SystemExit(str(error)) from error
    print(installed)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
