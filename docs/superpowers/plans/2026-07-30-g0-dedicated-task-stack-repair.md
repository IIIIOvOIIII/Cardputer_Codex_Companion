# G0 Dedicated-Task Stack Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate enabled-G0 stack-overflow reboots by moving G0 dual action to a serialized dedicated task, proving repeated execution on hardware, and publishing the repair as 1.3.5.

**Architecture:** A static `product-g0` task receives bounded G0 snapshots from its own queue and executes the existing chord-then-Mic function. A shared FreeRTOS mutex serializes complete MacroEngine operations across Profile macros and G0, while queue or lock failure preserves the Mic-only fallback. Hardware HIL expands from one click to twenty sequential clicks and gates the dedicated task's stack headroom.

**Tech Stack:** C++20, ESP-IDF 5.5.4, FreeRTOS static tasks/queues/mutexes, Python 3/pytest, ESP32-S3 USB Serial/JTAG HIL, GitHub Release and Pages.

## Global Constraints

- Factory version is `1.3.5`; Launcher version is `1.3.5l`.
- G0 remains disabled by default and uses the existing persistent settings/API schema.
- Enabled G0 must send the direct BLE HID chord and release before Mic toggle.
- Queue or execution-lock failure must send no partial chord and still toggle Mic.
- Profile macros and G0 must never interleave HID press/release sequences.
- `product-g0` uses a 3,072-byte static stack, an eight-entry static queue, and the same priority as `product-macro`.
- Twenty enabled HIL iterations must retain at least 768 bytes of G0 task stack.
- Flash only the Launcher app slot at `0x170000`; preserve device storage, PIN, Wi-Fi, Profile, pet, and settings.
- The Launcher application image must remain at or below `0x190000` bytes.
- No PIN, Wi-Fi secret, pairing material, or audio content may enter logs, tests, artifacts, commits, or release notes.

---

## File Structure

- `firmware/main/product/product_controller.cpp`: owns target-only queues, tasks, execution mutex, G0 dispatch and runtime telemetry.
- `tools/product/tests/test_g0_dedicated_task_contract.py`: fail-closed source contract for the dedicated-task wiring and removal from the shared macro queue.
- `scripts/product/run_g0_dual_action_hil.py`: repeated G0 hardware runner and metrics-only report.
- `tools/product/tests/test_g0_dual_action_hil.py`: unit tests for repeated acknowledgement/completion, stack parsing and stress-report gates.
- Existing version/release files listed in Task 3: unified 1.3.5 surfaces.
- `docs/validation/cardputer-g0-dedicated-task-1.3.5.md`: hardware evidence without credentials or HID content beyond bounded result counters.
- `docs/2026-07-30-g0-stack-overflow_PROGRESS.md`: milestone record.

---

### Task 1: Dedicated G0 Queue, Task and Serialized Macro Execution

**Files:**
- Create: `tools/product/tests/test_g0_dedicated_task_contract.py`
- Modify: `firmware/main/product/product_controller.cpp`

**Interfaces:**
- Consumes: `execute_g0_dual_action(const DeviceSettings&, G0DualActionSink&)`, `MacroEngine::execute(const KeyAction&)`, `enqueue_microphone_event(MicrophoneRuntimeEvent, bool)`.
- Produces: target-only `g_g0_queue`, `g_g0_task_handle`, `g_macro_execution_mutex`, `g0_task(void*)`, and unchanged `G0DispatchResult enqueue_g0_short_press()`.

- [ ] **Step 1: Write the failing dedicated-task source contract**

Create tests that read `product_controller.cpp` and require:

