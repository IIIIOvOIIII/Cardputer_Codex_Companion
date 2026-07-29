# G0 Dual-Action 1.3.4 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in, device-global HID chord to G0 so a short press emits and releases the configured chord before toggling the microphone, then publish the complete product as 1.3.4.

**Architecture:** Reuse the two CRC-covered unused bytes in the existing 12-byte `display_cfg` record, expose the setting through authenticated controller-owned Web callbacks, and route enabled G0 presses through the existing macro task and dedicated keyboard-HID sender. The macro invocation owns both actions so ordering is deterministic; any queue or HID failure falls back to the privacy-critical microphone toggle.

**Tech Stack:** ESP-IDF/C++20, FreeRTOS queues, NimBLE HID, NVS, ESP HTTPS server, vanilla HTML/CSS/JavaScript, CMake/CTest with ASan/UBSan, Python/pytest release tooling, Swift Package Manager, Go, esptool, GitHub Releases and GitHub Pages.

## Global Constraints

- G0 chord scope is device-global and independent of the active keyboard Profile.
- The feature is disabled by default. Existing NVS records must decode without migration.
- A valid short press executes `chord press -> release -> Mic toggle`; a long press executes neither action.
- Queue overflow, BLE disconnect, or HID send failure must never suppress the Mic toggle.
- Do not bypass the dedicated `keyboard-hid` sender task or call NimBLE report APIs from the UI task.
- Do not change the 56-key Profile schema or G0 boot-recovery behavior.
- Do not log PINs, Wi-Fi credentials, HID content, or other private material.
- Every source, installer, manifest, firmware, Agent, checksum, and public pointer must identify the release as 1.3.4; Launcher-compatible firmware must identify as 1.3.4l.
- Do not publish until normal, sanitizer, packaged-artifact, credential-history, and attached-device HIL gates pass.

---

### Task 1: Extend the persistent Device Settings contract

**Files:**
- Modify: `firmware/main/product/device_settings.hpp`
- Modify: `firmware/main/product/device_settings.cpp`
- Modify: `firmware/test/host/test_device_settings.cpp`

**Interfaces:**

```cpp
struct DeviceSettings {
  uint8_t schema_version = 1;
  Brightness brightness = Brightness::percent_75;
  ReturnToPet return_to_pet = ReturnToPet::seconds_30;
  PetFrameRate pet_frame_rate = PetFrameRate::fps_2_5;
  bool g0_chord_enabled = false;
  uint8_t g0_chord_modifiers = 0;
  uint8_t g0_chord_usage = 0;
};

inline constexpr uint8_t kG0ChordEnabledMask = 0x80;
inline constexpr uint8_t kG0ChordModifierMask = 0x0f;
inline constexpr uint8_t kG0ChordUsageMinimum = 0x04;
inline constexpr uint8_t kG0ChordUsageMaximum = 0x65;

bool device_g0_chord_is_valid(const DeviceSettings& settings);
```

- [ ] **Step 1: Write failing codec and store tests**

Add assertions for:

1. default encode places `0x00` in record bytes 6 and 7;
2. a legacy record whose bytes 6 and 7 are zero decodes to disabled;
3. enabled `{modifiers=0x05, usage=0x19}` round-trips and encodes byte 6 as `0x19`, byte 7 as `0x85`;
4. disabled settings may retain a valid modifier/usage pair;
5. enabled usage below `0x04` or above `0x65` is invalid;
6. modifiers above `0x0f` and reserved record bits 4-6 are invalid;
7. a failed backend commit preserves `DeviceSettingsStore::current()`.

- [ ] **Step 2: Confirm RED**

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_device_settings -j
ctest --test-dir build/product-host -R '^device_settings$' --output-on-failure
```

Expected: compile/test failure because the G0 settings fields and byte codec do not exist.

- [ ] **Step 3: Implement the backward-compatible codec**

Keep `schema_version == 1` and `kDeviceSettingsRecordBytes == 12`. Encode:

```cpp
record[6] = settings.g0_chord_usage;
record[7] =
    static_cast<uint8_t>(settings.g0_chord_modifiers |
                         (settings.g0_chord_enabled
                              ? kG0ChordEnabledMask
                              : 0));
