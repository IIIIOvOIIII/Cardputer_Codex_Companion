# Cardputer Codex Companion 1.2.0 Public Release Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use `executing-plans` to implement this
> plan task-by-task. Use `test-driven-development` for every behavior change
> and `verification-before-completion` before claiming release readiness.

**Goal:** Produce a credential-safe public `1.2.0` release with device-led
first-run onboarding, global backtick return behavior, a core Windows Machine
Agent, generic firmware artifacts, and a final clean removal of the currently
installed macOS Agent and audio components.

**Architecture:** Add a pure, testable onboarding state machine and NVS store
to the firmware, then bind it to existing asynchronous Wi-Fi, BLE pairing, Web,
Companion heartbeat, and display paths. Keep the Swift/macOS Agent intact.
Implement the Windows peer as a separate pure-Go module using the existing LAN
and CCPT contracts. Replace private-by-default release packaging with an
allowlisted public artifact pipeline and a redacted full-history secret gate.

**Tech Stack:** C++20 host tests, ESP-IDF 5.5.4 / ESP32-S3, M5Unified, embedded
HTML/CSS/JavaScript, Python 3.11 + pytest, Swift 6, Go 1.25, Windows DPAPI,
NSIS, CMake/CTest, Bash.

---

## Task 0: Create the isolated implementation worktree and prove the baseline

**Files:**

- Create worktree: `.worktrees/public-release-onboarding`
- Update:
  `docs/2026-07-27-public-release_PROGRESS.md`

**Step 1: Confirm worktree location safety**

Run:

```bash
git check-ignore -q .worktrees
git worktree list
git status --short --branch
```

Expected: `.worktrees` is ignored and `main` is clean.

**Step 2: Create the branch/worktree**

Run:

```bash
git worktree add .worktrees/public-release-onboarding \
  -b feat/public-release-onboarding
cd .worktrees/public-release-onboarding
```

Expected: the worktree begins at the approved design/plan commit.

**Step 3: Run the narrow baseline**

Run:

```bash
PYTHONPATH=. uv run pytest -q
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
swift test --package-path companion
```

Expected: all existing tests pass before implementation.

**Step 4: Record the baseline**

Update the progress document with test counts, current commit, and any
pre-existing limitation. Commit:

```bash
git add docs/2026-07-27-public-release_PROGRESS.md
git commit -m "docs: record public release baseline"
```

## Task 1: Turn the one-off credential review into a public release gate

**Files:**

- Create: `tools/product/audit_public_release.py`
- Create: `tools/product/tests/test_public_release_security.py`
- Create: `docs/validation/public-release-security.md`
- Modify: `scripts/verify_product_release.sh`
- Modify: `scripts/package_product_firmware.sh`
- Delete: `scripts/package_private_firmware.sh`
- Delete: `tools/product/generate_wifi_nvs.py`
- Delete: `tools/product/tests/test_private_packaging.py`
- Modify: `.gitignore`
- Modify: `README.md`

**Step 1: Write failing security-gate tests**

Test temporary Git repositories containing:

- a secret-like blob on a non-current branch;
- a secret-like blob reachable only by reflog;
- a retained unreachable secret-like blob;
- a private firmware/NVS historical path;
- safe synthetic test fixtures;
- a candidate whose value must not appear in stdout/stderr;
- a public artifact tree containing a forbidden `private/` directory;
- a clean repository and allowlisted artifact tree.

The test must also assert that the current tracked private packaging entry
points are rejected.

**Step 2: Run RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_public_release_security.py
```

Expected: fail because the audit command does not exist and current release
packaging still includes private artifacts.

**Step 3: Implement the redacted history/artifact auditor**

The tool must:

- enumerate `refs`, reflogs, and `git fsck --unreachable --no-reflogs` output;
- inspect paths and blobs through `git cat-file`, never checkout unknown data;
- identify rules by stable rule ID;
- emit only commit/object/path/rule metadata;
- never print candidate content;
- return non-zero for real findings or forbidden public artifacts;
- accept explicit repository and artifact-root arguments for tests.

Remove the tracked private packaging script/generator/tests. Keep generic image
merging only. Update README and the release script so public verification never
contacts an internal Vault and never generates Wi-Fi NVS.

**Step 4: Run GREEN**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_public_release_security.py \
  tools/product/tests/test_partition_layout.py \
  tools/product/tests/test_firmware_memory.py
python3 tools/product/audit_public_release.py \
  --repo . --artifacts dist --report \
  docs/validation/public-release-security.md
```

