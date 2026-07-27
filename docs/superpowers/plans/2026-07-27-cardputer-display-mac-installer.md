# Cardputer Display and Mac Installer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Release Cardputer Codex Companion 1.1.2 with atomic streamed pet
frames, a frozen real-pet frame during microphone capture, truthful Mac Agent
status, and a self-contained install/status/uninstall workflow for the Mac
Companion and Core Audio driver.

**Architecture:** Firmware keeps row-by-row pet decoding but writes a complete
96x104 frame through one LCD transaction and one address window. A pure display
policy selects animated, static, or no frame work according to microphone and
UI revision state. The Mac distribution installs the signed app to a stable
per-user path, stores the PIN only in a mode-0600 config, manages a persistent
LaunchAgent, and delegates privileged HAL/AudioBridge changes to the existing
exact-target root helper.

**Tech Stack:** C++20, ESP-IDF 5.5.4, M5Unified/M5GFX, Python 3.11, pytest,
Swift 6, launchd, Core Audio HAL, Bash, esptool.

## Global Constraints

- The release version is `1.1.2` on every current firmware, Companion, app,
  HAL, and Codex client metadata surface.
- No full RGB565 pet frame buffer or dynamic frame allocation may be added.
- Microphone `STARTING`, `LIVE24`, `LIVE16`, and `STOPPING` freeze a selected
  pet frame; they must not show the blue placeholder when a valid pet exists.
- `B/W/M` remains BLE/Wi-Fi/Mac Agent and may report `M+` only after an
  authenticated LAN heartbeat.
- PIN values must not enter argv, plist, logs, process listings, Git, or release
  artifacts.
- `uninstall` preserves config/logs; `uninstall --purge` removes them.
- Configured devices are flashed app-only at `0x20000`; full images are
  recovery-only.
- No `.pkg`, notarization, System Extension, or Developer ID distribution is
  introduced.

---

## File Structure

### Firmware display and version

- `firmware/main/product/display_policy.hpp`: pure animated/static/none frame
  policy.
- `firmware/main/product/display_policy.cpp`: policy implementation.
- `firmware/test/host/test_display_policy.cpp`: microphone freeze and animation
  scheduling tests.
- `firmware/main/product/display.cpp`: one-window streamed LCD transaction.
- `firmware/main/product/product_controller.cpp`: apply the pure render policy.
- `firmware/main/CMakeLists.txt`: compile the policy in the target.
- `firmware/test/host/CMakeLists.txt`: compile its host test.
- `tools/product/tests/test_companion_packaging.py`: structural LCD transaction
  and no-frame-buffer checks.
- `firmware/CMakeLists.txt`, `firmware/main/product/product_types.hpp`,
  `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`,
  `companion/Sources/CodexAppServer/JSONRPCProcess.swift`,
  `companion/AppBundle/Info.plist`, and `companion/AudioDriver/Info.plist`:
  1.1.2 version surfaces.
- `tools/product/tests/test_audio_release.py` and
  `tools/product/tests/test_audio_driver_bundle.py`: release-version gate.

### Mac installation

- `scripts/mac_installer.py`: stable path model, protected config, application
  staging, LaunchAgent lifecycle, status, uninstall, and purge orchestration.
- `scripts/mac_installer.sh`: `/usr/bin/python3` entry point for source-tree use.
- `scripts/install_companion_launch_agent.py`: reusable stable LaunchAgent
  payload with no source worktree dependency.
- `scripts/package_mac_installer.sh`: self-contained distributable assembly.
- `tools/product/tests/test_mac_installer.py`: test-root install/status/uninstall
  behavior and secret-boundary tests.
- `tools/product/tests/test_companion_packaging.py`: LaunchAgent and package
  content contracts.
- `scripts/verify_product_release.sh`: installer tests and package gate.
- `README.md`: operator install, status, uninstall, purge, and PIN instructions.

### Release records

- `docs/2026-07-26-cardputer-ble-microphone_PROGRESS.md`: milestone evidence.
- `docs/validation/cardputer-ble-microphone-release.md`: 1.1.2 release evidence.
- `dist/1.1.2-SHA256SUMS`: generated final release checksum list.

---

