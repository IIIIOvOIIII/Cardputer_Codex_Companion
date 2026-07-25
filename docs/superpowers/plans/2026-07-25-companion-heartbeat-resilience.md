# Companion Heartbeat Resilience Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep a running macOS Companion classified as online across transient ESP32 HTTPS failures, while still declaring a genuinely absent Companion offline after 30 seconds.

**Architecture:** Retain the existing serialized Mac HTTPS loop and firmware `CompanionProtocol`. Extend the firmware stale boundary from 10 to 30 seconds and route every authenticated Companion handler through one existing heartbeat callback. No new task, endpoint, request, storage format, or concurrency is introduced.

**Tech Stack:** ESP-IDF 5.5.4, C++20 host tests, ESP HTTPS server, pytest source-contract tests, Swift 6 Companion packaging, esptool, macOS launchd.

## Global Constraints

- The stale threshold is exactly 30,000 ms.
- Only authenticated Companion endpoints refresh Companion liveness.
- Browser status, Profile, Wi-Fi and PIN traffic must not refresh Companion liveness.
- Keep Mac action polling at two seconds, healthy pet checking at 30 seconds, failed pet retry at five seconds, and all Cardputer HTTPS operations serialized.
- Do not change BLE HID, Profile JSON, Web authentication, pet wire/storage formats, or Codex session semantics.
- Firmware version is `1.0.29` in both `firmware/CMakeLists.txt` and `firmware/main/product/product_types.hpp`.
- Deployment is application-only at `0x20000`; do not flash the full image at `0x0`.
- Preserve NVS-backed PIN, Wi-Fi credentials, Profile mappings, BLE bonds and cached pet data.
- Never print, log, document or commit the PIN or Wi-Fi credentials.

---

### Task 1: Firmware Companion Liveness Semantics

**Files:**
- Modify: `firmware/test/host/test_companion_protocol.cpp`
- Modify: `tools/product/tests/test_companion_packaging.py`
- Modify: `firmware/main/product/companion_protocol.hpp`
- Modify: `firmware/main/product/product_web.cpp`

**Interfaces:**
- Consumes: `CompanionProtocol::heartbeat(uint64_t now_ms)` and `CompanionProtocol::stale(uint64_t now_ms) const`.
- Produces: `kCompanionStaleAfterMs == 30000` and authenticated heartbeat coverage for every Companion handler.

- [ ] **Step 1: Write the stale-boundary failing test**

Change the time assertions in
`firmware/test/host/test_companion_protocol.cpp` so the first snapshot at
1,000 ms remains current through 30,999 ms and becomes stale at 31,000 ms.
Move the accepted restarted sequence to 31,000 ms. After a heartbeat at
42,000 ms, assert current through 71,999 ms and stale at 72,000 ms:

```cpp
  assert(!protocol.stale(30999));
  assert(protocol.stale(31000));

  assert(protocol.apply(restarted, 31000) ==
         CompanionMessageResult::snapshot);
  protocol.heartbeat(42000);
  assert(!protocol.stale(71999));
  assert(protocol.stale(72000));
```

- [ ] **Step 2: Run the focused host test and verify RED**

Run:

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_companion_protocol -j
ctest --test-dir build/product-host -R '^companion_protocol$' \
  --output-on-failure
```

Expected: `companion_protocol` fails because the production threshold is still
10,000 ms.

- [ ] **Step 3: Write the endpoint heartbeat failing test**

Add this test to `tools/product/tests/test_companion_packaging.py`:

```python
def test_authenticated_companion_handlers_refresh_heartbeat():
    web = (ROOT / "firmware/main/product/product_web.cpp").read_text()
    boundaries = (
        ("esp_err_t companion_status_handler",
         "esp_err_t companion_action_handler"),
        ("esp_err_t companion_action_handler",
         "esp_err_t pet_status_response"),
        ("esp_err_t pet_status_handler",
         "esp_err_t pet_begin_handler"),
        ("esp_err_t pet_begin_handler",
         "esp_err_t pet_chunk_handler"),
        ("esp_err_t pet_chunk_handler",
         "esp_err_t pet_commit_handler"),
        ("esp_err_t pet_commit_handler",
         "}  // namespace"),
    )
    for start, end in boundaries:
        handler = web.split(start, 1)[1].split(end, 1)[0]
        authorization = handler.index("authorized(request)")
        activity = handler.index("note_companion_activity();")
        assert authorization < activity