Expected: tests pass and the report records a clean retained object database
without secret values.

**Step 5: Commit**

```bash
git add .gitignore README.md scripts tools/product \
  docs/validation/public-release-security.md
git commit -m "security: gate public release artifacts"
```

## Task 2: Define the cross-platform product protocol contract

**Files:**

- Create: `protocol/product-v1/README.md`
- Create: `protocol/product-v1/fixtures/status.json`
- Create: `protocol/product-v1/fixtures/actions.json`
- Create: `protocol/product-v1/fixtures/pet-sync.json`
- Create: `tools/product/tests/test_product_protocol_contract.py`
- Modify: `firmware/test/host/test_product_web.cpp`
- Modify: `companion/Tests/ProductContractsTests/CompanionDTOTests.swift`

**Step 1: Write failing contract tests**

Specify exact authenticated endpoints, headers, request/response fields,
omission rules, maximum lengths, Codex action names, heartbeat semantics, and
CCPT synchronization metadata. Tests must reject `null`/`NA` limit placeholders
where fields should be omitted.

**Step 2: Run RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_product_protocol_contract.py
```

Expected: fail because the product-v1 fixtures are absent.

**Step 3: Add the versioned contract and fixtures**

Document existing behavior rather than inventing a second API. Reconcile any
firmware/Swift DTO drift exposed by the fixtures. Do not change BLE audio or
GATT protocols.

**Step 4: Run GREEN and cross-peer tests**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_product_protocol_contract.py
cmake --build build/product-host --target test_product_web -j
./build/product-host/test_product_web
swift test --package-path companion \
  --filter ProductContractsTests
```

Expected: all three peers accept the same fixtures.

**Step 5: Commit**

```bash
git add protocol/product-v1 tools/product/tests \
  firmware/test/host/test_product_web.cpp \
  companion/Tests/ProductContractsTests/CompanionDTOTests.swift
git commit -m "test: define product agent protocol"
```

## Task 3: Add the persistent onboarding domain model

**Files:**

- Create: `firmware/main/product/onboarding.hpp`
- Create: `firmware/main/product/onboarding.cpp`
- Create: `firmware/test/host/test_onboarding.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/main/CMakeLists.txt`

**Step 1: Write failing state-machine tests**

Cover:

- blank device begins at Wi-Fi scan;
- valid legacy configured device migrates directly to complete;
- each step advances only on its verified event;
- reboot resumes the last verified step;
- Agent heartbeat is required for final completion;
- transient disconnect after completion does not reopen onboarding;
- explicit rerun resets progress;
- invalid/corrupt records fail closed to onboarding without erasing unrelated
  configuration.

Use an in-memory store backend; host tests must not depend on ESP NVS.

**Step 2: Run RED**

Run:

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_onboarding -j
./build/product-host/test_onboarding
```

Expected: fail because the onboarding model is not implemented.

**Step 3: Implement the smallest state machine/store interface**

Keep persistence serialization bounded and versioned. Separate pure transition
logic from the ESP NVS adapter. Make completion writes atomic and idempotent.

**Step 4: Run GREEN**

Run:

```bash
cmake --build build/product-host --target test_onboarding -j
./build/product-host/test_onboarding
```

Expected: all onboarding domain tests pass.

**Step 5: Commit**

```bash
git add firmware/main/product/onboarding.* \
  firmware/main/CMakeLists.txt firmware/test/host
