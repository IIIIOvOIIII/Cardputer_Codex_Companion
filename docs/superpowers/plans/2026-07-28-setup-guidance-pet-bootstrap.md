# SETUP Guidance and Pet Bootstrap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver actionable first-run BLE/Agent/completion guidance and force
the Mac and Windows Machine Agents to synchronize the selected Pet immediately
when a Cardputer requests its first snapshot.

**Architecture:** Extend the existing onboarding state machine with one
non-persistent completion step, keeping stored schema 1 and the current input
priority. Add a per-loop Pet-attempt flag in each Agent so `needs_snapshot`
forces only a missing synchronization attempt. Then version and publish the
existing Factory, Launcher, Agent, Web Installer, and checksum pipelines as
1.3.1/1.3.1l.

**Tech Stack:** ESP-IDF 5.5.4/C++20, M5Unified, Swift 6/SwiftPM, Go, pytest,
CMake/CTest, GitHub Actions/Pages, esptool.

## Global Constraints

- Post-Agent guidance is transient and is skipped after a reboot.
- Stored onboarding schema remains version 1 and 12 bytes.
- All SETUP input, including final acknowledgement, remains local-only.
- The pairing name is exactly `Cardputer Codex`.
- Successful Pet cadence remains 30 seconds; failed cadence remains 5 seconds.
- `needs_snapshot` forces a missing Pet attempt without a duplicate in the
  same Agent loop.
- Factory/public artifacts contain no credentials, fixed PIN, pairing material,
  private certificate, or cached Pet.
- Publish Factory `1.3.1` and Launcher `1.3.1l`.

---

### Task 1: Firmware SETUP Guidance and Transient Completion

**Files:**
- Modify: `firmware/test/host/test_onboarding.cpp`
- Modify: `firmware/main/product/onboarding.hpp`
- Modify: `firmware/main/product/onboarding.cpp`

**Interfaces:**
- Consumes: `OnboardingStateMachine::on_agent_heartbeat(bool)` and
  `OnboardingController::on_key(...)`.
- Produces: `OnboardingStep::complete_guide` and
  `OnboardingStateMachine::acknowledge_complete()`.

- [ ] **Step 1: Write the failing firmware host assertions**

Add assertions that the BLE guide contains:

```cpp
assert(content_contains(ble_content, "ON COMPUTER: BLUETOOTH"));
assert(content_contains(ble_content, "SEARCH: CARDPUTER CODEX"));
assert(content_contains(ble_content, "TYPE COMPUTER CODE HERE"));
```

Update the Agent guide expectations to:

```cpp
assert(content_contains(agent_content, "IP:192.168.1.195 PIN:12345678"));
assert(content_contains(agent_content, "MAC: ./install.sh install"));
assert(content_contains(agent_content, "WIN: RUN 1.3.1 SETUP.EXE"));
assert(content_contains(agent_content, "WAITING HEARTBEAT..."));
```

After an authenticated heartbeat, assert:

```cpp
assert(after_ble_reboot.on_agent_heartbeat(true) == OnboardingResult::ok);
assert(after_ble_reboot.step() == OnboardingStep::complete_guide);
OnboardingController completed_guide(after_ble_reboot);
const OnboardingContent guide =
    completed_guide.content("192.168.1.195", "12345678");
assert(content_contains(guide, "SETUP COMPLETE"));
assert(content_contains(guide, "SETTINGS: FN+/ X4"));
assert(content_contains(guide, "WEB: HTTPS://192.168.1.195/"));
assert(content_contains(guide, "PIN:12345678"));
assert(content_contains(guide, "PRESS ANY KEY"));
assert(press(completed_guide, 30).captured);
assert(after_ble_reboot.step() == OnboardingStep::complete);
```

Reload the stored backend and assert the transient guide is skipped:

```cpp
OnboardingStateMachine completed_reboot(blank);
assert(completed_reboot.load(false) == OnboardingLoadResult::loaded);
assert(completed_reboot.step() == OnboardingStep::complete);
```

Add a failed-commit assertion proving the Agent page remains active.

- [ ] **Step 2: Run the focused host test and verify RED**

