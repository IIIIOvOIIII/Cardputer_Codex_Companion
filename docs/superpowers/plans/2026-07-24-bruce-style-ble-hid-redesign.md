# Bruce-style BLE HID Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rework the Cardputer BLE keyboard path to follow Bruce-style HID report semantics and readiness gating so macOS receives valid keyboard reports only after the HID input report is subscribed.

**Architecture:** Keep the ESP-IDF/NimBLE stack and `esp_hidd_dev_init`, but isolate HID correctness into pure host-testable functions. Runtime readiness becomes a state reducer that combines GAP, encryption, HIDD connect, and HID notify subscription state.

**Tech Stack:** ESP-IDF 5.5.4, NimBLE, ESP HID device helper, C++20 host tests, Swift Companion unchanged except prior name-compatibility tests.

## Global Constraints

- Do not import Arduino `BleKeyboard` into the ESP-IDF firmware.
- `HidReport` remains modifier + reserved + six key bytes.
- Companion GATT notification subscription must not mark the keyboard ready.
- Firmware version for this change is `1.0.16`.
- Final private full image is flashed at offset `0x0`.

---

### Task 1: Capture Bruce-compatible HID report descriptor invariants

**Files:**
- Modify: `firmware/test/host/test_hid_engine.cpp`
- Modify: `firmware/main/probe/hid_engine.cpp`
- Modify: `firmware/main/probe/hid_engine.hpp`

**Interfaces:**
- Produces: `uint8_t keyboard_report_map_key_array_slots()`
- Produces: `bool keyboard_report_map_uses_bruce_reserved_items()`

- [ ] **Step 1: Write the failing host test**

Add assertions that the keyboard report map declares six key slots and uses Bruce-compatible constant Array items for reserved input and LED padding output.

- [ ] **Step 2: Run RED**

Run:

```bash
cmake --build firmware/test/host/build --target test_hid_engine && firmware/test/host/build/test_hid_engine
```

Expected: fail because current map declares five key slots or the helper functions are missing.

- [ ] **Step 3: Implement minimal descriptor and helper fixes**

Change the key array report count to `0x06`, reserved input item to `0x81, 0x01`, and LED padding output item to `0x91, 0x01`. Add small parser helpers for the tests.

- [ ] **Step 4: Run GREEN**

Run the same host test. Expected: pass.

### Task 2: Add Bruce-style keyboard ready state reducer

**Files:**
- Modify: `firmware/test/host/test_ble_manifest.cpp`
- Modify: `firmware/main/probe/ble_services.hpp`
- Modify: `firmware/main/probe/ble_services.cpp`

**Interfaces:**
- Produces: `struct BleKeyboardLinkState`
- Produces: `bool ble_keyboard_ready_from_state(const BleKeyboardLinkState&)`
- Produces: `bool ble_keyboard_ready_requires_input_report_subscription()`

- [ ] **Step 1: Write the failing host test**

Assert that ready is false unless GAP connected, encrypted, HIDD connected, and HID input notification subscribed are all true. Assert that input subscription is required.

- [ ] **Step 2: Run RED**

Run:

```bash
cmake --build firmware/test/host/build --target test_ble_manifest && firmware/test/host/build/test_ble_manifest
```

Expected: compile or assertion failure before implementation.

- [ ] **Step 3: Implement the pure reducer**

Add the state struct and predicate. Keep it host-testable and free of NimBLE symbols.

- [ ] **Step 4: Run GREEN**

Run the same host test. Expected: pass.

### Task 3: Wire runtime BLE events to the ready reducer

**Files:**
- Modify: `firmware/main/probe/ble_services.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/main/probe/keyboard_probe.cpp`

**Interfaces:**
- Consumes: `ble_keyboard_ready_from_state`
- Runtime `ble_keyboard_ready()` returns the reducer result for live state.

- [ ] **Step 1: Add runtime state fields**

Track `gap_connected`, `encrypted`, `hidd_connected`, and `input_report_subscribed` independently.

- [ ] **Step 2: Handle subscribe events**

In `hid_gap_event`, handle NimBLE subscribe events. If `attr_handle == g_notify_handle`, treat it as Companion GATT and do not set HID subscription. For any other notify subscription, mark HID input subscribed.

- [ ] **Step 3: Update connection callbacks**

Reset all runtime HID state on disconnect and failed connect. Set HIDD state only in `ESP_HIDD_CONNECT_EVENT`. Set encrypted only on `BLE_GAP_EVENT_ENC_CHANGE` success.

- [ ] **Step 4: Log readiness transitions**

Log one concise line when readiness changes, including the four booleans.

- [ ] **Step 5: Add report-send retry**

In `EspHidReportSink::send_report`, retry `esp_hidd_dev_input_set` a small fixed number of times with one tick yield before logging failure.

### Task 4: Version, docs, release, flash, and serial verification

**Files:**
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify: `firmware/test/host/test_product_types.cpp`
- Modify: `README.md`
- Modify: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`

**Interfaces:**
- Produces firmware version `1.0.16`.

- [ ] **Step 1: Bump version to 1.0.16**

Update CMake, product constants, tests, and README boot text.

- [ ] **Step 2: Run full release gate**

Run:

```bash
scripts/verify_product_release.sh
```

Expected: Python, host, sanitizer, ESP-IDF, Swift, partition, and packaging checks pass.

- [ ] **Step 3: Flash private full image**

Run:

```bash
.tools/espressif/python_env/idf5.5_py3.14_env/bin/python -m esptool --chip esp32s3 -p /dev/cu.usbmodem21201 -b 460800 --before default_reset --after hard_reset write_flash 0x0 dist/private/cardputer_codex_companion-private-full.bin
```

Expected: `Hash of data verified.`

- [ ] **Step 4: Serial smoke**

Sample serial for at least 45 seconds and confirm app version 1.0.16, advertising, HTTPS, product runtime, and zero panic/reboot/stack/advertising-inactive indicators.

- [ ] **Step 5: Commit**

Commit with:

```bash
git commit -m "fix: rework ble hid keyboard readiness"
```