```

Decode byte 7 only after CRC validation. Reject reserved bits. Validate the
modifier mask and retained usage even when disabled; allow disabled
`usage == 0`, but require an enabled usage in `0x04..0x65`.

- [ ] **Step 4: Run focused and sanitizer tests**

```bash
cmake --build build/product-host --target test_device_settings -j
ctest --test-dir build/product-host -R '^device_settings$' --output-on-failure
cmake -S firmware/test/host -B build/product-host-sanitize \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/product-host-sanitize --target test_device_settings -j
ctest --test-dir build/product-host-sanitize \
  -R '^device_settings$' --output-on-failure
```

- [ ] **Step 5: Commit the persistence contract**

```bash
git add firmware/main/product/device_settings.{hpp,cpp} \
  firmware/test/host/test_device_settings.cpp
git commit -m "feat: persist optional G0 chord"
```

---

### Task 2: Implement deterministic G0 dual-action execution

**Files:**
- Create: `firmware/main/product/g0_dual_action.hpp`
- Create: `firmware/main/product/g0_dual_action.cpp`
- Create: `firmware/test/host/test_g0_dual_action.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/test/host/test_microphone_controller.cpp`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/main/product/product_controller.cpp`

**Interfaces:**

```cpp
enum class G0DualActionResult : uint8_t {
  microphone_only,
  chord_then_microphone,
  chord_failed_microphone_toggled,
};

class G0DualActionSink {
 public:
  virtual ~G0DualActionSink() = default;
  virtual bool execute_chord(uint8_t modifiers, uint8_t usage) = 0;
  virtual void toggle_microphone() = 0;
};

G0DualActionResult execute_g0_dual_action(
    const DeviceSettings& settings,
    G0DualActionSink& sink);
```

`execute_g0_dual_action()` must call `toggle_microphone()` exactly once even
when `execute_chord()` returns false.

- [ ] **Step 1: Write failing ordering tests**

Use a fake sink that records calls and add exact assertions:

```cpp
assert(disabled.events == std::vector{"mic"});
assert(enabled.events == std::vector{"chord:5:25", "mic"});
assert(failed.events == std::vector{"chord:5:25", "mic"});
```

Also assert the three exact result enum values. Preserve the existing
`test_microphone_controller.cpp` boundary assertions that 1000 ms is a click
and 1001 ms is ignored; add an explicit assertion that the ignored result is
never passed to the dual-action executor.

- [ ] **Step 2: Confirm RED**

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_g0_dual_action -j
```

Expected: target/source failure because the module does not exist.

- [ ] **Step 3: Implement the pure executor**

When disabled, skip `execute_chord()` and toggle Mic. When enabled, call the
chord sink first, then Mic, and return success/failure without changing the
ordering.

- [ ] **Step 4: Add a typed macro invocation**

Replace the ambiguous two-byte-only invocation with:

```cpp
enum class MacroInvocationKind : uint8_t {
  profile_key,
  g0_dual_action,
};