```python
def test_enabled_g0_uses_a_dedicated_static_task_and_queue():
    source = CONTROLLER.read_text()
    assert "constexpr std::size_t kG0QueueDepth = 8;" in source
    assert "constexpr std::size_t kG0TaskStackBytes = 3072;" in source
    assert 'g0_task, "product-g0"' in source
    assert "xQueueSend(g_g0_queue, &invocation, 0)" in source
    assert "MacroInvocationKind::g0_dual_action" not in source


def test_profile_and_g0_workers_share_execution_serialization():
    source = CONTROLLER.read_text()
    assert "g_macro_execution_mutex" in source
    assert source.count(
        "SemaphoreLock execution_lock(g_macro_execution_mutex);"
    ) == 2
    assert "g0 macro lock fallback" in source


def test_runtime_metrics_expose_g0_stack_headroom():
    source = CONTROLLER.read_text()
    assert '"name":"g0-dual"' in source
    assert "task_stack_free_bytes(g_g0_task_handle)" in source
```

- [ ] **Step 2: Run the contract and verify RED**

Run:

```bash
python3 -m pytest -q \
  tools/product/tests/test_g0_dedicated_task_contract.py
```

Expected: failures because 1.3.4 has no dedicated G0 queue/task/mutex or
`g0-dual` telemetry and still contains `MacroInvocationKind::g0_dual_action`.

- [ ] **Step 3: Implement the static resources and task wiring**

Add the exact bounded resources:

```cpp
constexpr std::size_t kG0QueueDepth = 8;
constexpr std::size_t kG0TaskStackBytes = 3072;

struct G0Invocation {
  uint8_t modifiers = 0;
  uint8_t usage = 0;
};

StaticQueue_t g_g0_queue_storage{};
std::array<uint8_t, kG0QueueDepth * sizeof(G0Invocation)>
    g_g0_queue_buffer{};
QueueHandle_t g_g0_queue = nullptr;
StaticSemaphore_t g_macro_execution_mutex_storage{};
SemaphoreHandle_t g_macro_execution_mutex = nullptr;
StaticTask_t g_g0_task_storage{};
std::array<StackType_t, kG0TaskStackBytes> g_g0_task_stack{};
TaskHandle_t g_g0_task_handle = nullptr;
```

In `keyboard()`, create the mutex, queue and both tasks before starting the
keyboard matrix. Treat any creation failure as `ESP_ERR_NO_MEM`.

- [ ] **Step 4: Move G0 execution out of `macro_task`**

Keep `MacroInvocation` Profile-only. Wrap Profile MacroEngine execution:

```cpp
SemaphoreLock execution_lock(g_macro_execution_mutex);
if (!execution_lock.locked()) {
  ESP_LOGW(kTag, "profile macro lock timeout");
  continue;
}
g_macro_engine.execute(action);
```

Implement `g0_task`:

```cpp
void g0_task(void*) {
  G0Invocation invocation;
  while (true) {
    if (xQueueReceive(g_g0_queue, &invocation, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    SemaphoreLock execution_lock(g_macro_execution_mutex);
    if (!execution_lock.locked()) {
      ESP_LOGW(kTag, "g0 macro lock fallback");
      enqueue_microphone_event(MicrophoneRuntimeEvent::g0_click, true);
      continue;
    }
    DeviceSettings settings{
        .g0_chord_enabled = true,
        .g0_chord_modifiers = invocation.modifiers,
        .g0_chord_usage = invocation.usage,
    };
    ProductG0DualActionSink sink;
    const auto result = execute_g0_dual_action(settings, sink);
    ESP_LOGI(kTag, "g0 dual action completed result=%u",
             static_cast<unsigned>(result));
  }
}
```

Change `enqueue_g0_short_press()` to enqueue `G0Invocation` to `g_g0_queue`.
Retain the existing Mic fallback if the queue is null or full.

- [ ] **Step 5: Add G0 task telemetry**

Add:

```json
{"name":"g0-dual","configured":3072,"high_water_free_bytes":N}
```

using `task_stack_free_bytes(g_g0_task_handle)`. Do not remove existing macro,
HID, audio, UI, HTTPS or network task metrics.

- [ ] **Step 6: Run focused and host tests**

Run:

```bash
python3 -m pytest -q \
  tools/product/tests/test_g0_dedicated_task_contract.py \
  tools/product/tests/test_g0_settings_api_contract.py
cmake -S firmware -B firmware/build-host \
  -DCARDPUTER_BUILD_HOST_TESTS=ON
cmake --build firmware/build-host -j
ctest --test-dir firmware/build-host --output-on-failure
```

Expected: dedicated contract passes and all existing host tests pass.

- [ ] **Step 7: Commit Task 1**

```bash
git add firmware/main/product/product_controller.cpp \
  tools/product/tests/test_g0_dedicated_task_contract.py
git commit -m "fix: isolate G0 dual action task"
```

---

### Task 2: Repeated G0 HIL and Stack-Headroom Gate

**Files:**
- Modify: `scripts/product/run_g0_dual_action_hil.py`
- Modify: `tools/product/tests/test_g0_dual_action_hil.py`

**Interfaces:**
- Consumes: serial `HIL G0 CLICK`, authenticated status/settings APIs, runtime `tasks` telemetry.
- Produces: `--iterations` CLI option, `SerialMonitor.wait_for_line_count()`, `_task_stack_free_values()`, `build_stress_report()`, and `validate_stress_report()`.

- [ ] **Step 1: Write failing stress-report tests**

Add synthetic telemetry with a `g0-dual` entry and assert:

```python
report = module.build_stress_report(
    lines,
    expected_iterations=20,
    microphone_transition_count=20,
    elapsed_ms=12345,
)
assert report["completion_count"] == 20
assert report["g0_stack_min_free_bytes"] == 912
module.validate_stress_report(
    report,
    expected_iterations=20,
    minimum_stack_free_bytes=768,
)
```

Also prove validation rejects 19 completions, one reset, one HID queue failure,
19 Mic transitions, or 767 free stack bytes.

- [ ] **Step 2: Run tests and verify RED**

Run:

```bash
python3 -m pytest -q \
  tools/product/tests/test_g0_dual_action_hil.py
```

Expected: failures because stress helpers and repeated-line waiting do not
exist.

- [ ] **Step 3: Implement metrics parsing and fail-closed validation**

Parse only numeric telemetry:

```python
def _task_stack_free_values(lines: list[str], name: str) -> list[int]:
    values = []
    for line in lines:
        value = _json_from_line(line)
        for task in value.get("tasks", []) if value else []:
            if task.get("name") == name:
                values.append(int(task["high_water_free_bytes"]))
    return values
```

The stress report includes:

```text
expected_iterations
acknowledgement_count
completion_count
microphone_transition_count
boot_count
reset_reason
hid_queue_failure_delta
g0_stack_min_free_bytes
elapsed_ms
```

It must not include the PIN, device URL, modifier, usage, HID contents or audio
samples.

- [ ] **Step 4: Add sequential `--iterations` execution**

Add `--iterations` with integer range `1..100`, default `1`. Reject
`--disabled` with any value other than `1`.

For each enabled iteration:

1. read a stable Mic state;
2. send `HIL G0 CLICK`;
3. wait for the next acknowledgement count;
4. wait for the next completion count;
5. wait until Mic reaches the opposite stable state (`READY` versus
   `LIVE16`/`LIVE24`);
6. increment the transition counter.

After the final iteration, wait for a post-action `g0-dual` telemetry sample
and validate it. Preserve prior G0 settings in `finally`.

- [ ] **Step 5: Run HIL unit tests and existing release tests**

Run:

```bash
python3 -m pytest -q \
  tools/product/tests/test_g0_dual_action_hil.py \
  tools/product/tests/test_audio_feasibility_hil.py \
  tools/product/tests/test_audio_release.py
```

Expected: all tests pass.

- [ ] **Step 6: Commit Task 2**

```bash
git add scripts/product/run_g0_dual_action_hil.py \
  tools/product/tests/test_g0_dual_action_hil.py
git commit -m "test: stress repeated G0 dual actions"
```

---

### Task 3: Unify 1.3.5 Release Surfaces