```

The exact handler slices prevent a heartbeat in one route from creating a false
positive for another route. The ordering assertion proves unauthorized
requests return before liveness activity.

- [ ] **Step 4: Run the focused source-contract test and verify RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py::test_authenticated_companion_handlers_refresh_heartbeat
```

Expected: FAIL because snapshot and pet status handlers lack heartbeat calls,
and the shared `note_companion_activity()` helper does not exist.

- [ ] **Step 5: Implement the minimal firmware change**

In `firmware/main/product/companion_protocol.hpp`, set:

```cpp
inline constexpr uint64_t kCompanionStaleAfterMs = 30000;
```

In `firmware/main/product/product_web.cpp`, define one shared callback before
the Companion handlers:

```cpp
void note_companion_activity() {
  if (g_heartbeat_handler != nullptr) g_heartbeat_handler();
}
```

Replace the existing `note_companion_pet_activity()` helper/calls and the direct
action-handler callback with `note_companion_activity()`. Call it immediately
after `authorized(request)` succeeds in:

- `companion_status_handler`;
- `companion_action_handler`;
- `pet_status_handler`;
- `pet_begin_handler`;
- `pet_chunk_handler`;
- `pet_commit_handler`.

Do not add heartbeat calls to general Web routes. The snapshot handler will
refresh once for authenticated request activity and again when a valid snapshot
is applied; both writes set the same monotonic timestamp and are harmless.

In the existing
`test_pet_sync_keeps_companion_online_and_closes_curl_pipes` assertion, replace:

```python
assert "note_companion_pet_activity();" in web
```

with:

```python
assert "note_companion_activity();" in web
```

- [ ] **Step 6: Run focused tests and verify GREEN**

Run:

```bash
cmake --build build/product-host --target test_companion_protocol -j
ctest --test-dir build/product-host -R '^companion_protocol$' \
  --output-on-failure
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py::test_authenticated_companion_handlers_refresh_heartbeat
```

Expected: both focused tests pass.

- [ ] **Step 7: Commit the liveness correction**

```bash
git add \
  firmware/test/host/test_companion_protocol.cpp \
  tools/product/tests/test_companion_packaging.py \
  firmware/main/product/companion_protocol.hpp \
  firmware/main/product/product_web.cpp
git commit -m "fix: harden companion heartbeat liveness"
```

---

### Task 2: Firmware 1.0.29 Version Contract

**Files:**
- Modify: `firmware/test/host/test_product_types.cpp`
- Modify: `firmware/test/host/test_ui_model.cpp`
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`

**Interfaces:**
- Consumes: existing `kProductVersion` UI/status contract and ESP-IDF `PROJECT_VER`.
- Produces: consistent firmware version `1.0.29` in the binary, status API, boot/device UI and build metadata.

- [ ] **Step 1: Write failing version assertions**

In `firmware/test/host/test_product_types.cpp`, change the assertion to:

```cpp
  static_assert(kProductVersion == std::string_view{"1.0.29"});
```

In `firmware/test/host/test_ui_model.cpp`, change the device-page assertion to:

```cpp
  assert(joined.find("FW:1.0.29") != std::string::npos);
```

- [ ] **Step 2: Run the version tests and verify RED**

Run:

```bash
cmake --build build/product-host \
  --target test_product_types test_ui_model -j
ctest --test-dir build/product-host \
  -R '^(product_types|ui_model)$' --output-on-failure
```

Expected: both tests fail against `1.0.28`.

- [ ] **Step 3: Implement the version bump**

In `firmware/CMakeLists.txt`, set:

```cmake
set(PROJECT_VER "1.0.29")
```

In `firmware/main/product/product_types.hpp`, set:

```cpp
inline constexpr std::string_view kProductVersion = "1.0.29";
```

- [ ] **Step 4: Run the version and liveness tests**

Run:

```bash
cmake --build build/product-host \
  --target test_product_types test_ui_model test_companion_protocol -j
ctest --test-dir build/product-host \
  -R '^(product_types|ui_model|companion_protocol)$' \
  --output-on-failure
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py::test_authenticated_companion_handlers_refresh_heartbeat
```

Expected: all selected tests pass.

- [ ] **Step 5: Verify version sources are consistent**

Run:

```bash
rg -n '1\\.0\\.(28|29)' \
  firmware/CMakeLists.txt \
  firmware/main/product/product_types.hpp \
  firmware/test/host/test_product_types.cpp \
  firmware/test/host/test_ui_model.cpp