### Task 1: Enforce Release Version 1.1.2

**Files:**

- Modify: `tools/product/tests/test_audio_release.py`
- Modify: `tools/product/tests/test_audio_driver_bundle.py`
- Modify: `firmware/test/host/test_product_types.cpp`
- Modify: `firmware/test/host/test_ui_model.cpp`
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify:
  `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Modify: `companion/Sources/CodexAppServer/JSONRPCProcess.swift`
- Modify: `companion/AppBundle/Info.plist`
- Modify: `companion/AudioDriver/Info.plist`

**Interfaces:**

- Consumes: current hard-coded release-version surfaces.
- Produces: one release identity, `1.1.2`, enforced by Python and host tests.

- [ ] **Step 1: Change only the expected test versions**

Set `VERSION = "1.1.2"` in `test_audio_release.py`, require 1.1.2 in the driver
bundle test, and change host assertions to:

```cpp
static_assert(kProductVersion == std::string_view{"1.1.2"});
assert(device.lines[0] == "VERSION:1.1.2");
```

Extend `test_release_version_is_consistent` to assert the Codex
`clientInfo.version` string is also `1.1.2`.

- [ ] **Step 2: Run the RED version tests**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_audio_driver_bundle.py
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
ctest --test-dir build/product-host -R 'product_types|ui_model' \
  --output-on-failure
```

Expected: failures identify the remaining 1.1.1 firmware, Companion, Codex
client, app plist, and HAL plist values.

- [ ] **Step 3: Update every live version surface**

Replace only current product version values with `1.1.2`; do not rewrite
historical docs or old plan targets.

- [ ] **Step 4: Run the GREEN version tests**

Rebuild the driver bundle from the updated source plist, then run:

```bash
scripts/build_audio_driver.sh
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_audio_driver_bundle.py
cmake --build build/product-host -j
ctest --test-dir build/product-host -R 'product_types|ui_model' \
  --output-on-failure
```

Expected: all selected Python and host tests pass.

- [ ] **Step 5: Commit the version boundary**

```bash
git add firmware/CMakeLists.txt firmware/main/product/product_types.hpp \
  firmware/test/host/test_product_types.cpp \
  firmware/test/host/test_ui_model.cpp \
  companion/AppBundle/Info.plist companion/AudioDriver/Info.plist \
  companion/Sources/cardputer-companion/CardputerCompanionMain.swift \
  companion/Sources/CodexAppServer/JSONRPCProcess.swift \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_audio_driver_bundle.py
git commit -m "chore: bump companion release to 1.1.2"
```

### Task 2: Render One Atomic Pet Frame and Freeze the Real Pet for Mic

**Files:**

- Create: `firmware/main/product/display_policy.hpp`
- Create: `firmware/main/product/display_policy.cpp`
- Create: `firmware/test/host/test_display_policy.cpp`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `tools/product/tests/test_companion_packaging.py`
- Modify: `firmware/main/product/display.cpp`
- Modify: `firmware/main/product/product_controller.cpp`

**Interfaces:**

- Produces:

```cpp
enum class PetFrameRenderMode : uint8_t {
  none,
  static_frame,
  animated_frame,
};

PetFrameRenderMode pet_frame_render_mode(
    MicrophoneState microphone_state,
    bool pet_chrome_changed,
    bool animation_due);
```

- Consumes: `MicrophoneState`, `PetStore::decode_rows`, and the existing
  `frame_index`/`next_frame_ms` controller state.

- [ ] **Step 1: Add the failing pure policy test**

The new host test must assert:

```cpp
assert(pet_frame_render_mode(
    MicrophoneState::live16, true, true) ==
    PetFrameRenderMode::static_frame);
assert(pet_frame_render_mode(
    MicrophoneState::live16, false, true) ==
    PetFrameRenderMode::none);
assert(pet_frame_render_mode(
    MicrophoneState::starting, true, false) ==
    PetFrameRenderMode::static_frame);
assert(pet_frame_render_mode(
    MicrophoneState::ready, false, true) ==
    PetFrameRenderMode::animated_frame);
assert(pet_frame_render_mode(
    MicrophoneState::ready, true, false) ==
    PetFrameRenderMode::animated_frame);
assert(pet_frame_render_mode(
    MicrophoneState::ready, false, false) ==
    PetFrameRenderMode::none);
```