**Files:**
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify: `companion/AppBundle/Info.plist`
- Modify: `companion/AudioDriver/Info.plist`
- Modify: `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Modify: `companion/Sources/CodexAppServer/JSONRPCProcess.swift`
- Modify: `scripts/build_windows_agent.sh`
- Modify: `scripts/mac_installer.py`
- Modify: `windows-agent/installer/CardputerCompanion.nsi`
- Modify: `release/product-release.json`
- Modify: `web-installer/manifest.json`
- Modify: `README.md`
- Modify: `README.zh-CN.md`
- Modify: `docs/PUBLIC_RELEASE.md`
- Modify: version assertions under `tools/product/tests/`

**Interfaces:**
- Consumes: current unified 1.3.4 release pattern.
- Produces: Factory/Agent `1.3.5`, Launcher `1.3.5l`, versioned artifact names and manifest references.

- [ ] **Step 1: Change the release test expectation first**

Set `VERSION = "1.3.5"` in the central release-version tests and update
expected artifact names.

- [ ] **Step 2: Run focused version tests and verify RED**

Run:

```bash
python3 -m pytest -q \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_web_assets.py \
  tools/product/tests/test_windows_agent_packaging.py
```

Expected: failures listing stale 1.3.4 surfaces.

- [ ] **Step 3: Update every active version surface**

Use `1.3.5` for Factory and Agents and `1.3.5l` for Launcher. Update release
artifact filenames and Web Installer paths. Keep the old Factory digest only as
a temporary non-publishable placeholder until Task 4 replaces it.

- [ ] **Step 4: Prove no active version drift**

Run:

```bash
rg -n '1\.3\.4l?' \
  firmware/CMakeLists.txt firmware/main/product \
  companion/AppBundle/Info.plist companion/AudioDriver/Info.plist \
  companion/Sources windows-agent scripts/mac_installer.py \
  scripts/build_windows_agent.sh release/product-release.json \
  README.md README.zh-CN.md docs/PUBLIC_RELEASE.md web-installer \
  tools/product/tests
```

Expected: no active 1.3.4 references other than explicitly historical
progress/design documents.

- [ ] **Step 5: Run version tests and commit**

```bash
python3 -m pytest -q \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_web_assets.py \
  tools/product/tests/test_windows_agent_packaging.py
git add firmware companion windows-agent scripts release web-installer \
  README.md README.zh-CN.md docs/PUBLIC_RELEASE.md tools/product/tests
git commit -m "chore: prepare 1.3.5 release"
```

---

### Task 4: Clean Build, App-Only Flash and Repeated Hardware Gate

**Files:**
- Create: `docs/validation/cardputer-g0-dedicated-task-1.3.5.md`
- Modify: `release/product-release.json`
- Modify: `docs/2026-07-30-g0-stack-overflow_PROGRESS.md`
- Generated: `dist/1.3.5-SHA256SUMS`
- Generated: versioned Factory, App, Launcher, macOS, Windows and Web assets.

**Interfaces:**
- Consumes: final Task 1-3 source, attached `/dev/cu.usbmodem21101`, existing mode-0600 Agent config, M5Launcher app offset `0x170000`.
- Produces: verified 1.3.5 artifacts and credential-free HIL evidence.

- [ ] **Step 1: Run the complete release gate**

```bash
scripts/verify_product_release.sh
```

Expected: all Python, host, sanitizer, ESP-IDF, memory, signing, packaging,
checksum, artifact allowlist and security/history checks pass.

- [ ] **Step 2: Pin the final Factory digest and rerun affected gates**

Calculate the Factory SHA-256, update
`release/product-release.json.sha256.firmware_factory`, regenerate checksums,
then rerun manifest, Web Installer, checksum and public-artifact tests.

- [ ] **Step 3: Verify the Launcher app-size boundary**

```bash
python3 tools/product/verify_launcher_app_size.py \
  firmware/build-launcher/cardputer_codex_companion.bin
