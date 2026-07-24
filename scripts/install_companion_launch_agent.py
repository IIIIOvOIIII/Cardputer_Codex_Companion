#!/usr/bin/env python3
import argparse
import os
import plistlib
import subprocess
from pathlib import Path


LABEL = "com.lynx.cardputer-companion"
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BINARY = (
    ROOT / "dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion"
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
        "WorkingDirectory": str(ROOT),
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--plist", type=Path, default=DEFAULT_PLIST)
    parser.add_argument("--log-dir", type=Path, default=DEFAULT_LOG_DIR)
    parser.add_argument("--load", action="store_true")
    args = parser.parse_args()

    binary = args.binary.expanduser().resolve()
    config = args.config.expanduser().resolve()
    plist_path = args.plist.expanduser().resolve()
    log_dir = args.log_dir.expanduser().resolve()

    if not binary.is_file():
        raise SystemExit(f"missing binary: {binary}")
    if not config.is_file():
        raise SystemExit(f"missing config: {config}")

    log_dir.mkdir(parents=True, exist_ok=True)
    plist_path.parent.mkdir(parents=True, exist_ok=True)
    plist_path.write_bytes(
        plistlib.dumps(plist_payload(binary, config, log_dir), sort_keys=False)
    )

    if args.load:
        domain = f"gui/{os.getuid()}"
        launchctl("bootout", domain, str(plist_path), check=False)
        launchctl("bootstrap", domain, str(plist_path))
        launchctl("kickstart", "-k", f"{domain}/{LABEL}")

    print(plist_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