struct MacroInvocation {
  MacroInvocationKind kind = MacroInvocationKind::profile_key;
  uint8_t layer = 0;
  uint8_t physical_key = 0;
  uint8_t modifiers = 0;
  uint8_t usage = 0;
};
```

Add `enqueue_g0_short_press()`:

1. snapshot `g_device_settings_store.current()` through the dedicated
   `snapshot_device_settings()` locked helper introduced below;
2. if disabled, enqueue the current privacy-critical Mic click directly;
3. if enabled, enqueue one `g0_dual_action` invocation carrying the snapshot;
4. if `g_macro_queue` is null/full, log `g0 chord queue fallback` and enqueue
   the privacy-critical Mic click directly.

Do not retain a pointer/reference to mutable settings in the queue.

- [ ] **Step 5: Adapt the macro task**

Make a `G0DualActionSink` adapter whose `execute_chord()` builds one
`KeyAction{.kind=ActionKind::hid_chord, ...}`, calls `g_macro_engine.execute()`,
and returns its result. Its `toggle_microphone()` calls
`enqueue_microphone_event(MicrophoneRuntimeEvent::g0_click, true)`.

Keep profile-key handling unchanged. Log only the result class.

- [ ] **Step 6: Route the physical button through the dispatcher**

In `ui_task()`, retain current hold timing. On release:

- `click` -> `enqueue_g0_short_press()`;
- `ignored` -> current ignored Mic runtime event.

Do not change `M5.BtnA.isPressed()` boot recovery.

- [ ] **Step 7: Run host, sanitizer, and source-boundary gates**

```bash
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
cmake --build build/product-host-sanitize -j
ctest --test-dir build/product-host-sanitize --output-on-failure
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_hid_concurrency_contract.py
```

- [ ] **Step 8: Commit the runtime**

```bash
git add firmware/main firmware/test/host
git commit -m "feat: execute G0 chord before microphone toggle"
```

---

### Task 3: Add authenticated G0 Settings API

**Files:**
- Modify: `firmware/main/product/product_web.hpp`
- Modify: `firmware/main/product/product_web.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/test/host/test_product_web.cpp`

**Interfaces:**

```cpp
struct ProductWebG0ChordSettings {
  bool enabled = false;
  uint8_t modifiers = 0;
  uint8_t usage = 0;
};

using ProductG0ChordGetHandler =
    ProductWebG0ChordSettings (*)();
using ProductG0ChordApplyHandler =
    DeviceSettingsResult (*)(ProductWebG0ChordSettings);

void product_web_set_g0_chord_handlers(
    ProductG0ChordGetHandler getter,
    ProductG0ChordApplyHandler apply);
```

Routes:

```text
GET /api/v1/settings/g0-chord
PUT /api/v1/settings/g0-chord
```

Both require `X-Cardputer-Pairing`.

- [ ] **Step 1: Write failing API contract tests**

Add host assertions for:

- route count becomes 20 and both routes require pairing;
- GET JSON is exactly
  `{"enabled":false,"modifiers":0,"usages":[]}`;
- enabled config renders a single usage;
- parser accepts disabled retained chord and enabled valid chord;
- parser rejects non-boolean `enabled`, modifiers outside `0..15`, more than
  one usage, and enabled empty/out-of-range usage;
- result mapping is `400 invalid_request` for validation and
  `500 settings_save_failed` for NVS failure.

- [ ] **Step 2: Confirm RED**

```bash
cmake --build build/product-host --target test_product_web -j
ctest --test-dir build/product-host -R '^product_web$' --output-on-failure
```

- [ ] **Step 3: Implement pure JSON/parser helpers**

Keep JSON conversion and validation host-buildable in `product_web.hpp`.
The HTTPS handler reads a bounded request body and never uses partial values
after validation fails.

- [ ] **Step 4: Register handlers and routes**

Increase both `kProductWebRoutes` and the ESP `httpd_uri_t` array to 20.
`config.httpd.max_uri_handlers` continues to derive from the route contract.
Return 503 `settings_unavailable` if callbacks are not registered.

- [ ] **Step 5: Own persistence in the controller**

Add a static `g_device_settings_mutex` plus:

```cpp
DeviceSettings snapshot_device_settings();
DeviceSettingsResult commit_device_settings(
    const DeviceSettings& candidate);
```

Route existing controller reads/writes of `g_device_settings_store` through
these helpers. Getter snapshots only the three G0 fields. Apply callback copies
the snapshot, changes only those fields, calls `commit_device_settings()`, and
leaves runtime state unchanged unless NVS commit succeeds. Register the pair
before `product_web_start()`.

- [ ] **Step 6: Run focused and complete host tests**

```bash
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
cmake --build build/product-host-sanitize -j
ctest --test-dir build/product-host-sanitize --output-on-failure
```

- [ ] **Step 7: Commit the API**

```bash
git add firmware/main/product/product_web.{hpp,cpp} \
  firmware/main/product/product_controller.cpp \
  firmware/test/host/test_product_web.cpp