Run:

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_onboarding -j
ctest --test-dir build/product-host -R '^onboarding$' --output-on-failure
```

Expected: compilation or assertion failure because `complete_guide`, the new
copy, and acknowledgement behavior do not exist.

- [ ] **Step 3: Implement the minimal state and content changes**

Append the transient step without changing checkpoint values:

```cpp
enum class OnboardingStep : uint8_t {
  wifi_scan,
  wifi_select,
  wifi_password,
  wifi_connect_verify,
  ble_pair_guide,
  agent_install_guide,
  complete_guide,
  complete,
};
```

Persist the existing completed checkpoint but retain the transient step:

```cpp
return persist(
    OnboardingCheckpoint::complete,
    OnboardingStep::complete_guide
);
```

Add:

```cpp
OnboardingResult OnboardingStateMachine::acknowledge_complete() {
  if (step_ != OnboardingStep::complete_guide) {
    return OnboardingResult::ignored;
  }
  step_ = OnboardingStep::complete;
  return OnboardingResult::ok;
}
```

In `browse_key`, consume any valid press while on `complete_guide`, call
`acknowledge_complete`, and leave release capture to the existing array.
Replace BLE/Agent copy and add the five completion rows exactly as specified.

- [ ] **Step 4: Run focused and complete host tests**

Run:

```bash
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
```

Expected: all host tests pass.

- [ ] **Step 5: Commit the firmware behavior**

```bash
git add firmware/main/product/onboarding.hpp \
  firmware/main/product/onboarding.cpp \
  firmware/test/host/test_onboarding.cpp
git commit -m "feat: improve first-run setup guidance"
```

### Task 2: macOS Immediate Pet Bootstrap

**Files:**
- Modify: `companion/Tests/ProductPetExecutableTests/main.swift`
- Modify: `companion/Sources/ProductPet/PetSyncCadence.swift`
- Modify: `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Modify: `tools/product/tests/test_companion_packaging.py`

**Interfaces:**
- Consumes: `CompanionAction.needsSnapshot`,
  `PetSyncCadence.isDue(at:)`, and `PetSyncCoordinator.synchronize(client:)`.
- Produces: `PetSyncCadence.shouldAttempt(at:forced:attemptedThisLoop:)`.

- [ ] **Step 1: Write failing Swift cadence tests**

Add:

```swift
try expect(
    cadence.shouldAttempt(
        at: started.advanced(by: .seconds(1)),
        forced: true,
        attemptedThisLoop: false
    ),
    "snapshot request forces pet sync inside cadence"
)
try expect(
    !cadence.shouldAttempt(
        at: started.advanced(by: .seconds(1)),
        forced: true,
        attemptedThisLoop: true
    ),
    "snapshot request does not duplicate loop attempt"
)
```

Add a packaging-source assertion that the executable checks
`action.needsSnapshot`, gates on `petAttemptedThisLoop`, and performs the
forced synchronization before building `currentSnapshot`.

- [ ] **Step 2: Run Swift/Python focused tests and verify RED**

Run:

```bash
swift run --package-path companion -c debug product-pet-tests
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py
```

Expected: the cadence API and executable behavior assertions fail.

- [ ] **Step 3: Implement the per-loop force gate**

Add:

```swift
public func shouldAttempt(
    at now: ContinuousClock.Instant,
    forced: Bool,
    attemptedThisLoop: Bool
) -> Bool {
    !attemptedThisLoop && (forced || isDue(at: now))
}
```

In the Agent loop, set `petAttemptedThisLoop` when the ordinary cadence runs.
After `pollAction()`, if `action.needsSnapshot` and the helper returns true,
run `petSync.synchronize`, record the result, and set the flag before creating
the outgoing snapshot. Keep existing warning/output behavior in a small local
helper to avoid duplicate logging.

- [ ] **Step 4: Run complete Swift and packaging tests**

Run:

```bash
swift run --package-path companion -c debug product-pet-tests
swift test --package-path companion
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py
```

Expected: all selected tests pass.

- [ ] **Step 5: Commit the macOS behavior**

```bash
git add companion/Sources/ProductPet/PetSyncCadence.swift \
  companion/Sources/cardputer-companion/CardputerCompanionMain.swift \
  companion/Tests/ProductPetExecutableTests/main.swift \
  tools/product/tests/test_companion_packaging.py
git commit -m "feat: bootstrap pet on device snapshot request"
```

### Task 3: Windows Immediate Pet Bootstrap

**Files:**
- Modify: `windows-agent/internal/app/agent_test.go`
- Modify: `windows-agent/internal/app/agent.go`

**Interfaces:**
- Consumes: `device.ActionEnvelope.NeedsSnapshot`.
- Produces: `synchronizePetIfDue(context.Context) bool` and
  `synchronizePet(context.Context)`.

- [ ] **Step 1: Write failing Agent-loop tests**

Add one test that establishes a successful Pet sync, advances only one second,
returns `NeedsSnapshot: true`, and expects two total synchronizer calls.

Add another test whose first loop is already due and returns
`NeedsSnapshot: true`, then expects exactly one synchronizer call.

- [ ] **Step 2: Run the focused Go tests and verify RED**

Run:

```bash
cd windows-agent
go test ./internal/app -run 'Pet|Snapshot' -count=1
```

Expected: the non-due `NeedsSnapshot` case observes only the original call.

- [ ] **Step 3: Implement forced synchronization without duplication**

Change:

```go
petAttemptedThisLoop := agent.synchronizePetIfDue(ctx)
```

After heartbeat:

```go
if action.NeedsSnapshot {
    agent.lastSnapshot = nil
    if !petAttemptedThisLoop {
        agent.synchronizePet(ctx)
        petAttemptedThisLoop = true
    }
}
```

Make `synchronizePetIfDue` return whether it attempted work and move the shared
result/cadence update into `synchronizePet`.

- [ ] **Step 4: Run Go tests and race tests**

Run:

```bash
cd windows-agent
go test ./...
go test -race ./...
```

Expected: all packages pass.

- [ ] **Step 5: Commit the Windows behavior**

```bash
git add windows-agent/internal/app/agent.go \
  windows-agent/internal/app/agent_test.go
git commit -m "feat: refresh pet on Windows snapshot request"
```

### Task 4: Version and Release Contract 1.3.1

**Files:**
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify: `firmware/main/product/ui_model.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/test/host/test_product_types.cpp`
- Modify: `firmware/test/host/test_product_types_launcher.cpp`
- Modify: `firmware/test/host/test_product_web.cpp`
- Modify: `firmware/test/host/test_ui_model.cpp`
- Modify: `companion/AppBundle/Info.plist`
- Modify: `companion/AudioDriver/Info.plist`
- Modify: `companion/Sources/CodexAppServer/JSONRPCProcess.swift`
- Modify: `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Modify: `windows-agent/README.txt`
- Modify: `windows-agent/internal/codex/process.go`
- Modify: `scripts/build_windows_agent.sh`
- Modify: `scripts/mac_installer.py`
- Modify: `scripts/package_product_firmware.sh`
- Modify: `scripts/package_windows_agent.sh`
- Modify: `scripts/verify_product_release.sh`
- Modify: `tools/product/package_launcher_image.py`
- Modify: `tools/product/tests/test_public_release_artifacts.py`
- Modify: `tools/product/tests/test_web_installer.py`
- Modify: `tools/product/tests/test_audio_driver_bundle.py`
- Modify: `tools/product/tests/test_audio_release.py`
- Modify: `tools/product/tests/test_mac_installer.py`
- Modify: `tools/product/tests/test_windows_agent_packaging.py`
- Modify: `tools/product/verify_public_artifacts.py`
- Modify: `README.md`
- Modify: `README.zh-CN.md`
- Modify: `release/product-release.json`
- Modify: `web-installer/index.html`
- Modify: `web-installer/manifest.json`

**Interfaces:**
- Consumes: existing release artifact naming and Pages same-origin staging.
- Produces: Factory `1.3.1`, Launcher `1.3.1l`, Agent `1.3.1`, and matching
  Web Installer names.

- [ ] **Step 1: Update version assertions first**

Change test constants and exact expectations to `1.3.1`/`1.3.1l`, including
firmware host product types, installer metadata, artifact allowlist, Web
Installer, and public READMEs.

- [ ] **Step 2: Run version-focused tests and verify RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_public_release_artifacts.py \
  tools/product/tests/test_web_installer.py \
  tools/product/tests/test_audio_driver_bundle.py \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_windows_agent_packaging.py
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
ctest --test-dir build/product-host -R 'product_types|ui_model|product_web' \
  --output-on-failure
```

Expected: failures report remaining 1.3.0 sources/artifacts.

- [ ] **Step 3: Update production version sources and documentation**

Update firmware CMake/default macros, Launcher build flags, macOS bundle and
CLI versions, Windows defaults/resources, installers, packaging scripts,
release manifest, Web Installer, READMEs, and verification scripts.

Set `release/product-release.json` firmware digest only after the new Factory
artifact has been built and hashed; do not copy the 1.3.0 digest.

- [ ] **Step 4: Run version-focused tests GREEN**

Repeat Step 2 after creating the required 1.3.1 build/package artifacts.

- [ ] **Step 5: Commit the release contract**