git commit -m "feat: add persistent onboarding state"
```

## Task 4: Implement on-device Wi-Fi selection and password entry

**Files:**

- Modify: `firmware/main/product/onboarding.hpp`
- Modify: `firmware/main/product/onboarding.cpp`
- Modify: `firmware/main/product/wifi_manager.hpp`
- Modify: `firmware/main/product/wifi_manager.cpp`
- Modify: `firmware/main/product/ui_model.hpp`
- Modify: `firmware/main/product/ui_model.cpp`
- Modify: `firmware/main/product/display.hpp`
- Modify: `firmware/main/product/display.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/test/host/test_onboarding.cpp`
- Modify: `firmware/test/host/test_wifi_manager.cpp`
- Modify: `firmware/test/host/test_ui_model.cpp`

**Step 1: Write failing interaction tests**

Cover asynchronous scan start/completion, sorted/paginated results, signal and
security labels, rescan, hidden SSID entry, masked password display,
backspace/cancel, open network, 8..63-byte secured password validation,
connect-before-persist, failure reason mapping, retry, and preservation of the
last known-good credentials.

Add a log/presentation assertion proving the password is never included in
model content or diagnostic text.

**Step 2: Run RED**

Run:

```bash
cmake --build build/product-host --target \
  test_onboarding test_wifi_manager test_ui_model -j
ctest --test-dir build/product-host --output-on-failure \
  -R 'onboarding|wifi_manager|ui_model'
```

Expected: new Wi-Fi onboarding assertions fail.

**Step 3: Bind onboarding to existing Wi-Fi primitives**

Reuse `product_wifi_scan()` and `product_wifi_save()` rather than introducing a
second Wi-Fi stack. Replace the no-credentials automatic provisioning-AP path
with the onboarding scan path; retain any recovery AP only behind an explicit
physical recovery action.

Render the onboarding page without blocking the UI task or allocating an
unbounded network list.

**Step 4: Run GREEN**

Run the focused command from Step 2. Expected: pass.

**Step 5: Commit**

```bash
git add firmware/main/product firmware/test/host
git commit -m "feat: add device wifi onboarding"
```

## Task 5: Complete BLE/Agent onboarding and global backtick routing

**Files:**

- Modify: `firmware/main/product/onboarding.hpp`
- Modify: `firmware/main/product/onboarding.cpp`
- Modify: `firmware/main/product/input_router.hpp`
- Modify: `firmware/main/product/input_router.cpp`
- Modify: `firmware/main/product/ui_navigation.hpp`
- Modify: `firmware/main/product/ui_navigation.cpp`
- Modify: `firmware/main/product/settings_menu.hpp`
- Modify: `firmware/main/product/settings_menu.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/main/probe/ble_services.hpp`
- Modify: `firmware/main/probe/ble_services.cpp`
- Modify: `firmware/test/host/test_onboarding.cpp`
- Modify: `firmware/test/host/test_input_router.cpp`
- Modify: `firmware/test/host/test_ui_navigation.cpp`
- Modify: `firmware/test/host/test_settings_menu.cpp`
- Modify: `firmware/test/host/test_product_controller.cpp`

**Step 1: Write failing priority/routing tests**

Prove:

- BLE passkey input wins over onboarding/backtick/HID;
- BLE advertising alone does not complete the BLE step;
- a persisted bond plus live HID connection completes it;
- authenticated Companion heartbeat completes the Agent step;
- backtick immediately returns from Device, Codex, Sync, Settings root,
  Settings submenu, editor, and result modal;
- backtick discards unsaved Settings edits;
- non-pet keys produce no HID reports;
- pet-page mappings still work unchanged.

**Step 2: Run RED**

Run:

```bash
cmake --build build/product-host --target \
  test_onboarding test_input_router test_ui_navigation \
  test_settings_menu test_product_controller -j
ctest --test-dir build/product-host --output-on-failure \
  -R 'onboarding|input_router|ui_navigation|settings_menu|product_controller'
```

Expected: new priority and immediate-return cases fail.

**Step 3: Implement the dispatch order**

Implement passkey -> onboarding -> global backtick -> Settings -> navigation
-> pet HID. Expose only the minimum BLE bond/connection truth needed by
onboarding. Feed the existing authenticated Companion heartbeat callback into
the state machine.

**Step 4: Run GREEN**

Run the focused command from Step 2. Expected: pass.

**Step 5: Commit**

```bash
git add firmware/main/product firmware/main/probe/ble_services.* \
  firmware/test/host
