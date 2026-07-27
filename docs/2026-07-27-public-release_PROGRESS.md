# Cardputer Codex Companion Public Release Progress

## 2026-07-27 23:46 HKT

- Current work: Completed read-only Git credential audit and obtained approval
  for the `1.2.0` public-release, onboarding, navigation, Windows Agent, build,
  and final Mac purge design.
- Expected result: A reviewed design that defines security, implementation,
  verification, and destructive-cleanup boundaries before production code is
  changed.
- Result: Achieved. No real credential was found in retained Git history; the
  approved design uses device-led onboarding, a separate Go Windows Agent,
  immediate backtick return from normal non-pet pages, and a final complete
  Mac uninstall.
- Next step: Commit the design, self-review it, obtain user review, then write
  the detailed test-driven implementation plan.

## 2026-07-27 23:52 HKT

- Current work: Converted the approved design into a task-by-task test-driven
  implementation plan covering the security gate, firmware onboarding,
  navigation, setup Web UI, Windows Agent, packaging, release verification,
  integration, and final Mac purge.
- Expected result: An executable plan with exact files, RED/GREEN commands,
  commits, destructive-action boundaries, and honest Windows HIL limits.
- Result: Achieved. The plan contains fourteen ordered implementation tasks
  and keeps complete Mac removal as the final operation after all Mac-based
  build and verification work.
- Next step: Commit the plan, obtain execution approval, create the isolated
  worktree, and begin Task 0 baseline verification.

## 2026-07-28 00:02 HKT

- Current work: Created isolated branch `feat/public-release-onboarding` at
  plan commit `24429b4`, provisioned a worktree-local Python environment, and
  ran the pre-change baseline.
- Expected result: Existing source, firmware-host, and Companion tests pass
  before implementation begins.
- Result: Achieved. Pytest passed 206/206 after generating the expected ignored
  HAL/Bridge test artifacts; CTest passed 37/37; the Swift release build and
  ProductAudio, ProductGATT, telemetry, configuration, and pet executable test
  targets all passed. Plain `swift test` is not a valid project gate on the
  current toolchain because XCTest is unavailable, so the repository's
  self-contained executable test targets remain authoritative.
- Next step: Implement Task 1's redacted all-history and public-artifact
  security gate, then remove private packaging from the public release path.

## 2026-07-28 00:30 HKT

- Current work: Completed the public-history security gate, frozen the
  cross-platform Agent protocol, added CRC-protected onboarding checkpoints,
  and integrated the first-run Wi-Fi scanner, selector, masked password editor,
  hidden-network path, and asynchronous connection flow into the firmware.
- Expected result: A clean device starts in station mode without exposing an
  AP, accepts Wi-Fi configuration entirely on Cardputer, persists only verified
  connectivity, and never sends setup keystrokes to HID.
- Result: Achieved. The redacted audit found zero credential values across 7
  refs, 189 reflog commits, 17 retained unreachable objects, and 1269 blobs.
  Focused onboarding/Wi-Fi/UI host tests pass 3/3, and the ESP32-S3 firmware
  compiles successfully at 0x18a560 bytes with 49 percent of the smallest app
  partition free.
- Next step: Bind verified BLE HID and authenticated Agent heartbeat to the
  remaining onboarding checkpoints, then enforce backtick return behavior on
  every normal subpage.

## 2026-07-28 00:47 HKT

- Current work: Completed the BLE and Machine Agent onboarding gates and
  implemented global backtick routing for normal non-pet pages.
- Expected result: Advertising alone cannot advance setup; only a bonded,
  encrypted, authenticated, subscribed HID link can pass the Bluetooth gate,
  and only an authenticated Agent request can finish setup. Backtick must
  discard pending Settings changes and return directly to the pet without
  emitting HID.
- Result: Achieved. Dispatch priority is now passkey, onboarding, global
  return, Settings, navigation, and pet HID. Seven focused host targets pass,
  and the ESP32-S3 build succeeds at 0x18a920 bytes with 49 percent free.
- Next step: Add a restricted setup status surface to the authenticated Web
  application and a deliberate restart-onboarding action.

## 2026-07-28 01:05 HKT

- Current work: Added the setup-only Web experience, bounded public progress
  endpoint, packaging-injected release base URL, platform-selected Agent
  download, and confirmed restart-setup action.
- Expected result: Before completion, browsers see only the current setup
  phase; detailed status and all normal configuration APIs remain PIN
  protected, while Agent heartbeat endpoints remain usable to complete setup.
- Result: Achieved. The public setup JSON contains only product, version,
  completion, and step. Ten Web asset tests, the firmware Web contract test,
  JavaScript syntax validation, deterministic embedded-asset verification, and
  an ESP32-S3 build all pass. The image is 0x18c420 bytes with 48 percent free.
- Next step: Implement the Windows Agent's secure configuration store and
  authenticated device client against the frozen product-v1 fixtures.