```

Expected: every match in these four files is `1.0.29`.

- [ ] **Step 6: Commit the release version**

```bash
git add \
  firmware/test/host/test_product_types.cpp \
  firmware/test/host/test_ui_model.cpp \
  firmware/CMakeLists.txt \
  firmware/main/product/product_types.hpp
git commit -m "chore: release firmware 1.0.29"
```

---

### Task 3: Release Gate and Artifact Verification

**Files:**
- Generated, untracked: `firmware/build/cardputer_codex_companion.bin`
- Generated, untracked: `dist/cardputer_codex_companion-full.bin`
- Generated, untracked: `dist/private/cardputer_codex_companion-private-full.bin`
- Generated, untracked: `dist/CardputerCompanion.app`
- Modify after verification: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`

**Interfaces:**
- Consumes: Tasks 1–2 committed firmware source.
- Produces: fully verified 1.0.29 app image, recovery image and packaged Companion.

- [ ] **Step 1: Run the complete release gate**

Run:

```bash
scripts/verify_product_release.sh
```

Expected:

- all pytest tests pass;
- all normal host tests pass;
- all ASan/UBSan host tests pass;
- Web assets are current;
- ESP-IDF target build and partition/memory checks pass;
- Swift release build and doctor pass;
- generic/private firmware and Companion app packaging pass;
- secret/generated-artifact guards pass;
- the script prints SHA-256 values for the app and full images.

- [ ] **Step 2: Confirm firmware metadata and artifact privacy**

Run:

```bash
strings firmware/build/cardputer_codex_companion.bin | rg '1\\.0\\.29'
git status --short
if git ls-files | rg '^(build|dist)/|wifi_cfg\\.bin$'; then
  echo "generated or private artifact is tracked" >&2
  exit 1
fi
```

Expected: the binary contains `1.0.29`, generated/private artifacts are
untracked, and only intentional documentation evidence may remain modified.

- [ ] **Step 3: Record release evidence in project progress**

Append a milestone to
`docs/2026-07-24-cardputer-codex-companion_PROGRESS.md` containing:

- root cause and exact stale-window/shared-heartbeat behavior correction;
- pytest, normal host and sanitizer counts;
- ESP-IDF version and memory headroom;
- application and private full-image size/SHA-256;
- statement that no Mac polling/concurrency behavior changed;
- next step: app-only hardware deployment.

- [ ] **Step 4: Commit release evidence**

```bash
git add docs/2026-07-24-cardputer-codex-companion_PROGRESS.md
git commit -m "docs: record heartbeat release verification"
```

---

### Task 4: App-Only Hardware Deployment and Stability Acceptance

**Files:**
- Deploy: `firmware/build/cardputer_codex_companion.bin`
- Reload: `dist/CardputerCompanion.app`
- Modify after acceptance: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`
- Modify after acceptance: `/Users/nicholasliao/clawd/memory/2026-07-25.md`

**Interfaces:**
- Consumes: verified Task 3 artifacts and the existing private Companion config.
- Produces: running Cardputer 1.0.29 with stable Mac online state and preserved local configuration.

- [ ] **Step 1: Resolve the live serial target without a broad glob write**

Run:

```bash
ls -1 /dev/cu.usbmodem*
```

Expected: the connected Cardputer is `/dev/cu.usbmodem21201`. If more than one
device is listed, inspect serial identity before selecting one; never pass an
unresolved glob to esptool.

- [ ] **Step 2: Flash only the application partition**

Run:

```bash
.tools/espressif/python_env/idf5.5_py3.14_env/bin/python -m esptool \
  --chip esp32s3 \
  --port /dev/cu.usbmodem21201 \
  --baud 460800 \
  --before default_reset \
  --after hard_reset \
  write_flash \
  --flash_mode dio \
  --flash_freq 80m \
  --flash_size 8MB \
  0x20000 firmware/build/cardputer_codex_companion.bin
```

Expected: esptool reports `Hash of data verified`. Do not write the private full
image at `0x0`.

- [ ] **Step 3: Verify the flashed application bytes**

Run:

```bash
.tools/espressif/python_env/idf5.5_py3.14_env/bin/python -m esptool \
  --chip esp32s3 \
  --port /dev/cu.usbmodem21201 \
  --before default_reset \
  --after hard_reset \
  verify_flash \
  0x20000 firmware/build/cardputer_codex_companion.bin
