# Onboarding Scan Stability and Public README Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a hardware-verified `1.2.1` release that no longer reboots after first-run Wi-Fi scanning, has English-first bilingual public documentation and an Apache-2.0 license, and is pushed to the requested GitHub repository.

**Architecture:** Defer `WIFI_EVENT_SCAN_DONE` out of ESP's default event-loop task into the existing statically allocated `wifi-state` task. Replace dynamic scan-result containers with bounded fixed arrays, retain the strongest 12 unique SSIDs, then invoke the existing product callback from the safe task context.

**Tech Stack:** ESP-IDF 5.5.4, C++20, FreeRTOS static tasks, M5Unified, CMake/CTest, pytest, Swift Package Manager, Go, esptool, Git/SSH.

## Global Constraints

- The event-loop callback must perform no scan-result allocation, sorting, product mutex acquisition, onboarding mutation, or UI rendering.
- Inspect no more than 48 AP records and publish no more than 12 unique SSIDs.
- All live version and package surfaces must be exactly `1.2.1`.
- `README.md` must be English and its first visible line must link to `README.zh-CN.md`.
- Author is `Lynx (hi@iam.lc)`; license is Apache-2.0.
- No Wi-Fi credential, pairing secret, private image, token, or private key may enter Git or `dist/`.
- Do not reinstall the locally purged macOS Agent or audio driver.
- Push only the verified `main` branch to `git@github.com:IIIIOvOIIII/Cardputer_Codex_Companion.git`.

---

### Task 1: Lock the Scan-Dispatch Regression

**Files:**
- Create: `tools/product/tests/test_wifi_scan_dispatch.py`
- Modify: `firmware/test/host/test_wifi_manager.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `firmware/main/product/wifi_manager.cpp`
- Produces: a source-boundary regression and deterministic bounded-result host tests.

- [ ] **Step 1: Write the failing source-boundary test**

Add a test which extracts the `WIFI_EVENT_SCAN_DONE` branch and asserts it
contains `g_scan_results_pending.store` but not
`publish_scan_results();`. It must also assert the `wifi_timeout_task` body
contains `g_scan_results_pending.exchange` followed by
`publish_scan_results();`.

- [ ] **Step 2: Run it and record RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_wifi_scan_dispatch.py
```

Expected before implementation: failure because the current event handler
calls `publish_scan_results()` directly.

- [ ] **Step 3: Add bounded-selection host cases**

Extend `test_wifi_manager.cpp` with duplicate SSIDs, more than 12 unique
entries, strongest-record replacement, descending RSSI, and deterministic
tie-order assertions against the pure selection helper introduced in Task 2.

- [ ] **Step 4: Keep the new host test unbuildable/RED until Task 2**

Run:

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
```

Expected: compile/link failure naming the not-yet-implemented selection
interface.

### Task 2: Defer and Bound Wi-Fi Scan Processing

**Files:**
- Modify: `firmware/main/product/wifi_manager.hpp`
- Modify: `firmware/main/product/wifi_manager.cpp`
- Test: `tools/product/tests/test_wifi_scan_dispatch.py`
- Test: `firmware/test/host/test_wifi_manager.cpp`

**Interfaces:**
- Produces: `select_wifi_scan_entries(std::span<const WifiScanEntry>, std::span<WifiScanEntry>) -> std::size_t`
- Produces: one-shot atomic `g_scan_results_pending` dispatch from the ESP event callback to `wifi_timeout_task`.

- [ ] **Step 1: Implement fixed-capacity selection**

Define the pure helper so it deduplicates by SSID, retains the strongest
record, replaces the weakest retained entry when full, and sorts the published
slice by RSSI descending and SSID ascending.

- [ ] **Step 2: Remove dynamic scan containers**

Replace both scan-path vectors with:

```cpp
std::array<wifi_ap_record_t, 48> g_scan_records{};
std::array<WifiScanEntry, 12> g_scan_candidates{};
std::array<WifiScanEntry, 12> g_scan_results{};
```

Convert at most 48 raw records into candidates and publish the helper's
returned span.

- [ ] **Step 3: Defer scan completion**

Add:

```cpp
std::atomic<bool> g_scan_results_pending{false};
```

The event handler stores `true` and returns. The `wifi-state` task uses
`exchange(false)` before invoking `publish_scan_results()`. Clear stale state
immediately before `esp_wifi_scan_start()`.

- [ ] **Step 4: Run focused GREEN gates**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_wifi_scan_dispatch.py
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
```