Register `test_display_policy` in the host CMake file.

- [ ] **Step 2: Add failing LCD structure assertions**

Update `test_pet_renderer_streams_one_lcd_window_without_full_frame_buffer` to
require, inside `display_render_pet_frame`:

```python
assert frame_body.count("M5.Display.startWrite()") == 1
assert frame_body.count("M5.Display.endWrite()") == 1
assert frame_body.count("M5.Display.setAddrWindow(") == 1
```

Require `stream_pet_row` not to contain `startWrite`, `endWrite`, or
`setAddrWindow`. Require `display_render_page` not to call
`display_render_placeholder(effective)` on every PET chrome redraw.

- [ ] **Step 3: Run the RED display tests**

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py \
  -k 'pet_renderer'
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
ctest --test-dir build/product-host -R display_policy --output-on-failure
```

Expected: the new policy target is absent or fails to link, and the existing
per-row LCD transactions violate the structural assertions.

- [ ] **Step 4: Implement the minimal pure policy**

`display_policy.cpp` returns `static_frame` for a microphone-busy state only
when `pet_chrome_changed`; otherwise it returns `animated_frame` when the
microphone permits animation and either chrome changed or the frame deadline is
due.

- [ ] **Step 5: Convert the renderer to one display transaction**

In `display_render_pet_frame`, start the display transaction and set the full
96x104 address window before `decode_rows`. `stream_pet_row` validates monotonic
row order and calls only:

```cpp
M5.Display.writePixels(pixels.data(), kPetFrameWidth, true);
```

Always restore byte order and end the transaction after `decode_rows`.

- [ ] **Step 6: Apply static/animated modes in the controller**

Track whether PET chrome was redrawn in the current loop. Use
`pet_frame_render_mode` after leaving the UI mutex:

- for `static_frame`, render `frame_index` once and do not increment it;
- for `animated_frame`, render, increment modulo eight, and update the existing
  deadline;
- for `none`, do no LCD pet-frame work;
- if decoding fails in either rendering mode, use the existing placeholder.

- [ ] **Step 7: Run GREEN display and host tests**

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py \
  -k 'pet_renderer'
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
```

Expected: all selected Python tests and all host tests pass.

- [ ] **Step 8: Commit the display repair**

```bash
git add firmware/main/CMakeLists.txt firmware/test/host/CMakeLists.txt \
  firmware/main/product/display_policy.hpp \
  firmware/main/product/display_policy.cpp \
  firmware/test/host/test_display_policy.cpp \
  firmware/main/product/display.cpp \
  firmware/main/product/product_controller.cpp \
  tools/product/tests/test_companion_packaging.py
git commit -m "fix: render pet frames atomically"
```

### Task 3: Make LaunchAgent Paths Stable and Testable

**Files:**

- Modify: `scripts/install_companion_launch_agent.py`
- Create: `tools/product/tests/test_launch_agent_installer.py`
- Modify: `tools/product/tests/test_companion_packaging.py`

**Interfaces:**

- Produces:

```python
def plist_payload(binary: Path, config: Path, log_dir: Path) -> dict
def install_launch_agent(
    binary: Path,
    config: Path,
    plist_path: Path,
    log_dir: Path,
    *,
    load: bool,
) -> Path
def uninstall_launch_agent(plist_path: Path, *, unload: bool) -> None
```

- Consumes: stable executable and config paths selected by the top-level Mac
  installer.

- [ ] **Step 1: Add failing LaunchAgent tests**

Assert the generated plist:

- contains `RunAtLoad=True` and `KeepAlive=True`;
- points to `~/Applications/CardputerCompanion.app/...`;
- passes only `run --config CONFIG`;
- has no `WorkingDirectory`;
- contains no PIN field or value;
- writes logs to `~/Library/Logs/CardputerCodexCompanion`;
- can be installed and uninstalled under a temporary home without invoking
  real launchctl.

- [ ] **Step 2: Run the RED tests**

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_launch_agent_installer.py \
  tools/product/tests/test_companion_packaging.py \
  -k 'launch_agent'
