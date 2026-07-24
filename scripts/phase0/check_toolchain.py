from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

EXPECTED = {
    "esp_idf.tag": "v5.5.4",
    "esp_idf.commit": "735507283d5b2f9fb363a1901172dbd9e847945d",
    "node.version": "22.14.0",
    "node.sha256": "e9404633bc02a5162c5c573b1e2490f5fb44648345d64a958b17e325729a5e42",
    "python.version": "3.11.11",
}


def validate_lock(lock: dict[str, object]) -> list[str]:
    failures: list[str] = []
    for dotted, expected in EXPECTED.items():
        section, key = dotted.split(".", 1)
        actual = lock.get(section, {})
        value = actual.get(key) if isinstance(actual, dict) else None
        if value != expected:
            failures.append(dotted)
    return failures


def _run(args: list[str], optional: bool = False) -> str | None:
    try:
        completed = subprocess.run(
            args, capture_output=True, text=True, check=True
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        return None
    return completed.stdout.strip()


def _first_line(text: str | None) -> str:
    if not text:
        return "unavailable"
    for line in text.splitlines():
        if line.strip():
            return line.strip()
    return "unavailable"


def _run_git(command: list[str], repo_root: Path, optional: bool = False) -> str | None:
    args = ["git", "-C", str(repo_root)] + command
    return _run(args, optional=optional)


def _collect_idf(lock: dict[str, object], repo_root: Path) -> dict[str, str]:
    idf_dir = repo_root / ".tools" / "esp-idf"
    if not idf_dir.exists():
        return {"state": "MISSING"}

    head = _run_git(["rev-parse", "HEAD"], idf_dir)
    expected = (
        lock.get("esp_idf", {}).get("commit")
        if isinstance(lock.get("esp_idf"), dict)
        else None
    )
    state = "MATCH" if head == expected else "MISMATCH"
    return {
        "commit": head or "unavailable",
        "state": state,
    }


def _collect_node(lock: dict[str, object], repo_root: Path) -> dict[str, str]:
    node_exe = repo_root / ".tools" / "node-v22.14.0-darwin-arm64" / "bin" / "node"
    if not node_exe.exists():
        return {"state": "MISSING"}
    version = _run([str(node_exe), "-v"])
    return {
        "version": _first_line(version).lstrip("v"),
        "state": "PRESENT",
    }


def _collect_python(repo_root: Path) -> dict[str, str]:
    python_exe = repo_root / ".tools" / "uv-python" / "bin" / "python3"
    if not python_exe.exists():
        return {"state": "MISSING"}
    version = _run([str(python_exe), "--version"])
    return {
        "version": _first_line(version).replace("Python ", ""),
        "state": "PRESENT",
    }


def _collect_prerequisite(label: str, cmd: list[str] | None, repo_root: Path) -> dict[str, str]:
    if cmd is None:
        return {"label": label, "state": "BLOCKED", "detail": "command unavailable"}

    value = _run(cmd)
    if not value:
        return {"label": label, "state": "BLOCKED", "detail": "not available"}
    return {"label": label, "state": "OK", "value": _first_line(value)}


def build_report(repo_root: Path, lock_path: Path) -> dict[str, object]:
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    failures = validate_lock(lock)

    codex_version = _run(["codex", "--version"])
    swift_version = _run(["swift", "--version"])
    macos_version = _run(["sw_vers", "-productVersion"])
    return {
        "lock": {
            "pinned": lock,
            "validation_failures": failures,
        },
        "observed": {
            "esp_idf": _collect_idf(lock, repo_root),
            "node": _collect_node(lock, repo_root),
            "python": _collect_python(repo_root),
            "swift": {"version": _first_line(swift_version)},
            "macos": {"version": _first_line(macos_version)},
            "codex": {"version": _first_line(codex_version)},
        },
        "prerequisites": {
            "codesigning_identity": _collect_prerequisite(
                "codesigning_identity",
                ["security", "find-identity", "-v", "-p", "codesigning", "-s", "-"],
                repo_root,
            ),
            "hil_application": _collect_prerequisite(
                "hil_application",
                [
                    "sh",
                    "-c",
                    "ls /Applications | awk '/Companion/ {print}'",
                ],
                repo_root,
            ),
        },
        "state": "BLOCKED" if failures else "PASS",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--json",
        type=Path,
        default=Path("build/phase0/toolchain.json"),
        help="Output JSON path.",
    )
    args = parser.parse_args()

    repo_root = Path.cwd()
    lock_path = repo_root / "toolchain.lock.json"
    report = build_report(repo_root, lock_path)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report["state"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