git commit -m "feat: expose G0 chord settings API"
```

---

### Task 4: Add the Web Settings control and regenerate embedded assets

**Files:**
- Modify: `web/src/index.html`
- Modify: `web/src/app.js`
- Modify: `web/src/device_api.js`
- Modify: `web/src/style.css`
- Modify: `web/tests/device_api.test.js`
- Modify: `tools/product/tests/test_web_assets.py`
- Regenerate: `firmware/main/product/web_assets.hpp`

- [ ] **Step 1: Add failing Web source/behavior tests**

Require these stable DOM IDs:

```text
g0-chord-form
g0-chord-enabled
g0-chord-capture
save-g0-chord
```

Extend the Node fake API tests to assert authenticated GET/PUT paths and this
save payload:

```json
{"enabled":true,"modifiers":4,"usages":[25]}
```

Assert a disabled save retains the captured `modifiers` and `usages`, and
success/failure calls the existing in-page `showResult()` dialog.

Add and export a pure browser/Node helper from `device_api.js`:

```javascript
function g0ChordPayload(enabled,draft){
  return {
    enabled:Boolean(enabled),
    modifiers:draft.modifiers,
    usages:draft.usages.slice(0,1),
  };
}
```

The browser save path must use this helper; the Node tests call the same
function rather than duplicating payload logic.

- [ ] **Step 2: Confirm RED**

```bash
PYTHONPATH=. uv run pytest -q tools/product/tests/test_web_assets.py
node --test web/tests/device_api.test.js
```

- [ ] **Step 3: Implement the Settings card**

Add Chinese text:

- title: `G0 双动作`;
- checkbox: `启用 G0 组合键`;
- help: `按下 G0 时先发送组合键，再打开或关闭 Mic。`;
- button: `保存 G0 配置`.

Use a separate `g0ChordDraft={modifiers:0,usages:[]}` so editing a Profile key
cannot mutate G0 state. Refactor the existing keydown conversion into a shared
pure helper, while `captureChord()` and `captureG0Chord()` store in their own
drafts.

- [ ] **Step 4: Implement load and save**

After PIN authentication, load `/api/v1/settings/g0-chord`. Saving PUTs the
draft without clearing it when the checkbox is off. Disable the save button
during the request and restore it in `finally`.

Use:

```javascript
showResult("success","G0 双动作配置已保存")
showResult("error",`G0 配置保存失败：${error.message}`)
```

- [ ] **Step 5: Regenerate and verify assets**

```bash
python3 scripts/build_web_assets.py
python3 scripts/build_web_assets.py --check
PYTHONPATH=. uv run pytest -q tools/product/tests/test_web_assets.py
node --test web/tests/device_api.test.js
git diff --check
```

- [ ] **Step 6: Commit the Web feature**

```bash
git add web firmware/main/product/web_assets.hpp \
  tools/product/tests/test_web_assets.py
git commit -m "feat: configure G0 chord from Web settings"
```

---

### Task 5: Add repeatable serial HIL control and proof

**Files:**
- Modify: `firmware/main/product/hil_serial_control.hpp`
- Modify: `firmware/main/product/hil_serial_control.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/test/host/test_hil_serial_control.cpp`
- Create: `scripts/product/run_g0_dual_action_hil.py`
- Create: `tools/product/tests/test_g0_dual_action_hil.py`

**Interface:**

```text
HIL G0 CLICK
```

The command must enter the same `enqueue_g0_short_press()` dispatcher as a
physical short press.

- [ ] **Step 1: Write failing parser tests**

Assert CRLF/LF acceptance, split input handling, rejection of extra tokens,
and `HilMicrophoneCommand::g0_click`.

- [ ] **Step 2: Write failing HIL helper tests**

The Python HIL runner must:

1. PUT the desired G0 config with the device PIN via a mode-0600 temporary curl
   config;
2. send `HIL G0 CLICK`;
3. observe the serial `queued/completed/fallback` result without exposing the
   PIN;
4. poll `/api/v1/status` for one Mic state transition;
5. record reset reason, boot count, queue failure delta, and timings only.

Test parser/sanitizer functions without hardware first.

- [ ] **Step 3: Confirm RED**

```bash
cmake --build build/product-host --target test_hil_serial_control -j
ctest --test-dir build/product-host \
  -R '^hil_serial_control$' --output-on-failure
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_g0_dual_action_hil.py
```

- [ ] **Step 4: Implement the serial route and runner**

Do not directly inject a Mic runtime event from the new command. The physical
button and HIL command must share `enqueue_g0_short_press()`.

- [ ] **Step 5: Run focused gates**

```bash
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_g0_dual_action_hil.py
```

- [ ] **Step 6: Commit HIL support**

```bash
git add firmware/main/product/hil_serial_control.{hpp,cpp} \
  firmware/main/product/product_controller.cpp \
  firmware/test/host/test_hil_serial_control.cpp \
  scripts/product/run_g0_dual_action_hil.py \
  tools/product/tests/test_g0_dual_action_hil.py
