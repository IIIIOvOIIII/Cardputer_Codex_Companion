import json
import os
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
mkdir -p "${root}/dist/CardputerCompanion.app"
printf 'build\\n' >> "${root}/build.trace"
"""


def copy_entry(layout: Path) -> None:
    shutil.copy2(ENTRY, layout / "install.sh")
    (layout / "install.sh").chmod(0o755)


def make_source_layout(
    tmp_path: Path,
    *,
    app_present: bool,
) -> tuple[Path, Path]:
    layout = tmp_path / "source"
    scripts = layout / "scripts"
    scripts.mkdir(parents=True)
    copy_entry(layout)
    (scripts / "mac_installer.py").write_text(INSTALLER_STUB)
    builder = scripts / "build_companion.sh"
    builder.write_text(BUILD_STUB)
    builder.chmod(0o755)
    if app_present:
        (layout / "dist/CardputerCompanion.app").mkdir(parents=True)
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