```bash
git add README.md README.zh-CN.md \
  firmware/CMakeLists.txt firmware/main/product/product_types.hpp \
  firmware/main/product/ui_model.cpp firmware/test/host/CMakeLists.txt \
  firmware/test/host/test_product_types.cpp \
  firmware/test/host/test_product_types_launcher.cpp \
  firmware/test/host/test_product_web.cpp \
  firmware/test/host/test_ui_model.cpp \
  companion/AppBundle/Info.plist companion/AudioDriver/Info.plist \
  companion/Sources/CodexAppServer/JSONRPCProcess.swift \
  companion/Sources/cardputer-companion/CardputerCompanionMain.swift \
  windows-agent/README.txt windows-agent/internal/codex/process.go \
  scripts/build_windows_agent.sh scripts/mac_installer.py \
  scripts/package_product_firmware.sh scripts/package_windows_agent.sh \
  scripts/verify_product_release.sh \
  tools/product/package_launcher_image.py \
  tools/product/tests/test_audio_driver_bundle.py \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_mac_installer.py \
  tools/product/tests/test_public_release_artifacts.py \
  tools/product/tests/test_web_installer.py \
  tools/product/tests/test_windows_agent_packaging.py \
  tools/product/verify_public_artifacts.py \
  release/product-release.json web-installer/index.html \
  web-installer/manifest.json
git commit -m "chore: prepare 1.3.1 release"
```

### Task 5: Build, Hardware Verification, and Public Deployment

**Files:**
- Modify: `docs/2026-07-28-setup-guidance-pet-bootstrap_PROGRESS.md`
- Create: `docs/validation/cardputer-setup-guidance-pet-bootstrap-1.3.1.md`
- Generated locally: `dist/*1.3.1*`, `dist/*1.3.1l*`,
  `dist/1.3.1-SHA256SUMS`

**Interfaces:**
- Consumes: repository release gate, attached Cardputer serial/esptool, GitHub
  Release, and Pages workflow.
- Produces: verified public 1.3.1/1.3.1l release and live Pages installer.

- [ ] **Step 1: Run the complete release gate**

Run:

```bash
./scripts/verify_product_release.sh
```

Expected: Python, audio, host, sanitizer, Node, Swift, Go/race, ESP-IDF,
packaging, checksum, artifact allowlist, and security audit all pass.

- [ ] **Step 2: Detect and flash the attached Cardputer**

Resolve the single `/dev/cu.usbmodem*` port, inspect its current partition
layout, and use the least destructive compatible path:

```bash
cardputer_port="$(find /dev -maxdepth 1 -name 'cu.usbmodem*' -print)"
test -n "${cardputer_port}"
python -m esptool --chip esp32s3 -p "${cardputer_port}" \
  write_flash 0x20000 dist/Cardputer-Codex-Companion-1.3.1-app.bin
```

Use Factory `0x0` only if the device does not already have the product
partition table. Run `verify_flash` on the exact written range.

- [ ] **Step 3: Perform serial/HIL acceptance**

Capture boot and confirm:

- app version `1.3.1`;
- no panic, watchdog, stack overflow, or reboot;
- BLE advertising name `Cardputer Codex`;
- the SETUP BLE, Agent, and completion copy;
- completion key press/release is consumed without HID;
- Pet appears immediately after a `needs_snapshot` request.

Record exact evidence and residual manual boundaries in the validation doc.

- [ ] **Step 4: Commit and push verified source/evidence**

```bash
git add docs/2026-07-28-setup-guidance-pet-bootstrap_PROGRESS.md \
  docs/validation/cardputer-setup-guidance-pet-bootstrap-1.3.1.md
git commit -m "docs: record 1.3.1 setup and pet verification"
GIT_SSH_COMMAND='ssh -i /Users/nicholasliao/.ssh/id_co_openclaw -o IdentitiesOnly=yes' \
  git push origin main
git tag -a v1.3.1 -m "Cardputer Codex Companion 1.3.1"
GIT_SSH_COMMAND='ssh -i /Users/nicholasliao/.ssh/id_co_openclaw -o IdentitiesOnly=yes' \
  git push origin v1.3.1
```

- [ ] **Step 5: Publish release assets and verify Pages**

Create GitHub Release `v1.3.1` with the exact artifact set from
`release/product-release.json`. Confirm every GitHub-reported digest matches
the local checksum manifest. Wait for the Pages workflow on the release commit,
then download the live same-origin Factory binary and compare its SHA-256.

- [ ] **Step 6: Run final completion verification**

Run:

```bash
git status --short
GIT_SSH_COMMAND='ssh -i /Users/nicholasliao/.ssh/id_co_openclaw -o IdentitiesOnly=yes' \
  git ls-remote origin refs/heads/main refs/tags/v1.3.1
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_web_installer.py \
  tools/product/tests/test_public_release_artifacts.py \
  tools/product/tests/test_companion_packaging.py
python3 tools/product/audit_public_release.py \
  --repo "$PWD" --artifacts "$PWD/dist"
```

Expected: clean tree, remote SHA match, focused tests pass, public release
digests match, Pages Factory hash matches, and audit findings are zero.