```

Expected: the device flash matches the built 1.0.29 application.

- [ ] **Step 4: Reload the packaged Mac Companion**

Run:

```bash
python3 scripts/install_companion_launch_agent.py --load
/bin/launchctl print \
  "gui/$(id -u)/com.lynx.cardputer-companion"
```

Expected: launchd reports `state = running`, and `program` points to this
repository's `dist/CardputerCompanion.app`. Do not print the config file.

- [ ] **Step 5: Verify initial public device state**

Run:

```bash
curl -sk --max-time 5 https://192.168.1.195/api/v1/status | \
  python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["version"] == "1.0.29", d
assert d["wifi"] == "OK", d
assert d["companion"] == "OK", d
print("initial status: version=1.0.29 wifi=OK companion=OK")
'
```

Expected: version, Wi-Fi and Companion assertions pass. BLE may briefly be in
reconnect state immediately after reset; wait for it to return to `OK` without
changing the existing bond.

- [ ] **Step 6: Observe beyond the old stale window**

Sample the public status every 12 seconds for seven samples. Retry a failed
diagnostic GET up to three times without treating transport failure itself as a
Companion state:

```bash
python3 - <<'PY'
import json
import subprocess
import time

url = "https://192.168.1.195/api/v1/status"
for sample in range(7):
    status = None
    for attempt in range(3):
        result = subprocess.run(
            ["/usr/bin/curl", "-sk", "--max-time", "5", url],
            capture_output=True,
            text=True,
        )
        if result.returncode == 0:
            try:
                status = json.loads(result.stdout)
                break
            except json.JSONDecodeError:
                pass
        if attempt != 2:
            time.sleep(2)
    assert status is not None, f"status unavailable at sample {sample + 1}"
    assert status["version"] == "1.0.29", status
    assert status["wifi"] == "OK", status
    assert status["companion"] == "OK", status
    print(f"sample {sample + 1}: Mac OK")
    if sample != 6:
        time.sleep(12)
PY
```

Expected: all seven samples report `Mac OK` over approximately 72 seconds,
which spans more than seven old 10-second stale windows. These browser status
requests do not refresh Companion liveness.

- [ ] **Step 7: Verify preserved state and runtime health**

Confirm:

- BLE returns to `OK` and normal keyboard input still reaches the Mac;
- the Web login still accepts the existing PIN without rotating it;
- existing Wi-Fi address and Profile key mappings remain present;
- the cached/current pet remains visible and synchronized;
- LaunchAgent stdout continues pet/action work;
- no new panic, reboot loop, curl credential leak or repeated `Mac OFFLINE`
  appears.

Do not copy the PIN, Wi-Fi password, full config or private headers into the
progress document.

- [ ] **Step 8: Record acceptance, hashes and workspace memory**

Append the final milestone to
`docs/2026-07-24-cardputer-codex-companion_PROGRESS.md` and update
`/Users/nicholasliao/clawd/memory/2026-07-25.md` with:

- operation summary;
- `CO: Not required` for local development/device deployment;
- firmware version and app-only offset;
- release test counts;
- artifact path and SHA-256;
- real-device stability evidence;
- root cause and lesson learned;
- no secret values.

- [ ] **Step 9: Commit final evidence**

```bash
git add docs/2026-07-24-cardputer-codex-companion_PROGRESS.md
git commit -m "docs: record heartbeat deployment"
git status --short --branch
```

Expected: the repository is clean on `main`. This repository currently has no
Git remote, so there is no push target; confirm with `git remote -v`.

---

## Completion Checklist

- [ ] Stale boundary is exactly 30 seconds and covered at 29,999/30,000 ms.
- [ ] Authenticated pet status refreshes the existing heartbeat after auth.
- [ ] Unauthorized/general Web requests do not keep the Mac state online.
- [ ] Mac request cadence and serialized transport are unchanged.
- [ ] Firmware and UI report `1.0.29`.
- [ ] Full release gate passes.
- [ ] App-only flash at `0x20000` verifies successfully.
- [ ] Existing PIN, Wi-Fi, Profile, BLE bond and cached pet survive.
- [ ] BLE/Wi-Fi/Mac return to `OK`.
- [ ] Mac stays `OK` throughout the 72-second acceptance observation.
- [ ] Final private full image path and SHA-256 are delivered.