git commit -m "feat: finish companion onboarding flow"
```

## Task 6: Add the restricted setup Web experience

**Files:**

- Modify: `firmware/main/product/product_web.hpp`
- Modify: `firmware/main/product/product_web.cpp`
- Modify: `firmware/main/product/web_assets.hpp`
- Modify: `web/src/index.html`
- Modify: `web/src/app.js`
- Modify: `web/src/style.css`
- Modify: `scripts/build_web_assets.py`
- Modify: `firmware/test/host/test_product_web.cpp`
- Modify: `tools/product/tests/test_web_assets.py`

**Step 1: Write failing Web tests**

Cover:

- unauthenticated setup-progress endpoint exposes only bounded setup data;
- no PIN, Wi-Fi password, profile, or normal configuration API leaks before
  authentication;
- normal profile UI is unavailable while onboarding is incomplete;
- Agent install links are selected by platform from a public manifest/base
  URL;
- an authenticated rerun-setup action requires explicit confirmation;
- completed devices retain the current PIN login/profile behavior.

**Step 2: Run RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_web_assets.py
cmake --build build/product-host --target test_product_web -j
./build/product-host/test_product_web
```

Expected: fail on missing setup mode and endpoint contract.

**Step 3: Implement setup-only mode**

Add a minimal progress view and public installer links. Do not place Wi-Fi
credential entry back into the Web UI. Generate embedded assets with the
existing deterministic script.

**Step 4: Run GREEN**

Run the commands from Step 2 plus:

```bash
python3 scripts/build_web_assets.py --check
```

Expected: all Web/firmware tests and generated-asset check pass.

**Step 5: Commit**

```bash
git add firmware/main/product web/src scripts/build_web_assets.py \
  firmware/test/host/test_product_web.cpp \
  tools/product/tests/test_web_assets.py
git commit -m "feat: add onboarding web guidance"
```

## Task 7: Build the Windows secure configuration and device client

**Files:**

- Create: `windows-agent/go.mod`
- Create: `windows-agent/cmd/cardputer-agent/main.go`
- Create: `windows-agent/internal/config/config.go`
- Create: `windows-agent/internal/config/protector.go`
- Create: `windows-agent/internal/config/dpapi_windows.go`
- Create: `windows-agent/internal/config/protector_test.go`
- Create: `windows-agent/internal/device/client.go`
- Create: `windows-agent/internal/device/discovery.go`
- Create: `windows-agent/internal/device/client_test.go`
- Create: `windows-agent/internal/testutil/fixtures.go`

**Step 1: Write failing Go tests**

Cover config validation, atomic private writes, encrypted pairing material,
redacted errors, first authenticated certificate fingerprint capture,
subsequent fingerprint mismatch rejection, manual-IP fallback, authenticated
heartbeat, bounded retry, and product-v1 fixture decoding.

Use a fake protector on non-Windows tests. Windows builds must select the
DPAPI implementation and must not compile a plaintext fallback.

**Step 2: Run RED**

Run:

```bash
go test ./...
```

from `windows-agent/`.

Expected: fail because the module/client do not exist.

**Step 3: Implement config, DPAPI, TLS pinning, discovery, and client**

Keep secrets out of command-line arguments and logs. Initial self-signed TLS
acceptance is allowed only in the explicit PIN-authenticated first-pair path;
all later connections require the stored leaf fingerprint.

**Step 4: Run GREEN and cross-compile**

Run:

```bash
go test ./...
GOOS=windows GOARCH=amd64 CGO_ENABLED=0 go build \
  ./cmd/cardputer-agent
GOOS=windows GOARCH=arm64 CGO_ENABLED=0 go build \
  ./cmd/cardputer-agent
```

Expected: tests and both Windows builds pass.

**Step 5: Commit**

```bash
git add windows-agent
git commit -m "feat: add secure windows device client"
```

## Task 8: Implement Windows Codex telemetry and action handling

**Files:**

