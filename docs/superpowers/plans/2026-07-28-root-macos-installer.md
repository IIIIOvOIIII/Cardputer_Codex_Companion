# Root macOS Installer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide one root-level `./install.sh` for macOS Agent and microphone-driver install, status, uninstall, and purge in both source and packaged layouts.

**Architecture:** A strict Bash dispatcher detects whether it runs from a source checkout or the self-contained macOS release directory. It lazily builds a missing source application only for `install`, then delegates every operation to the existing Python installer so secret handling and system mutations keep one owner.

**Tech Stack:** Bash 3.2, Python 3.11+, pytest, Swift Package Manager, macOS launchd, Core Audio HAL, SHA-256 release manifests.

## Global Constraints

- The root shell entry point supports macOS only.
- Public operations are exactly `install`, `status`, `uninstall`, and `uninstall --purge`.
- Windows x64 continues to use the NSIS `.exe`; Windows ARM64 continues to use the portable archive.
- PIN values must not enter arguments, logs, LaunchAgent plists, Git, or release artifacts.
- `status` and `uninstall` must work without a source build.
- The existing Python installer remains the only owner of installation and removal behavior.
- Firmware and product version `1.2.1` remain unchanged.

---

## File Structure

- Create `install.sh`: public context-aware macOS dispatcher.
- Create `tools/product/tests/test_root_installer.py`: source/package dispatch,
  lazy-build, argument, packaging-source, and README contract tests.
- Modify `tools/product/tests/test_companion_packaging.py`: require the
  generated package to contain the exact root entry.
- Modify `scripts/package_mac_installer.sh`: copy the root dispatcher into the
  release package.
- Modify `README.md`: document root source installation and identical packaged
  commands.
- Modify `README.zh-CN.md`: mirror the English installation boundary in
  Chinese.
- Modify `dist/1.2.1-SHA256SUMS`: record regenerated installer-package entry
  hash.
- Modify `docs/2026-07-28-root-installer-publication_PROGRESS.md`: record test,
  package, and publication evidence.

### Task 1: Add the Root Dispatcher

**Files:**

- Create: `tools/product/tests/test_root_installer.py`
- Create: `install.sh`

**Interfaces:**

- Consumes: `scripts/build_companion.sh`,
  `scripts/mac_installer.py`, `installer/mac_installer.py`,
  `dist/CardputerCompanion.app`, and the existing
  `CARDPUTER_MAC_INSTALL_TEST_ROOT` test boundary.
- Produces: executable `install.sh` that preserves `"$@"` and exits with the
  delegated installer status.

- [ ] **Step 1: Write failing source/package dispatcher tests**

Create `tools/product/tests/test_root_installer.py` with temporary source and
package layouts. Copy the root entry into each layout, use a stub Python
installer that prints its arguments as JSON, and use a source build stub that
creates `dist/CardputerCompanion.app` and appends `build` to a trace file.

The core assertions are:

```python
ROOT = Path(__file__).resolve().parents[3]
ENTRY = ROOT / "install.sh"


def test_root_installer_exists_and_is_executable():
    assert ENTRY.is_file()
    assert os.access(ENTRY, os.X_OK)


def test_source_install_builds_only_when_bundle_is_missing(tmp_path):
    layout, trace = make_source_layout(tmp_path, app_present=False)
    result = run_entry(layout, "install")
    assert json.loads(result.stdout) == ["install"]
    assert trace.read_text().splitlines() == ["build"]

    result = run_entry(layout, "install")
    assert json.loads(result.stdout) == ["install"]
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
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q tools/product/tests/test_root_installer.py
```

Expected: failure because root `install.sh` does not exist.

- [ ] **Step 3: Add the minimal context-aware entry point**

Create executable `install.sh` with this dispatch:

```bash
#!/usr/bin/env bash
set -euo pipefail

readonly install_root="$(
  cd "$(dirname "${BASH_SOURCE[0]}")" && pwd
)"

if [[ "$(/usr/bin/uname -s)" != "Darwin" &&
      -z "${CARDPUTER_MAC_INSTALL_TEST_ROOT:-}" ]]; then
  echo "Cardputer Companion installer requires macOS" >&2
  exit 1
fi

if [[ -f "${install_root}/installer/mac_installer.py" ]]; then
  exec /usr/bin/python3 \
    "${install_root}/installer/mac_installer.py" "$@"
fi

if [[ ! -f "${install_root}/scripts/mac_installer.py" ]]; then
  echo "Cardputer Companion installer layout is invalid" >&2
  exit 1
fi

if [[ "${1:-}" == "install" &&
      ! -d "${install_root}/dist/CardputerCompanion.app" ]]; then
  "${install_root}/scripts/build_companion.sh"
fi

exec /usr/bin/python3 \
  "${install_root}/scripts/mac_installer.py" "$@"
```

Set mode `0755`.

- [ ] **Step 4: Run the focused tests and verify GREEN**

Run:

```bash
PYTHONPATH=. uv run pytest -q tools/product/tests/test_root_installer.py
```

Expected: all root-dispatch tests pass.

- [ ] **Step 5: Commit the independently working entry point**