Expected: all pass.

- [ ] **Step 5: Commit the functional fix**

```bash
git add firmware/main/product/wifi_manager.hpp \
  firmware/main/product/wifi_manager.cpp \
  firmware/test/host/test_wifi_manager.cpp \
  firmware/test/host/CMakeLists.txt \
  tools/product/tests/test_wifi_scan_dispatch.py
git commit -m "fix: defer onboarding wifi scan processing"
```

### Task 3: Advance the Release to 1.2.1

**Files:**
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify: `companion/Sources/CardputerCompanion/main.swift`
- Modify: `audio-driver/CardputerCodexMicrophone/Info.plist`
- Modify: `scripts/mac_installer.py`
- Modify: `scripts/build_windows_agent.sh`
- Modify: `scripts/package_windows_agent.sh`
- Modify: `scripts/verify_product_release.sh`
- Modify: `release/product-release.json`
- Modify: version contract tests under `firmware/test/host/` and `tools/product/tests/`

**Interfaces:**
- Produces: one consistent `1.2.1` firmware, macOS Agent, Windows Agent, installers, manifest, and checksum naming surface.

- [ ] **Step 1: Change version contract expectations first**

Update exact version assertions and artifact-name expectations from `1.2.0`
to `1.2.1`.

- [ ] **Step 2: Run focused version tests and record RED**

Run the host product-type/UI/Web tests plus macOS installer, driver bundle,
companion packaging, Windows packaging, and public artifact tests. Expected:
failures identifying still-live `1.2.0` production surfaces.

- [ ] **Step 3: Update every live production version**

Change only live version/package declarations to `1.2.1`. Historical release
evidence and old validation documents remain historical.

- [ ] **Step 4: Run focused version tests and scan**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_driver_bundle.py \
  tools/product/tests/test_companion_packaging.py \
  tools/product/tests/test_mac_installer.py \
  tools/product/tests/test_windows_agent_packaging.py \
  tools/product/tests/test_public_release_artifacts.py
rg -n '1\\.2\\.0' firmware companion audio-driver windows-agent \
  scripts release README.md README.zh-CN.md
```

Expected: tests pass and remaining matches are reviewed historical text only.

- [ ] **Step 5: Commit release version**

```bash
git add firmware companion audio-driver windows-agent scripts release \
  tools/product/tests
git commit -m "chore: advance product release to 1.2.1"
```

### Task 4: Rewrite Public Documentation and Add License

**Files:**
- Modify: `README.md`
- Create: `README.zh-CN.md`
- Create: `LICENSE`
- Modify: `docs/PUBLIC_RELEASE.md`
- Modify: `docs/WINDOWS_AGENT.md`

**Interfaces:**
- Produces: English-first public onboarding/install/uninstall documentation and Apache-2.0 licensing.

- [ ] **Step 1: Add documentation contract tests**

Add assertions to an existing public-release test that `README.md` starts with
a link to `README.zh-CN.md`, includes flashing, macOS, Windows, author, and
license sections, and that `LICENSE` contains `Apache License` and
`Version 2.0, January 2004`.

- [ ] **Step 2: Run the documentation contract and record RED**

Expected: failure because the root README is Chinese, the Chinese target does
not exist, and no license file exists.

- [ ] **Step 3: Write English and Chinese documents**

Use exact `1.2.1` artifact names and distinguish:

```text
new/factory device: full image at 0x0
state-preserving upgrade: application image at 0x20000
```

Document Agent install/status/uninstall for macOS and Windows, the first-run
Wi-Fi/BLE/Agent gates, build/release verification, author
`Lynx (hi@iam.lc)`, and Apache-2.0.

- [ ] **Step 4: Add the canonical Apache-2.0 text**

Create `LICENSE` from the unmodified Apache License 2.0 text and use a concise
README license link rather than duplicating the license.

- [ ] **Step 5: Run documentation tests and link/path checks**

Run the focused pytest, `git diff --check`, and a local script which confirms
all relative README links resolve.

- [ ] **Step 6: Commit documentation**

```bash
git add README.md README.zh-CN.md LICENSE docs/PUBLIC_RELEASE.md \
  docs/WINDOWS_AGENT.md tools/product/tests