```

Expected: the current payload still contains the repository `WorkingDirectory`
and lacks an unload path.

- [ ] **Step 3: Refactor the helper without changing the secret boundary**

Remove `WorkingDirectory`. Keep the fixed PATH, `RunAtLoad`, `KeepAlive`, and
`--config`. Add an exact-label unload operation and reusable functions; retain
the existing CLI compatibility used by development commands.

- [ ] **Step 4: Run GREEN LaunchAgent tests**

Run the Step 2 command.

Expected: all LaunchAgent tests pass.

- [ ] **Step 5: Commit the stable LaunchAgent**

```bash
git add scripts/install_companion_launch_agent.py \
  tools/product/tests/test_launch_agent_installer.py \
  tools/product/tests/test_companion_packaging.py
git commit -m "fix: install companion from stable paths"
```

### Task 4: Build the Unified Mac Install/Status/Uninstall Workflow

**Files:**

- Create: `scripts/mac_installer.py`
- Create: `scripts/mac_installer.sh`
- Create: `tools/product/tests/test_mac_installer.py`
- Modify: `scripts/install_audio_driver.sh`

**Interfaces:**

- Produces CLI:

```text
mac_installer.sh install [--config PATH] [--app PATH]
mac_installer.sh status
mac_installer.sh uninstall [--purge]
```

- Installed app:
  `~/Applications/CardputerCompanion.app`.
- Protected config:
  `~/Library/Application Support/CardputerCodexCompanion/config.json`.
- Delegates HAL mutation to the installed app's bundled
  `install_audio_driver.sh`.

- [ ] **Step 1: Add failing test-root install tests**

Use `CARDPUTER_MAC_INSTALL_TEST_ROOT` to map user and system paths into a
temporary directory. Use a synthetic signed-layout app fixture and a config
fixture:

```json
{
  "device": "https://192.168.1.192",
  "pairing": "87654321",
  "pin_revision": 0
}
```

Do not print the fixture pairing value. Assert:

- app installs at the stable Applications path;
- config is valid JSON and mode `0600`;
- plist contains no pairing value;
- install uses staging and leaves no stage directories;
- repeated install is idempotent;
- ordinary uninstall removes app/plist/audio exact targets but keeps config and
  logs;
- purge removes config and logs;
- unrelated files in Applications, LaunchAgents, HAL, PrivilegedHelperTools,
  and LaunchDaemons survive.

- [ ] **Step 2: Add failing input and status tests**

Assert:

- `--config` rejects group/world-readable files;
- URL must be HTTPS and RFC1918 or `.local`;
- pairing must be exactly eight ASCII digits;
- no `--pin` or `--pairing` CLI option exists;
- status differentiates missing app, unloaded LaunchAgent, crash-loop exit,
  missing HAL, missing bridge, missing Core Audio device, and LAN HTTP 401.

- [ ] **Step 3: Run the RED installer tests**

```bash
PYTHONPATH=. uv run pytest -q tools/product/tests/test_mac_installer.py
```

Expected: import or CLI tests fail because the unified installer does not
exist.

- [ ] **Step 4: Implement protected configuration and stable app staging**

Use `getpass.getpass()` for interactive PIN input. Validate imported config
mode before reading it. Write JSON to a mode-0600 temporary file in the target
directory, `fsync`, and replace atomically. Copy the app to a sibling stage,
validate its executable/resources, then atomically replace the exact target
while retaining a rollback backup until LaunchAgent bootstrap succeeds.

- [ ] **Step 5: Orchestrate privileged audio installation**

For real installs, invoke:

```text
/usr/bin/sudo INSTALLED_APP/Contents/Resources/install_audio_driver.sh
  install DRIVER BRIDGE LAUNCHD_PLIST