git commit -m "test: automate G0 dual-action hardware proof"
```

---

### Task 6: Unify active product version 1.3.4

**Files:**
- Modify active version surfaces under:
  - `firmware/CMakeLists.txt`
  - `firmware/main/product/product_types.hpp`
  - `firmware/main/product/ui_model.cpp`
  - `firmware/main/product/onboarding.cpp`
  - `companion/AppBundle/Info.plist`
  - `companion/AudioDriver/Info.plist`
  - `companion/Sources/cardputer-companion/`
  - `windows-agent/`
  - `scripts/verify_product_release.sh`
  - `release/product-release.json`
  - `README.md`
  - `README.zh-CN.md`
  - `web-installer/`
- Modify release/version tests under `tools/product/tests/`

- [ ] **Step 1: Make version tests expect 1.3.4/1.3.4l**

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_web_installer.py \
  tools/product/tests/test_windows_agent_packaging.py
```

Expected: failures for remaining active 1.3.3 surfaces.

- [ ] **Step 2: Update active surfaces**

Preserve historical design/progress/release notes. Set Factory and Agents to
1.3.4 and Launcher to 1.3.4l. Keep the Factory digest placeholder as the prior
digest only until Task 7 builds the final image; do not publish with it.

- [ ] **Step 3: Verify no active version drift**

```bash
rg -n '1\.3\.3l?' \
  firmware/CMakeLists.txt firmware/main/product \
  companion/AppBundle/Info.plist companion/AudioDriver/Info.plist \
  companion/Sources/cardputer-companion windows-agent \
  scripts/verify_product_release.sh release/product-release.json \
  README.md README.zh-CN.md web-installer
```

Expected: no active 1.3.3 reference, except explicitly labeled historical
release notes if present.

- [ ] **Step 4: Commit the release metadata**

```bash
git add firmware companion windows-agent scripts release \
  README.md README.zh-CN.md web-installer tools/product/tests
git commit -m "chore: prepare 1.3.4 release"
```

---

### Task 7: Build, flash, and pass the release gates

**Generated artifacts:**

```text
dist/Cardputer-Codex-Companion-1.3.4-factory.bin
dist/Cardputer-Codex-Companion-1.3.4-app.bin
dist/Cardputer-Codex-Companion-1.3.4l-launcher.bin
dist/CardputerCompanion-1.3.4-windows-amd64.zip
dist/CardputerCompanion-1.3.4-windows-arm64.zip
dist/CardputerCompanion-1.3.4-windows-x64-setup.exe
dist/CardputerCompanion-1.3.4-web-installer.zip
dist/1.3.4-SHA256SUMS
```

- [ ] **Step 1: Run the complete reproducible release gate**

```bash
scripts/verify_product_release.sh
```

Expected: Python, Swift, Go, Node, host C++, sanitizer, firmware, partition,
DIRAM, signing, packaging, checksum, allowlist, and credential/history gates
all exit 0.

- [ ] **Step 2: Pin the final Factory digest**

```bash
shasum -a 256 \
  dist/Cardputer-Codex-Companion-1.3.4-factory.bin
```

