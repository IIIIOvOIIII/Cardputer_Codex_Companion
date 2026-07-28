#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import sys


VERSION = "1.3.1"
ALLOWED = {
    f"{VERSION}-SHA256SUMS",
    "CardputerAudioBridge",
    "CardputerCodexMicrophone.driver",
    "CardputerCompanion.app",
    "CardputerCompanion-mac-installer",
    f"CardputerCompanion-{VERSION}-windows-amd64.zip",
    f"CardputerCompanion-{VERSION}-windows-arm64.zip",
    f"CardputerCompanion-{VERSION}-windows-x64-setup.exe",
    f"CardputerCompanion-{VERSION}-web-installer.zip",
    f"Cardputer-Codex-Companion-{VERSION}-app.bin",
    f"Cardputer-Codex-Companion-{VERSION}-factory.bin",
    f"Cardputer-Codex-Companion-{VERSION}l-launcher.bin",
    "cardputer_codex_companion-full.bin",
    "cardputer_codex_companion.bin",
    "com.lynx.cardputer-audio-bridge.plist",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dist", type=Path, required=True)
    parser.add_argument("--require-complete", action="store_true")
    arguments = parser.parse_args()
    arguments.dist.mkdir(parents=True, exist_ok=True)
    actual = {entry.name for entry in arguments.dist.iterdir()}
    unexpected = sorted(actual - ALLOWED)
    missing = sorted(ALLOWED - actual) if arguments.require_complete else []
    if unexpected or missing:
        print("public artifact allowlist verification failed", file=sys.stderr)
        for name in unexpected:
            print(f"- unexpected: {name}", file=sys.stderr)
        for name in missing:
            print(f"- missing: {name}", file=sys.stderr)
        return 2
    print(
        "public artifact allowlist verified: "
        f"{len(actual)} approved top-level entries"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