- Create: `windows-agent/internal/codex/jsonrpc.go`
- Create: `windows-agent/internal/codex/process.go`
- Create: `windows-agent/internal/codex/telemetry.go`
- Create: `windows-agent/internal/codex/actions.go`
- Create: `windows-agent/internal/codex/codex_test.go`
- Create: `windows-agent/internal/app/agent.go`
- Create: `windows-agent/internal/app/agent_test.go`
- Modify: `windows-agent/cmd/cardputer-agent/main.go`

**Step 1: Write failing process/telemetry tests**

Use a fake `codex` child process and fixture JSON-RPC stream. Cover startup,
request correlation, restart/backoff, clean shutdown, active session, model,
thinking, Fast placement, optional limit omission, supported action routing,
and malformed event isolation.

**Step 2: Run RED**

Run:

```bash
go test ./internal/codex ./internal/app
```

Expected: fail because the Codex and application packages are absent.

**Step 3: Implement the supervised Codex loop**

Port behavior from the existing Swift adapter/telemetry reader while keeping
the Windows process and path handling idiomatic. Do not parse private rollout
content beyond the metadata already required by the product contract.

**Step 4: Run GREEN**

Run:

```bash
go test ./...
```

Expected: all Windows Agent tests pass.

**Step 5: Commit**

```bash
git add windows-agent
git commit -m "feat: add windows codex integration"
```

## Task 9: Port deterministic pet synchronization to Windows

**Files:**

- Create: `windows-agent/internal/pet/selection.go`
- Create: `windows-agent/internal/pet/transcode.go`
- Create: `windows-agent/internal/pet/ccpt.go`
- Create: `windows-agent/internal/pet/sync.go`
- Create: `windows-agent/internal/pet/pet_test.go`
- Modify: `windows-agent/internal/app/agent.go`
- Modify: `windows-agent/go.mod`

**Step 1: Write failing compatibility tests**

Use repository fixtures to prove atlas selection, v1/v2 dimensions, custom pet
metadata, alpha/background composition, 96x104 RGB565 output, strict proven
period normalization, eight-frame CCPT v1 encoding, digest equality, chunked
resume, and no-op when the device already has the digest.

Compare expected bytes/digests with the existing Swift ProductPet output.

**Step 2: Run RED**

Run:

```bash
go test ./internal/pet
```

Expected: fail because pet support is absent.

**Step 3: Implement selection, WebP decode, transcoding, and sync**

Use `golang.org/x/image/webp`; keep the strict pixel-equality period rule and
CCPT v1 unchanged. Do not invent Windows-specific pet formats.

**Step 4: Run GREEN and cross-peer compatibility**

Run:

```bash
go test ./internal/pet ./internal/app
swift run --package-path companion -c release product-pet-tests
```

Expected: Go and Swift produce the same fixture results.

**Step 5: Commit**

```bash
git add windows-agent
git commit -m "feat: add windows pet synchronization"
```

## Task 10: Package, auto-start, diagnose, and uninstall the Windows Agent

**Files:**

- Create: `windows-agent/installer/CardputerCompanion.nsi`
- Create: `windows-agent/installer/install_task.xml.in`
- Create: `scripts/build_windows_agent.sh`
- Create: `scripts/package_windows_agent.sh`
- Create: `tools/product/tests/test_windows_agent_packaging.py`
- Modify: `windows-agent/cmd/cardputer-agent/main.go`
- Modify: `scripts/verify_product_release.sh`

**Step 1: Write failing packaging tests**

Inspect the staged installer definition and binaries for:

- per-user `%LOCALAPPDATA%` paths;
- login Scheduled Task create/remove symmetry;
- Start Menu entries;
- `status` and redacted `doctor` commands;
- uninstall removal of task, binary, config, and logs;
- no driver/service/UAC requirement;
- no embedded PIN, LAN address, internal URL, or private artifact;
- version/architecture metadata.

**Step 2: Run RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_windows_agent_packaging.py
```

Expected: fail because the Windows package does not exist.

**Step 3: Implement deterministic build and NSIS packaging**

Build amd64/arm64 portable archives and the x64 installer. The installer must
not accept the PIN as a property or command-line argument. Pairing occurs on
first run.

**Step 4: Run GREEN and inspect artifacts**

Run:

```bash
scripts/build_windows_agent.sh
scripts/package_windows_agent.sh
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_windows_agent_packaging.py
```

Expected: versioned Windows artifacts exist and pass static policy tests.

**Step 5: Commit**

```bash
git add windows-agent scripts tools/product/tests \
  scripts/verify_product_release.sh