```

After successful install, restart `coreaudiod` through `sudo` and wait
boundedly for `Cardputer Codex Microphone` enumeration. For uninstall, call the
same exact helper with `uninstall`, restart Core Audio, and never use a broad
glob.

In test-root mode, pass the existing audio helper's test-root environment and
skip real `sudo`, launchctl, and Core Audio mutation while still exercising
real file operations.

- [ ] **Step 6: Install or unload the user LaunchAgent**

Use the Task 3 helper. On install, require `launchctl print` to show a running
PID after bounded retries. On uninstall, boot out the exact label before
removing the plist/app.

- [ ] **Step 7: Implement read-only status**

Report separate states for:

- app/version/signature;
- protected config;
- LaunchAgent loaded/running/PID/last exit;
- HAL bundle and version;
- AudioBridge LaunchDaemon;
- Core Audio input enumeration;
- authenticated LAN status.

For LAN status, pass the PIN to `/usr/bin/curl --config -` over stdin, never
argv, and print only HTTP/state results.

- [ ] **Step 8: Run GREEN installer tests**

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_mac_installer.py \
  tools/product/tests/test_audio_driver_installer.py \
  tools/product/tests/test_launch_agent_installer.py
```

Expected: all installer, exact-target audio, and LaunchAgent tests pass.

- [ ] **Step 9: Commit the unified installer**

```bash
git add scripts/mac_installer.py scripts/mac_installer.sh \
  scripts/install_audio_driver.sh \
  tools/product/tests/test_mac_installer.py
git commit -m "feat: add reversible Mac companion installer"
```

### Task 5: Package the Installer and Extend the Release Gate

**Files:**

- Create: `scripts/package_mac_installer.sh`
- Modify: `scripts/verify_product_release.sh`
- Modify: `tools/product/tests/test_companion_packaging.py`
- Modify: `tools/product/tests/test_audio_release.py`
- Modify: `README.md`

**Interfaces:**

- Produces:

```text
dist/CardputerCompanion-mac-installer/
  install.sh
  installer/mac_installer.py
  installer/install_companion_launch_agent.py
  CardputerCompanion.app/
```

- Consumes: signed `dist/CardputerCompanion.app` built from the same commit.

- [ ] **Step 1: Add failing package tests**

Require the distributable to contain executable `install.sh`, both Python
installer modules, the signed app, bundled HAL/bridge helper, and no config,
PIN, logs, selected pet cache, audio samples, worktree path, or source-tree
absolute path.

- [ ] **Step 2: Run the RED package tests**

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py \
  tools/product/tests/test_audio_release.py
```

Expected: package assertions fail because the distributable is absent.

- [ ] **Step 3: Build the self-contained installer**

`package_mac_installer.sh` must rebuild the app, replace the installer output
directory, copy only the declared files, normalize executable modes, and verify
the app signature. `install.sh` resolves all paths relative to itself and calls
the bundled Python module.

- [ ] **Step 4: Add the package to the full release gate**

The release gate must run installer tests, build the installer directory,
verify signatures, reject secrets/content artifacts, and run an install/status/
uninstall/`--purge` cycle under a temporary test root.

- [ ] **Step 5: Document operator commands**

README examples:

```bash
dist/CardputerCompanion-mac-installer/install.sh install
dist/CardputerCompanion-mac-installer/install.sh status
dist/CardputerCompanion-mac-installer/install.sh uninstall
dist/CardputerCompanion-mac-installer/install.sh uninstall --purge
```

Explain that `M` means authenticated Mac Agent, the PIN is entered hidden, and
the device must show the current PIN on its DEVICE page.

- [ ] **Step 6: Run GREEN package tests**

```bash
scripts/build_companion.sh
scripts/package_mac_installer.sh
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_mac_installer.py
codesign --verify --deep --strict \
  dist/CardputerCompanion-mac-installer/CardputerCompanion.app
```

Expected: tests and signature verification pass.

- [ ] **Step 7: Commit packaging and docs**

```bash
git add scripts/package_mac_installer.sh \
  scripts/verify_product_release.sh \
  tools/product/tests/test_companion_packaging.py \
  tools/product/tests/test_audio_release.py README.md
