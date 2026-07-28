# Launcher Wi-Fi Recovery Implementation Plan

> **For Codex:** Execute each task in order. Write and observe failing tests
> before implementation, keep the ESP event callback bounded, and do not erase
> real device data without explicit approval.

**Goal:** Deliver Factory `1.3.2` and Launcher-compatible `1.3.2l` firmware
that survives saved-Wi-Fi got-IP, retries transient scan state, and provides a
Backspace-held Y/N full Companion reset.

**Architecture:** Convert the ESP default event callback into a fixed-data
producer and make `wifi-state` the sole state-machine/NVS/UI notification
consumer. Add a host-testable scan-request policy and recovery decision model.
Run recovery after display/NVS startup but before keyboard, Wi-Fi, Web, BLE, or
product storage startup.

**Tech stack:** C++20, ESP-IDF 5.5, FreeRTOS static tasks, NVS, ESP partition
APIs, CMake host tests, pytest product gates.

---

## Task 1: Create the isolated implementation worktree

**Files:** none

1. Confirm `.worktrees/` is ignored and `main` is clean.
2. Create branch `fix/launcher-wifi-recovery-1.3.2` from current `main` in
   `.worktrees/launcher-wifi-recovery-1.3.2`.
3. Run the narrow host baseline and record the result in the progress document.

## Task 2: Specify Wi-Fi event and scan policy with failing tests

**Files:**

- Modify: `firmware/main/product/wifi_manager.hpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/test/host/test_wifi_manager.cpp`
- Add: `tools/product/tests/test_wifi_event_boundary.py`

1. Add host tests for fixed pending-event data:
   got-IP coalescing/copy, disconnect delivery, and event consumption.
2. Add host tests for scan policy:
   queued request, one in-flight scan, retry after `ESP_ERR_WIFI_STATE`, success,
   and permanent failure.
3. Add a source contract test that inspects `event_handler()` and fails if it
   contains NVS calls, notification, mutex operations, state-machine mutation,
   reconnect, or scan start.
4. Run only these tests and confirm RED.

## Task 3: Move Wi-Fi work onto `wifi-state`

**Files:**

- Modify: `firmware/main/product/wifi_manager.hpp`
- Modify: `firmware/main/product/wifi_manager.cpp`

1. Implement the minimal host-testable pending-event and scan policy helpers.
2. In `event_handler()`, copy got-IP data or set event flags and return.
3. In `wifi_timeout_task()`, consume got-IP/disconnect/scan events, mutate the
   state machine, persist candidate credentials, reconnect, publish scan
   results, and invoke status handlers.
4. Change `product_wifi_scan()` to queue a request and return `ESP_OK` after
   argument validation.
5. Retry transient scan state at the state-task cadence with bounded backoff;
   surface permanent failures through the scan handler.
6. Run the Task 2 tests and the existing Wi-Fi/onboarding host tests.

## Task 4: Specify Backspace recovery with failing tests

**Files:**

- Add: `firmware/main/product/boot_recovery.hpp`
- Add: `firmware/main/product/boot_recovery.cpp`
- Add: `firmware/test/host/test_boot_recovery.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Add: `tools/product/tests/test_boot_recovery_contract.py`

1. Test the decision model: Backspace hold threshold, Y confirmation, N cancel,
   unrelated key ignored, and no deletion before Y.
2. Test the exact reset namespace set:
   `wifi`, `product`, `product_tls`, `phase0_id`, `nimble_bond`.
3. Add a source contract test for ordering before normal keyboard/BLE/Wi-Fi
   startup and for build-specific `storage`/`assets` partition selection.
4. Run the new tests and confirm RED.

## Task 5: Implement boot recovery and full Companion deletion

**Files:**

- Modify: `firmware/main/product/keyboard_matrix.hpp`
- Modify: `firmware/main/product/keyboard_matrix.cpp`
- Modify: `firmware/main/product/display.hpp`
- Modify: `firmware/main/product/display.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/main/CMakeLists.txt`
- Implement files from Task 4

1. Add an early matrix sampler that can detect physical Backspace before the
   scanner task starts.
2. Require a continuous approximately 600 ms Backspace hold.
3. Render the dedicated recovery prompt and wait for physical Y/N without
   starting HID.
4. On Y, erase only the five Companion NVS namespaces, commit each erase, and
   erase the build-selected Companion storage partition in watchdog-safe sector
   increments.
5. On success, render confirmation and controlled restart. On failure, render a
   stable stage code and wait for retry/reboot. On N, continue startup.
6. Run all recovery tests plus display/keyboard host tests.

## Task 6: Bump and align release versions

**Files:**

- Modify all project-owned `1.3.1` release/version declarations, tests, docs,
  package scripts, manifests, agent metadata, and Web installer references
  found by `rg`.

1. Update common public version to `1.3.2`.
2. Update Launcher firmware expectation/output to `1.3.2l`.
3. Preserve protocol versions and historical release documentation where the
   text is intentionally historical.
4. Run the version-consistency and artifact-name tests.

## Task 7: Run complete software verification and build artifacts

**Files:**

- Update: `docs/2026-07-28-launcher-wifi-recovery_PROGRESS.md`

1. Run host CMake/CTest with sanitizers as supported by the project.
2. Run all `tools/product/tests`.
3. Build Factory firmware and Launcher firmware from clean build directories.
4. Run the product release and Launcher image gates.
5. Confirm artifact application descriptors report `1.3.2` and `1.3.2l`.
6. Record artifact paths, sizes, hashes, and gate results.

## Task 8: Verify on the attached Cardputer

**Files:**

- Update: `docs/2026-07-28-launcher-wifi-recovery_PROGRESS.md`

1. Resolve the attached serial device and read its partition table before any
   flash.
2. Flash the Launcher-compatible `1.3.2l` through the compatible application
   path without erasing user data.
3. Capture serial evidence through saved-Wi-Fi association and got-IP.
4. Verify no `sys_evt` stack overflow, no reset, no startup E294, and onboarding
   advances to BLE pairing.
5. Verify Backspace recovery entry and N cancel locally.
6. Do not press Y on the user's device until the user explicitly approves the
   destructive HIL step. The host tests and partition-boundary inspection cover
   Y until then.

## Task 9: Close out

**Files:**

- Update: `docs/2026-07-28-launcher-wifi-recovery_PROGRESS.md`
- Update: `/Users/nicholasliao/clawd/memory/2026-07-28.md`

1. Review the scoped diff and run `git diff --check`.
2. Commit with a focused `fix:` message.
3. Merge or fast-forward the feature branch into `main` after all gates pass.
4. Push `main` with the approved SSH identity.
5. Record root cause, artifacts, device evidence, and any deferred destructive
   Y-path HIL validation in the daily memory file without credentials.
6. Report the compiled firmware paths and remaining user action, if any.