git commit -m "docs: publish bilingual setup and agent guide"
```

### Task 5: Build, Flash, and Prove the Hardware Fix

**Files:**
- Generate: `firmware/build/cardputer_codex_companion.bin`
- Generate: `dist/cardputer_codex_companion.bin`
- Generate: `dist/cardputer_codex_companion-full.bin`
- Create: `docs/validation/cardputer-onboarding-scan-1.2.1.md`
- Modify: `docs/2026-07-28-onboarding-scan-publication_PROGRESS.md`

**Interfaces:**
- Consumes: v1.2.1 firmware source and attached `/dev/cu.usbmodem21101`
- Produces: serial-backed scan stability evidence and generic full firmware.

- [ ] **Step 1: Build the ESP32-S3 application**

Run:

```bash
rm -f firmware/sdkconfig firmware/sdkconfig.old
(cd firmware && ../scripts/phase0/idf.sh set-target esp32s3 &&
  ../scripts/phase0/idf.sh build)
```

- [ ] **Step 2: Flash only the application partition**

Use the ESP-IDF Python environment:

```bash
python -m esptool --chip esp32s3 --port /dev/cu.usbmodem21101 \
  --before default_reset --after hard_reset \
  write_flash 0x20000 firmware/build/cardputer_codex_companion.bin
```

This preserves the current blank onboarding state.

- [ ] **Step 3: Run the 60-second serial gate**

Capture boot and scan logs. Require app version `1.2.1`, a completed scan, no
`assert failed`, no `Guru Meditation`, no panic, and no second boot banner
during the observation period.

- [ ] **Step 4: Package and verify generic firmware**

Run `scripts/package_product_firmware.sh` and
`tools/product/verify_public_firmware.py` against the full image and product
partition layout.

- [ ] **Step 5: Record hardware evidence**

Write the exact port, firmware SHA-256, observation interval, scan count,
reset/panic checks, and result to the validation document.

### Task 6: Run Release/Security Gates and Push

**Files:**
- Generate: `dist/1.2.1-SHA256SUMS`
- Modify: `docs/2026-07-28-onboarding-scan-publication_PROGRESS.md`

**Interfaces:**
- Produces: verified public repository state on GitHub `main`.

- [ ] **Step 1: Run the complete release gate**

```bash
scripts/verify_product_release.sh
```

Require Python, host normal/sanitized, firmware, Swift/C17/Go, packaging,
codesign, blank Wi-Fi partition, checksum, artifact allowlist, and credential
audit success.

- [ ] **Step 2: Review repository scope**

Run `git status --short`, `git diff --check`, `git log --oneline -8`, and
confirm no unrelated or generated files are staged.

- [ ] **Step 3: Commit final evidence**

```bash
git add docs/validation/cardputer-onboarding-scan-1.2.1.md \
  docs/2026-07-28-onboarding-scan-publication_PROGRESS.md
git commit -m "test: verify onboarding scan stability"
```

- [ ] **Step 4: Configure and authenticate the remote**

```bash
git remote add origin \
  git@github.com:IIIIOvOIIII/Cardputer_Codex_Companion.git
GIT_SSH_COMMAND='ssh -i /Users/nicholasliao/.ssh/id_co_openclaw -o IdentitiesOnly=yes' \
  ssh -T git@github.com
```

Accept GitHub's expected non-zero `ssh -T` success status only when the output
states authentication succeeded.

- [ ] **Step 5: Push verified main**

```bash
GIT_SSH_COMMAND='ssh -i /Users/nicholasliao/.ssh/id_co_openclaw -o IdentitiesOnly=yes' \
  git push -u origin main
```

- [ ] **Step 6: Verify remote identity**

Use `git ls-remote origin refs/heads/main` through the same SSH command and
require its commit ID to equal local `git rev-parse HEAD`.