git commit -m "feat: package Mac companion installer"
```

### Task 6: Build, Deploy, and Verify 1.1.2

**Files:**

- Modify:
  `docs/2026-07-26-cardputer-ble-microphone_PROGRESS.md`
- Modify: `docs/validation/cardputer-ble-microphone-release.md`
- Generate: `dist/1.1.2-SHA256SUMS`
- Generate: firmware, full images, Mac app, installer, and HIL evidence under
  ignored `build/` and `dist/`.

**Interfaces:**

- Consumes: Tasks 1-5 at one commit lineage, attached Cardputer USB serial, and
  the current device PIN entered interactively by the user.
- Produces: flashed Cardputer 1.1.2, stable installed Mac Agent/HAL, `M+`,
  verified microphone input, and distributable paths.

- [ ] **Step 1: Run the full release gate**

```bash
scripts/verify_product_release.sh
```

Expected: Python, host, sanitizer, ESP-IDF clean build, Swift, audio ring,
HAL, signature, private packaging, installer package, and diff gates all pass.

- [ ] **Step 2: Install the current Mac distribution**

Run the packaged installer interactively so the current Cardputer URL and PIN
are written without echo:

```bash
dist/CardputerCompanion-mac-installer/install.sh install
dist/CardputerCompanion-mac-installer/install.sh status
```

Expected: app signature valid; LaunchAgent loaded/running from
`~/Applications`; HAL and AudioBridge active; Core Audio device enumerated.

- [ ] **Step 3: Flash only the application partition**

Resolve the attached serial device, stop any process holding it, and run the
repository's pinned esptool:

```bash
esptool.py --chip esp32s3 --port SERIAL write_flash \
  0x20000 firmware/build/cardputer_codex_companion.bin
esptool.py --chip esp32s3 --port SERIAL verify_flash \
  0x20000 firmware/build/cardputer_codex_companion.bin
```

Expected: independent flash verification reports a matching digest without
changing NVS, Wi-Fi, PIN, profiles, pets, or BLE bonds.

- [ ] **Step 4: Monitor boot and runtime state**

Capture serial logs through cold boot and page navigation. Verify:

- displayed version `1.1.2`;
- no reset, watchdog, stack overflow, or heap failure;
- status API version `1.1.2`;
- BLE/Wi-Fi OK;
- authenticated Companion OK and PET header `B+W+M+`;
- microphone READY with no error.

- [ ] **Step 5: Verify moving and frozen pet rendering**

Observe at least 60 seconds of the selected pet moving at the configured frame
rate. Confirm no horizontal row displacement, blank frame, color mismatch, or
blue placeholder. Start microphone through G0 or the approved serial HIL
command; confirm one real selected-pet frame remains static while MIC is live.
Stop microphone and confirm animation resumes from that frame.

- [ ] **Step 6: Verify microphone and Agent recovery**

Confirm `Cardputer Codex Microphone` is enumerated and input level changes with
an acoustic stimulus. Restart the user LaunchAgent and the AudioBridge service;
verify BLE audio reconnects and `M+` returns. Reboot login-start proof may be
deferred to the later clean-install gate, but the plist must pass `RunAtLoad`
and `KeepAlive` inspection now.

- [ ] **Step 7: Run a bounded installer lifecycle test**

Use a temporary test-root lifecycle for install/status/uninstall/purge in this
release. Do not remove the live installed driver or current config merely to
simulate the future destructive clean-install test.

- [ ] **Step 8: Generate and verify final checksums**

Hash:

- app-only firmware;
- generic full firmware;
- private full firmware;
- Companion executable;
- installer `install.sh`;
- final validation report.

Write `dist/1.1.2-SHA256SUMS` and run:

```bash
shasum -a 256 -c dist/1.1.2-SHA256SUMS
```

- [ ] **Step 9: Update release evidence and commit**

Record exact test counts, hashes, serial device, flash verification, installed
paths, LaunchAgent PID, status fields, pet visual result, and microphone result.
Do not record the PIN.

```bash
git add docs/2026-07-26-cardputer-ble-microphone_PROGRESS.md \
  docs/validation/cardputer-ble-microphone-release.md
git commit -m "docs: validate Cardputer companion 1.1.2"
```

- [ ] **Step 10: Final verification**

```bash
git status --short
git log -8 --oneline
dist/CardputerCompanion-mac-installer/install.sh status
shasum -a 256 -c dist/1.1.2-SHA256SUMS
```

Expected: tracked worktree clean, installed runtime healthy, and every checksum
entry `OK`. If the branch still has no remote, report that commits remain local
rather than claiming a push.