```bash
git add install.sh tools/product/tests/test_root_installer.py
git commit -m "feat: add root macOS installer entry"
```

### Task 2: Package and Document the Root Entry

**Files:**

- Modify: `tools/product/tests/test_root_installer.py`
- Modify: `tools/product/tests/test_companion_packaging.py`
- Modify: `scripts/package_mac_installer.sh`
- Modify: `README.md`
- Modify: `README.zh-CN.md`

**Interfaces:**

- Consumes: root `install.sh` from Task 1 and the existing macOS packaging
  directory.
- Produces: packaged `install.sh` byte-identical to the public root entry and
  bilingual operator instructions that use the root command.

- [ ] **Step 1: Add failing packaging and documentation contracts**

Add:

```python
def test_package_uses_root_installer_entry():
    script = (ROOT / "scripts/package_mac_installer.sh").read_text()
    assert '"${repo_root}/install.sh"' in script
    assert '"${repo_root}/scripts/mac_installer.sh"' not in script


@pytest.mark.parametrize("readme_name", ["README.md", "README.zh-CN.md"])
def test_readme_documents_root_installer(readme_name):
    readme = (ROOT / readme_name).read_text()
    assert "./install.sh install" in readme
    assert "./install.sh status" in readme
    assert "./install.sh uninstall" in readme
    assert "./install.sh uninstall --purge" in readme
```

In
`test_mac_installer_package_is_self_contained_and_reversible`, add:

```python
assert entry.read_bytes() == (ROOT / "install.sh").read_bytes()
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q tools/product/tests/test_root_installer.py
```

Expected: packaging-source assertion fails because the package still copies
`scripts/mac_installer.sh`.

- [ ] **Step 3: Switch packaging to the root entry**

Change the installer entry copy in `scripts/package_mac_installer.sh` to:

```bash
/bin/cp "${repo_root}/install.sh" "${staged}/install.sh"
```

Keep the existing executable-mode, staging, signature, rollback, and package
layout logic unchanged.

- [ ] **Step 4: Update both README installation sections**

Document source checkout use as:

```bash
./install.sh install
./install.sh status
```

State that source `install` builds `dist/CardputerCompanion.app` only when it
is absent. State that the extracted macOS installer exposes the same commands.
Keep the existing uninstall, purge, PIN-security, Windows EXE, and Windows
ARM64 instructions.

- [ ] **Step 5: Run targeted installer and package tests**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_root_installer.py \
  tools/product/tests/test_mac_installer.py \
  tools/product/tests/test_companion_packaging.py
```

Expected: all selected tests pass and the generated packaged entry is
byte-identical to the root entry.

- [ ] **Step 6: Commit package and documentation behavior**

```bash
git add scripts/package_mac_installer.sh README.md README.zh-CN.md \
  tools/product/tests/test_root_installer.py \
  tools/product/tests/test_companion_packaging.py
git commit -m "docs: publish root installer workflow"
```

### Task 3: Regenerate, Verify, and Publish

**Files:**

- Modify: `dist/1.2.1-SHA256SUMS`
- Modify: `docs/2026-07-28-root-installer-publication_PROGRESS.md`

**Interfaces:**

- Consumes: all Task 1 and Task 2 source and tests.
- Produces: verified release checksum evidence and a pushed `origin/main`
  equal to local `main`.

- [ ] **Step 1: Run the complete release gate**

Run:

```bash
scripts/verify_product_release.sh
```

Expected: firmware, host, sanitizer, Swift, C, Go/race, installer, signing,
artifact, checksum, and credential-audit gates pass; the script regenerates
`dist/1.2.1-SHA256SUMS`.

- [ ] **Step 2: Verify root/package identity and checksum manifest**

Run:

```bash
cmp install.sh dist/CardputerCompanion-mac-installer/install.sh
shasum -a 256 -c dist/1.2.1-SHA256SUMS
git diff --check
```

Expected: `cmp` returns zero, all 11 checksum entries report `OK`, and no
whitespace error is reported.

- [ ] **Step 3: Record the release evidence**

Append a timestamped milestone to
`docs/2026-07-28-root-installer-publication_PROGRESS.md` containing the
targeted-test count, complete gate result, checksum count, audit result, and
next publication step.

- [ ] **Step 4: Commit the final evidence**

```bash
git add dist/1.2.1-SHA256SUMS \
  docs/2026-07-28-root-installer-publication_PROGRESS.md
git commit -m "test: verify root installer release"
```

- [ ] **Step 5: Push and prove remote identity**

Run:

```bash
GIT_SSH_COMMAND='ssh -i /Users/nicholasliao/.ssh/id_co_openclaw -o IdentitiesOnly=yes -o BatchMode=yes' \
  git push origin main
local_sha="$(git rev-parse HEAD)"
remote_sha="$(
  GIT_SSH_COMMAND='ssh -i /Users/nicholasliao/.ssh/id_co_openclaw -o IdentitiesOnly=yes -o BatchMode=yes' \
    git ls-remote origin refs/heads/main | awk '{print $1}'
)"
test "$local_sha" = "$remote_sha"
```

Expected: push succeeds without force and the two full commit IDs match.