```

Expected: application size is at most `0x190000`.

- [ ] **Step 4: Flash only the Launcher application slot**

Resolve exactly one current `cu.usbmodem` device and run:

```bash
python -m esptool --chip esp32s3 \
  --port /dev/cu.usbmodem21101 --baud 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x170000 \
  firmware/build-launcher/cardputer_codex_companion.bin
```

Then run `verify_flash` against the same offset and image. Do not write `0x0`
or replace storage/NVS.

- [ ] **Step 5: Run enabled repeated G0 HIL**

Read device URL/PIN from the existing mode-0600 local config without printing
them. Run:

```bash
python3 scripts/product/run_g0_dual_action_hil.py \
  --port /dev/cu.usbmodem21101 \
  --device-url https://DEVICE_IP \
  --pin-file MODE_0600_PIN_FILE \
  --iterations 20 \
  --output build/hil/g0-dedicated-task-enabled.json
```

Expected: 20 acknowledgements, 20 completions, 20 Mic transitions, zero reset,
zero queue failures and at least 768 bytes free G0 task stack.

- [ ] **Step 6: Run disabled G0 HIL**

Run the same tool with `--disabled --iterations 1`. Expected: Mic-only,
no dual-action completion, no reset and prior G0 settings restored.

- [ ] **Step 7: Record and commit hardware evidence**

Record only version, artifact hashes, task stack minimum, counters, reset
result and final service status. Do not record PIN, URL, modifier/usage or audio
content.

```bash
git add release/product-release.json \
  docs/validation/cardputer-g0-dedicated-task-1.3.5.md \
  docs/2026-07-30-g0-stack-overflow_PROGRESS.md
git commit -m "docs: record 1.3.5 G0 hardware verification"
```

---

### Task 5: Integrate and Publish 1.3.5

**Files:**
- Modify: `docs/2026-07-30-g0-stack-overflow_PROGRESS.md`
- Generated/release assets: nine public 1.3.5 files matching the 1.3.4 release layout.

**Interfaces:**
- Consumes: green Task 4 commit and artifacts.
- Produces: `main`, annotated `v1.3.5`, GitHub Release, Pages deployment and local `dist/` copies.

- [ ] **Step 1: Fast-forward `main` and verify the merged source**

Merge the isolated feature branch into `main` without rewriting unrelated
history. Run focused tests and `git diff --check` on the merged result.

- [ ] **Step 2: Push `main` and annotated tag**

Use the repository's authorized SSH key:

```bash
GIT_SSH_COMMAND='ssh -i ~/.ssh/id_co_openclaw -o IdentitiesOnly=yes' \
  git push origin main
git tag -a v1.3.5 -m "Cardputer Codex Companion 1.3.5"
GIT_SSH_COMMAND='ssh -i ~/.ssh/id_co_openclaw -o IdentitiesOnly=yes' \
  git push origin v1.3.5
```

- [ ] **Step 3: Publish and verify GitHub Release assets**

Retrieve `webapp.external.github_com.api_token` through the `lynx-vault`
service-account flow without printing or persisting it. Create a non-draft,
non-prerelease `v1.3.5` release with checksums, App, Factory, Launcher, macOS,
Web Installer, Windows amd64/ARM64 and Windows setup assets.

Verify all nine GitHub asset digests against local files.

- [ ] **Step 4: Deploy and verify Pages**

Dispatch `.github/workflows/pages.yml` from `main`, wait for success, then
download the public manifest and Factory image. Require manifest version
`1.3.5` and exact pinned Factory SHA-256.

- [ ] **Step 5: Final live verification and closeout**

Verify:

- remote `main` equals local `HEAD`;
- tag resolves to the hardware-verified commit;
- device reports `1.3.5l`, BLE/Wi-Fi/Agent `OK`, Mic stable;
- authenticated G0 settings API remains valid and the user's prior setting was
  restored;
- repository tracked files are clean.

Update the progress and daily memory records, commit/push documentation, and
report Release, Web Installer and local artifact paths.
