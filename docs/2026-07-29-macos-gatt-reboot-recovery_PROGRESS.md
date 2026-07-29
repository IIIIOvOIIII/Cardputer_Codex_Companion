# macOS GATT Reboot Recovery Progress

## 2026-07-29 09:50 HKT

- Current work: Root-cause diagnosis and implementation planning for microphone
  GATT recovery after Cardputer reboot.
- Expected result: A reviewed design and executable plan that restores
  `MIC READY` without restarting the Agent.
- Result: Achieved for diagnosis and planning. Runtime evidence showed
  `BLE/Wi-Fi/Agent=OK` with `MIC=UNAVAILABLE`; a controlled Agent restart
  restored READY within one second and remained stable for 30 seconds. Source
  review confirmed missing `didFailToConnect` handling and no connection-phase
  watchdog.
- Next step: Implement the pure recovery policy with failing tests, integrate
  CoreBluetooth callbacks and timers, then run the fixed-PID five-cycle HIL
  gate.

## 2026-07-29 09:54 HKT

- Current work: Created isolated worktree
  `fix/macos-gatt-reboot-recovery-1.3.3` and established the macOS test
  baseline before implementing the recovery policy.
- Expected result: Existing focused GATT and packaging tests pass before source
  changes.
- Result: Partial. The packaging baseline remains available, while SwiftPM
  XCTest compilation is blocked by the known host boundary: the active Apple
  Command Line Tools SDK does not provide the `XCTest` module. This is
  unrelated to the GATT implementation and matches prior project validation.
- Next step: Keep the XCTest specification in source and exercise the same
  policy assertions through the repository's existing framework-free
  `product-gatt-tests` executable, beginning with a real RED compile failure.

## 2026-07-29 10:02 HKT

- Current work: Implemented the pure recovery policy and integrated it into the
  CoreBluetooth connection lifecycle.
- Expected result: Failed connects, discovery/notification/write failures,
  unintentional disconnects, and 8-second phase timeouts all converge on one
  bounded in-process retry path without restarting the Agent.
- Result: Achieved at source/build level. The executable policy test first
  failed because the policy types were absent, then passed after implementation.
  The new packaging contract also failed before integration and now passes.
  Release `product-gatt-tests`, `product-audio-tests`, and all 27 companion
  packaging tests pass. The implementation logs only phase/reason/retry/
  generation and suppresses retry after intentional stop.
- Next step: Add the fixed-PID five-cycle hardware reboot HIL runner and its
  privacy/contract tests.

## 2026-07-29 10:10 HKT

- Current work: Added the automated fixed-PID Cardputer reboot recovery HIL
  runner and documented the release acceptance gate.
- Expected result: A repeatable five-cycle reset test that authenticates
  without exposing the PIN, rejects Agent restarts, and stores only recovery
  timing/state metrics.
- Result: Achieved at tool level. The test first failed because the module did
  not exist, then all three helper/privacy tests passed. The runner uses a
  mode-0600 temporary curl configuration, validates the local HTTPS target,
  requires the same LaunchAgent PID throughout, and emits only cycle, PID,
  ready time, and microphone state.
- Next step: Build and install the repaired macOS Agent without modifying the
  existing pairing configuration, then run the real five-cycle device gate.

## 2026-07-29 10:16 HKT

- Current work: Built, signed, and installed the repaired macOS Agent while
  preserving the existing pairing config and healthy system audio components.
- Expected result: The installed user Agent runs the repaired binary, retains
  LAN authentication, and reaches `MIC READY` before the reboot gate.
- Result: Achieved. The full installer correctly rolled back when its
  unnecessary system-audio `sudo` refresh was unavailable; the unchanged HAL
  and AudioBridge remained healthy. The user App was then atomically replaced,
  its previous bundle was retained under `build/install-backup`, and the
  LaunchAgent restarted once at PID 67815. Installed version remains 1.3.2
  until the planned release bump. App/config/Agent/HAL/Bridge/audio/LAN status
  is healthy, and the device reports BLE/Wi-Fi/Agent `OK` with microphone
  `READY/NONE`.
- Next step: Keep PID 67815 fixed while performing five host-controlled
  Cardputer resets and the post-recovery serial audio start/stop proof.

## 2026-07-29 10:38 HKT

- Current work: Diagnosed the first hardware-gate failure, corrected the
  firmware-side subscription race, and completed reboot/audio-path HIL.
- Expected result: Five Cardputer resets recover `MIC READY` without changing
  the Agent PID, then serial START/STOP proves nonzero microphone transport
  without firmware faults.
- Result: Achieved. The first run exposed two separate boundaries. The attached
  device uses the M5Launcher table with `cardpu` at `0x170000`, so the product
  application was recovered and independently verified at that actual slot
  without touching NVS. More importantly, NimBLE delivered encryption and GATT
  subscription callbacks before its late `BLE_GAP_EVENT_CONNECT`; that CONNECT
  branch then cleared the already-live audio link. A RED source-contract test
  captured this event order, and the fix now clears audio state only on the
  real DISCONNECT path. Host tests pass 41/41 and the focused Python suite
  passes 7/7. Five resets returned the Agent's GATT phase to READY in 4.508,
  4.366, 9.367 (including one bounded watchdog retry), 3.792, and 1.996
  seconds while PID 67815 remained fixed. Authenticated Web confirmation also
  returned READY on all five cycles; its separate WLAN/TLS completion times
  were 16.232, 15.252, 10.362, 15.900, and 11.590 seconds. The final serial gate
  reported `HIL MIC START ACCEPTED`, `HIL MIC STOP ACCEPTED`, 739 captured
  frames, nonzero PCM peak 280, zero source overruns, zero transport drops, and
  zero panic/abort/stack/allocation-failure lines. A companion audio probe
  independently received and decoded 541 frames with signal peak 13,355,
  RMS 226.46, zero sequence gaps, and zero reconnects.
- Next step: Commit the verified recovery/root-cause fix, align every release
  surface to 1.3.3/1.3.3l, then build and run the complete release gate.