Apply the exact value to
`release/product-release.json.sha256.firmware_factory`, regenerate
`dist/1.3.4-SHA256SUMS`, then rerun the manifest, checksum, Web-installer, and
public-artifact tests.

- [ ] **Step 3: Flash the attached Cardputer**

Resolve exactly one current serial port rather than assuming a stale path.
Flash the Launcher-compatible app at the M5Launcher `cardpu` app offset so
persisted credentials/settings survive:

```bash
cardputer_ports="$(
  find /dev -maxdepth 1 -type c -name 'cu.usbmodem*' -print | sort
)"
test -n "${cardputer_ports}"
test "$(printf '%s\n' "${cardputer_ports}" | wc -l | tr -d ' ')" -eq 1
cardputer_port="${cardputer_ports}"
idf_python="$(
  find "$PWD/.tools/espressif/python_env" \
    -path '*/bin/python' -type f -print | sort | tail -n 1
)"
test -x "${idf_python}"
"${idf_python}" -m esptool --chip esp32s3 \
  --port "${cardputer_port}" --baud 921600 \
  write_flash 0x170000 \
  dist/Cardputer-Codex-Companion-1.3.4l-launcher.bin
```

Do not flash the Factory image over a configured device.

- [ ] **Step 4: Run attached-device HIL**

Validate both modes:

1. disabled: one G0 click changes Mic state and emits no HID report;
2. enabled with `Alt+V`: HID press and release reach the Mac before one Mic
   state change.

For each mode require:

- no boot/reset;
- no macro/HID queue failure increment;
- Web value persists across one device reboot;
- active keyboard Profile is unchanged;
- physical G0 short press matches `HIL G0 CLICK`;
- long press remains ignored.

- [ ] **Step 5: Re-run release checks after digest pinning**

```bash
scripts/verify_product_release.sh
git diff --check
git status --short
```

- [ ] **Step 6: Commit final digest and evidence**

Update `docs/2026-07-29-g0-dual-action_PROGRESS.md` with test counts, serial
port, binary paths/digests, reset result, and HIL ordering evidence.

```bash
git add release/product-release.json \
  docs/2026-07-29-g0-dual-action_PROGRESS.md
git commit -m "test: verify G0 dual-action 1.3.4 release"
```

---

### Task 8: Publish and verify v1.3.4

**Interfaces:**
- Consumes: Task 7 artifacts and exact checksums.
- Produces: pushed main, annotated tag/Release `v1.3.4`, and live Pages installer.

- [ ] **Step 1: Re-run the public credential and artifact audit**

```bash
python3 tools/product/audit_public_release.py \
  --repo "$PWD" --artifacts "$PWD/dist"
python3 tools/product/verify_public_artifacts.py \
  --dist dist --require-complete
```

Require zero findings, including repository refs/reflogs and artifact content.

- [ ] **Step 2: Push reviewed commits**

```bash
GIT_SSH_COMMAND='ssh -i /Users/nicholasliao/.ssh/id_co_openclaw -o IdentitiesOnly=yes' \
  git push origin main
```

Require local `HEAD` to equal `origin/main`.

- [ ] **Step 3: Publish GitHub Release v1.3.4**

Create the annotated tag only after gates pass. Publish the verified Factory,
app, Launcher, macOS installer, both Windows archives, Windows setup
executable, Web installer, and `1.3.4-SHA256SUMS`. Confirm the GitHub Factory
asset digest equals `release/product-release.json`.

- [ ] **Step 4: Deploy Pages after the Release exists**

Run the repository `Deploy Web Installer` workflow from `main`. Require its
fetch-and-stage Factory digest check and Pages deployment job to succeed.

- [ ] **Step 5: Verify live delivery**

Require:

```text
Web installer URL: HTTP 200
manifest version: 1.3.4
Factory release asset: HTTP 200
live Factory SHA-256: equals release/product-release.json
```

- [ ] **Step 6: Close out**

Record commit, tag, Release URL, workflow run, live URL, artifact digests, HIL
evidence, and zero-finding credential audit in the progress document and
`/Users/nicholasliao/clawd/memory/2026-07-29.md`. Commit and push the progress
closeout before reporting completion.
