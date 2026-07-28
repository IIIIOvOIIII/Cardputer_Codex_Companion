# Web Login PIN Authentication Recovery Progress

## 2026-07-28 14:00 HKT

- Current work: Reproduce the post-initialization Web login failure across the
  browser, device HTTPS endpoint, PIN authentication handler, and installed
  Machine Agent.
- Expected result: Identify the first failing boundary without changing the
  newly initialized device configuration.
- Result: Partial. The device serves the login page over HTTPS at
  `192.168.1.195`, while the installed Agent is running but reports
  `LAN HTTP-503`.
- Next step: Capture the login endpoint response and trace the persisted PIN
  from setup completion through the Web guard.

## 2026-07-28 14:18 HKT

- Current work: Trace the authenticated Web bootstrap failure through the
  firmware and inspect the attached device's actual partition table.
- Expected result: Distinguish a PIN mismatch from a storage or network
  failure.
- Result: Achieved. A deliberately invalid PIN returned HTTP 401, while the
  installed Agent's saved PIN reached `/api/v1/profiles` and received HTTP
  503. Serial logs reported `wifi_cfg partition is absent`,
  `pet store unavailable: ESP_ERR_NOT_FOUND`, and
  `profile catalog storage initialization failed`. The physical partition
  table was the Launcher layout with `app0`, `cardpu`, and a 448 KiB `spiffs`
  partition, not the product layout.
- Next step: Verify Launcher's merged-image behavior and recover the attached
  device using a product-layout full flash with NVS preservation.

## 2026-07-28 14:36 HKT

- Current work: Recover the attached device after confirming Launcher's
  merged-image install boundary.
- Expected result: Install the product partition table and 1.2.3 application
  without losing the completed setup, PIN, Wi-Fi credentials, or BLE bond.
- Result: Achieved. The prior 16 KiB NVS was backed up with mode 0600, the
  verified 1.2.3 full image was written at physical address `0x0`, and the NVS
  backup was restored at `0x9000`. Serial boot proof shows the product
  partitions, application 1.2.3 from `ota_0@0x20000`, onboarding step
  `complete`, `profile catalog ready`, restored Wi-Fi, and the existing BLE
  bond. The Mac installer status now reports `LAN AUTHENTICATED`.
- Next step: Decide whether the public product replaces Launcher during first
  install or ships a separate Launcher-compatible edition with reduced
  partition guarantees.

## 2026-07-28 14:40 HKT

- Current work: Finalize the public installation architecture after the user
  selected both an official factory channel and a Launcher-compatible channel.
- Expected result: Establish exact versioning, compatibility, partition,
  diagnostic, packaging, and hardware-verification boundaries without
  maintaining a Launcher fork.
- Result: Achieved. The approved design uses runtime version `1.3.0` for the
  factory build and `1.3.0l` for a same-source Launcher build, requires
  M5Launcher 2.8.0 or later, retains the full product feature set, adds an
  explicit runtime storage contract, and separates PIN, network, and partition
  errors in the Web UI.
- Next step: Complete written-spec review, then create the test-driven
  implementation plan.

## 2026-07-28 14:50 HKT

- Current work: Convert the approved dual-release specification into a
  test-driven, file-by-file implementation and hardware-verification plan.
- Expected result: Cover same-source versioning, Launcher packaging, runtime
  partition diagnosis, Web error semantics, public installation, full release
  gates, and both hardware installation paths with no open design decisions.
- Result: Achieved. The implementation plan defines seven independently
  testable tasks, exact artifacts and interfaces, red/green commands, commit
  boundaries, attached-device backup and validation, and public release
  verification.
- Next step: Select the plan execution mode and create or verify an isolated
  worktree before changing production code.

## 2026-07-28 15:00 HKT

- Current work: Implement Task 1 same-source versioning for the factory
  firmware, Launcher firmware, macOS components, and Windows Agent.
- Expected result: Default firmware and all Agents report `1.3.0`, while a
  build-time definition produces Launcher runtime `1.3.0l` without a source
  fork.
- Result: Achieved. The RED host assertion failed against `1.2.3`; the new
  build-time boundary then passed factory and Launcher runtime tests. Focused
  host tests passed 4/4 and the rebuilt macOS/Windows version and installer
  regressions passed 32/32.
- Next step: Add failing tests for exact-boundary Launcher image packaging and
  the expanded public artifact set.