git commit -m "feat: package windows machine agent"
```

## Task 11: Advance every release surface to 1.2.0 and update public docs

**Files:**

- Modify: `firmware/main/product/product_types.hpp`
- Modify: `companion/AppBundle/Info.plist`
- Modify: `companion/AudioDriver/Info.plist`
- Modify: `companion/Sources/CodexAppServer/JSONRPCProcess.swift`
- Modify:
  `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Modify: `scripts/mac_installer.py`
- Modify: `tools/product/tests/test_audio_driver_bundle.py`
- Modify: `tools/product/tests/test_audio_release.py`
- Modify: `tools/product/tests/test_mac_installer.py`
- Modify: `README.md`
- Modify: `docs/USER_GUIDE.md`
- Create: `docs/WINDOWS_AGENT.md`
- Create: `docs/PUBLIC_RELEASE.md`
- Create: `release/product-release.json`

**Step 1: Change the version consistency test first**

Set the expected version to `1.2.0` and add Windows/manifest surfaces.

**Step 2: Run RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_mac_installer.py \
  tools/product/tests/test_windows_agent_packaging.py
```

Expected: fail on remaining `1.1.8` surfaces.

**Step 3: Update all live version surfaces and documentation**

Document generic flash, on-device initialization, macOS/Windows installation,
Windows capability boundary, clean uninstall, and the no-private-image public
policy. Do not alter historical validation documents.

**Step 4: Run GREEN and search for stale live versions**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_mac_installer.py \
  tools/product/tests/test_windows_agent_packaging.py
rg -n '1\\.1\\.8' firmware/main companion scripts tools \
  web release README.md docs/USER_GUIDE.md docs/WINDOWS_AGENT.md \
  docs/PUBLIC_RELEASE.md
```

Expected: tests pass; any `1.1.8` match is historical/test-fixture content that
is explicitly reviewed, not a live release surface.

**Step 5: Commit**

```bash
git add firmware/main/product/product_types.hpp companion \
  scripts/mac_installer.py tools/product/tests README.md docs release
git commit -m "chore: prepare companion 1.2.0 release"
```

## Task 12: Build and verify the complete public release

**Files:**

- Modify: `scripts/verify_product_release.sh`
- Create: `dist/1.2.0-SHA256SUMS`
- Create: `docs/validation/cardputer-public-release-1.2.0.md`
- Update: `docs/2026-07-27-public-release_PROGRESS.md`

**Step 1: Make the release gate artifact-allowlist test fail**

Before regenerating artifacts, assert that only the approved `1.2.0` firmware,
macOS, Windows, manifest, and checksum outputs may be present. Confirm the
current stale/private outputs fail this precondition without printing their
contents.

**Step 2: Rebuild from a clean generated-artifact state**

Use explicit, validated project paths. Remove or move only generated `build/`
and `dist/` outputs; never touch source or workspace roots. Run:

```bash
scripts/verify_product_release.sh
```

The updated gate must include:

- complete pytest suite;
- focused audio/macOS packaging tests;
- normal and sanitizer host tests;
- generated Web asset check;
- clean ESP-IDF build and memory gate;
- Swift build and product executable tests;
- audio/HAL build/signature tests;
- Go tests and Windows cross-build;
- NSIS packaging/static tests;
- generic firmware/macOS/Windows packaging;
- history/artifact security audit;
- version consistency and artifact allowlist;
- final SHA-256 generation and verification.

**Step 3: Verify the generic firmware contains no private provisioning**

Run the auditor against the final artifact set and verify the generic full
image contains a blank Wi-Fi NVS partition according to the public partition
contract, not merely an absence of a known local password.

**Step 4: Write the release report**

Record exact test counts, build sizes, memory headroom, artifact names/digests,
security audit result, and the fact that Windows runtime HIL remains pending.
Do not record secrets or user-specific local paths in the public report.

