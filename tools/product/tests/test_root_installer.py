import json
import os
import plistlib
import shutil
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[3]
ENTRY = ROOT / "install.sh"
INSTALLER_STUB = """\
import json
import sys

print(json.dumps(sys.argv[1:]))
"""
BUILD_STUB = """\
#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
app="${root}/dist/CardputerCompanion.app"
mkdir -p "${app}/Contents/MacOS"
/bin/cp \
  "${root}/companion/AppBundle/Info.plist" \
  "${app}/Contents/Info.plist"
printf '#!/bin/sh\\nprintf "cardputer-companion 1.3.2\\\\n"\\n' \
  > "${app}/Contents/MacOS/cardputer-companion"
chmod 0755 "${app}/Contents/MacOS/cardputer-companion"
printf 'build\\n' >> "${root}/build.trace"
"""
EXPECTED_VERSION = "1.3.2"


def copy_entry(layout: Path) -> None:
    shutil.copy2(ENTRY, layout / "install.sh")
    (layout / "install.sh").chmod(0o755)


def make_source_layout(
    tmp_path: Path,
    *,
    app_present: bool,
    app_version: str = EXPECTED_VERSION,
) -> tuple[Path, Path]:
    layout = tmp_path / "source"
    scripts = layout / "scripts"
    scripts.mkdir(parents=True)
    source_info = layout / "companion/AppBundle/Info.plist"
    source_info.parent.mkdir(parents=True)
    source_info.write_bytes(
        plistlib.dumps(
            {
                "CFBundleIdentifier": "com.lynx.cardputer-companion",
                "CFBundleShortVersionString": EXPECTED_VERSION,
            }
        )
    )
    copy_entry(layout)
    (scripts / "mac_installer.py").write_text(INSTALLER_STUB)
    builder = scripts / "build_companion.sh"
    builder.write_text(BUILD_STUB)
    builder.chmod(0o755)
    if app_present:
        app = layout / "dist/CardputerCompanion.app"
        executable = app / "Contents/MacOS/cardputer-companion"
        executable.parent.mkdir(parents=True)
        (app / "Contents/Info.plist").write_bytes(
            plistlib.dumps(
                {
                    "CFBundleIdentifier": "com.lynx.cardputer-companion",
                    "CFBundleShortVersionString": app_version,
                }
            )
        )
        executable.write_text(
            f'#!/bin/sh\nprintf "cardputer-companion {app_version}\\n"\n'
        )
        executable.chmod(0o755)
    return layout, layout / "build.trace"


def make_packaged_layout(tmp_path: Path) -> Path:
    layout = tmp_path / "package"
    installer = layout / "installer"
    installer.mkdir(parents=True)
    copy_entry(layout)
    (installer / "mac_installer.py").write_text(INSTALLER_STUB)
    (layout / "CardputerCompanion.app").mkdir()
    return layout


def run_entry(
    layout: Path,
    *arguments: str,
    check: bool = True,
) -> subprocess.CompletedProcess:
    environment = dict(os.environ)
    environment["CARDPUTER_MAC_INSTALL_TEST_ROOT"] = str(
        layout / "test-root"
    )
    return subprocess.run(
        [str(layout / "install.sh"), *arguments],
        cwd=layout,
        env=environment,
        check=check,
        capture_output=True,
        text=True,
    )


def test_root_installer_exists_and_is_executable():
    assert ENTRY.is_file()
    assert os.access(ENTRY, os.X_OK)


def test_source_install_builds_only_when_bundle_is_missing(tmp_path):
    layout, trace = make_source_layout(tmp_path, app_present=False)

    first = run_entry(layout, "install")
    assert json.loads(first.stdout) == ["install"]
    assert trace.read_text().splitlines() == ["build"]

    second = run_entry(layout, "install")
    assert json.loads(second.stdout) == ["install"]
    assert trace.read_text().splitlines() == ["build"]


def test_source_install_rebuilds_when_bundle_version_is_stale(tmp_path):
    layout, trace = make_source_layout(
        tmp_path,
        app_present=True,
        app_version="1.3.1",
    )

    result = run_entry(layout, "install")

    assert json.loads(result.stdout) == ["install"]
    assert trace.read_text().splitlines() == ["build"]
    rebuilt_info = plistlib.loads(
        (
            layout
            / "dist/CardputerCompanion.app/Contents/Info.plist"
        ).read_bytes()
    )
    assert rebuilt_info["CFBundleShortVersionString"] == EXPECTED_VERSION


def test_source_install_keeps_current_bundle_without_rebuilding(tmp_path):
    layout, trace = make_source_layout(tmp_path, app_present=True)

    result = run_entry(layout, "install")

    assert json.loads(result.stdout) == ["install"]
    assert not trace.exists()


@pytest.mark.parametrize(
    "arguments",
    [("status",), ("uninstall",), ("uninstall", "--purge")],
)
def test_source_non_install_operations_never_build(tmp_path, arguments):
    layout, trace = make_source_layout(tmp_path, app_present=False)

    result = run_entry(layout, *arguments)

    assert json.loads(result.stdout) == list(arguments)
    assert not trace.exists()


def test_packaged_layout_dispatches_without_source_build(tmp_path):
    layout = make_packaged_layout(tmp_path)

    result = run_entry(layout, "install")

    assert json.loads(result.stdout) == ["install"]
    assert not (layout / "scripts").exists()


def test_invalid_layout_fails_before_dispatch(tmp_path):
    layout = tmp_path / "invalid"
    layout.mkdir()
    copy_entry(layout)

    result = run_entry(layout, "status", check=False)

    assert result.returncode != 0
    assert "installer layout is invalid" in result.stderr
