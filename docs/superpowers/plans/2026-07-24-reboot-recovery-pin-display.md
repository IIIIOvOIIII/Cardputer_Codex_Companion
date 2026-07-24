# Reboot Recovery, Stable PIN, and Larger Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use inline execution in this session. Subagents are not used because the active session explicitly forbids delegation unless requested by the user. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make BLE recover after Cardputer reboot, persist the Web PIN across reboots unless rotated, and increase the Cardputer display text size.

**Architecture:** Keep the existing strict BLE HID-ready gate and add a watchdog policy that restarts advertising or terminates stale half-ready links. Keep Web PIN storage in the existing product NVS namespace and persist the first generated PIN. Increase display size by shortening `UiModel::runtime_text()` and using a larger body font in `display.cpp`.

**Tech Stack:** ESP-IDF 5.5.4, NimBLE/esp_hidd, M5Unified display, C++20 host tests, Python pytest release checks, Swift companion release build.

## Global Constraints

- Do not weaken BLE security: MITM/passkey pairing remains required.
- Do not put PIN values in Git, memory, launchd plist, process arguments, or logs.
- Preserve current HTTPS/Web API compatibility.
- Full private firmware image remains `dist/private/cardputer_codex_companion-private-full.bin` and is flashed at `0x0`.

---

### Task 1: BLE reconnect watchdog policy

**Files:**
- Modify: `firmware/main/probe/ble_services.hpp`
- Modify: `firmware/main/probe/ble_services.cpp`
- Modify: `firmware/test/host/test_ble_manifest.cpp`
- Modify: `firmware/main/product/product_controller.cpp`

**Interfaces:**
- Produces: `ble_should_start_advertising(bool connected, bool advertising_active) -> bool`
- Produces: `ble_stale_link_timeout_ms() -> uint32_t`
- Produces: `ble_should_reset_stale_link(const BleKeyboardLinkState&, uint64_t now_ms, uint64_t state_changed_ms) -> bool`

- [x] Write failing tests in `test_ble_manifest.cpp` asserting:
  - advertising watchdog interval is non-zero;
  - advertising starts when disconnected and inactive;
  - stale half-ready links time out;
  - fully ready links do not time out.
- [x] Run `cmake --build build/product-host --target test_ble_manifest && build/product-host/test_ble_manifest` and confirm failure.
- [x] Implement the helpers and runtime watchdog task.
- [x] Run the same test and confirm pass.

### Task 2: Stable Web PIN lifecycle

**Files:**
- Modify: `firmware/main/product/product_web.hpp`
- Modify: `firmware/main/product/product_web.cpp`
- Modify: `firmware/test/host/test_product_web.cpp`

**Interfaces:**
- Produces: `enum class ProductWebPinLoadAction { use_stored, generate_and_persist, generate_ephemeral }`
- Produces: `product_web_pin_load_action(bool nvs_open_ok, bool stored_found, bool stored_valid) -> ProductWebPinLoadAction`

- [x] Write failing tests asserting first valid NVS PIN is reused, missing/invalid NVS PIN is generated and persisted, and NVS-open failure is ephemeral.
- [x] Run `cmake --build build/product-host --target test_product_web && build/product-host/test_product_web` and confirm failure.
- [x] Modify `load_pairing_code()` to open NVS read/write, reuse valid stored PIN, otherwise generate once and persist.
- [x] Run the same test and confirm pass.

### Task 3: Larger display text

**Files:**
- Modify: `firmware/main/product/ui_model.cpp`
- Modify: `firmware/main/product/display.cpp`
- Modify: `firmware/test/host/test_ui_model.cpp`
- Modify: `tools/product/tests/test_companion_packaging.py`

**Interfaces:**
- Produces: shortened `UiModel::runtime_text()` suitable for text size 2.
- Produces: display body text size constant of `2`.

- [x] Write failing tests asserting runtime text uses `B:`, `W:`, `M:`, separates IP/PIN, and does not include cwd.
- [x] Add Python regression asserting display body text size constant is `2`.
- [x] Run `cmake --build build/product-host --target test_ui_model && build/product-host/test_ui_model` and `python3 -m pytest tools/product/tests/test_companion_packaging.py -q`; confirm failure.
- [x] Implement shortened runtime text and display renderer size constants.
- [x] Run the same tests and confirm pass.

### Task 4: Version, full release, flash, and evidence

**Files:**
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify: `firmware/test/host/test_product_types.cpp`
- Modify: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`
- Modify: `/Users/nicholasliao/clawd/memory/2026-07-24.md`

- [x] Bump product version to `1.0.21`.
- [x] Run `scripts/verify_product_release.sh`.
- [x] Flash the 1.0.21 application image to `/dev/cu.usbmodem21201` at `0x20000` to preserve product NVS and the current Web PIN; full private image remains generated for blank-device/full-recovery flashing.
- [x] Verify status endpoint and Mac agent state.
- [x] Update progress and workspace memory without recording secrets.
- [x] Commit all changes locally.