**Step 5: Commit release metadata**

```bash
git add scripts/verify_product_release.sh \
  dist/1.2.0-SHA256SUMS \
  docs/validation/cardputer-public-release-1.2.0.md \
  docs/2026-07-27-public-release_PROGRESS.md
git commit -m "build: produce companion 1.2.0 release"
```

## Task 13: Review the release branch and integrate locally

**Files:**

- Review all changes from the design commit to branch HEAD.

**Step 1: Run completion verification from fresh commands**

Run:

```bash
git status --short --branch
git diff --check main...HEAD
scripts/verify_product_release.sh
shasum -a 256 -c dist/1.2.0-SHA256SUMS
python3 tools/product/audit_public_release.py \
  --repo . --artifacts dist
```

Expected: clean worktree and all gates pass.

**Step 2: Perform a requirements review**

Check each user requirement against code and evidence:

- retained Git history audited;
- no credential deletion needed;
- device Wi-Fi scan/select/password onboarding;
- computer-initiated BLE pairing guide;
- authenticated Agent installation completion;
- all non-pet pages use immediate backtick return;
- Windows core Agent/install/uninstall artifacts;
- generic `1.2.0` firmware built;
- final Mac purge still pending and intentionally last.

**Step 3: Fast-forward local main**

Because the repository has no configured remote, integrate only locally:

```bash
git -C /Users/nicholasliao/clawd/Cardputer_Codex_Companion \
  merge --ff-only feat/public-release-onboarding
```

Do not publish to a public remote until the destination is separately supplied
and authorized.

## Task 14: Completely remove the current Mac Agent/driver and private outputs

**Files/state removed:**

- `~/Applications/CardputerCompanion.app`
- `~/Library/LaunchAgents/com.lynx.cardputer-companion.plist`
- `~/Library/Application Support/CardputerCodexCompanion`
- `~/Library/Logs/CardputerCodexCompanion`
- `/Library/Audio/Plug-Ins/HAL/CardputerCodexMicrophone.driver`
- `/Library/PrivilegedHelperTools/com.lynx.cardputer-audio-bridge`
- `/Library/LaunchDaemons/com.lynx.cardputer-audio-bridge.plist`
- project `build/private`
- project `dist/private`

**Step 1: Resolve and verify exact targets**

Use the installed/release `1.2.0` uninstaller's status command and read-only
filesystem/launchd/process checks. Do not display config contents.

**Step 2: Run the tested purge path**

Invoke the packaged macOS installer:

```bash
python3 dist/CardputerCompanion-mac-installer/installer/mac_installer.py \
  uninstall --purge
```

Allow the native authorization prompt for the exact HAL/helper removal.

Move the two explicit ignored private project directories to Trash so the
operation remains recoverable; do not use a recursive command against a broad
directory or unresolved variable.

**Step 3: Verify the clean state**

Read-only checks must prove:

- the Companion LaunchAgent is not registered;
- no Companion/AudioBridge process remains;
- App, LaunchAgent, config, and log paths are absent;
- HAL, privileged helper, and LaunchDaemon are absent;
- `build/private` and `dist/private` are absent;
- the public `dist/` artifacts and checksum manifest remain intact.

Do not reinstall any component.

**Step 4: Record operational completion**

Update:

- `docs/2026-07-27-public-release_PROGRESS.md`
- `/Users/nicholasliao/clawd/memory/2026-07-27.md`

Record no CO was required because this was local development/Mac/device work,
the final status, security-audit conclusion, generated artifact paths, pending
Windows HIL, and the clean-uninstall result. Do not include PINs, credentials,
or private artifact contents.

Commit the final project progress update:

```bash
git add docs/2026-07-27-public-release_PROGRESS.md
git commit -m "docs: close companion 1.2.0 preparation"
```

## Final delivery

Report:

- generic firmware application/full-image paths;
- Windows installer and portable Agent paths;
- macOS installer path;
- checksum/manifest/security-report paths;
- exact verification results;
- confirmed clean Mac uninstall state;
- Windows HIL as the only deferred runtime gate;
- absence of a configured public Git destination, so no public push was
  performed.
