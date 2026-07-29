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
