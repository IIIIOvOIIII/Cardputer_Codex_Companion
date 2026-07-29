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

## 2026-07-29 11:01 HKT

- Current work: Built and flashed 1.3.3l, installed the 1.3.3 macOS Agent, and
  repeated the final hardware gate against the release candidates.
- Expected result: A fixed Agent PID survives five device resets and restores
  `MIC READY`; serial control starts/stops live audio with nonzero input.
- Result: Achieved after closing one additional macOS startup race. If the
  system HID client connected before the Agent scan selected the peripheral,
  advertising stopped and the old Agent remained in `scanning` forever. A RED
  test now requires recovery lookup through both the product and HID service
  UUIDs, plus an 8-second scanning watchdog. The repaired Agent reached READY
  even when started after HID was already connected. Five further resets all
  returned `MIC READY` with PID 15317 unchanged; authenticated Web confirmation
  took 9.814, 10.682, 16.329, 16.981, and 18.555 seconds on the slow WLAN/TLS
  path, while GATT logs showed earlier recovery (for example 4.381 seconds on
  the diagnostic cycle). The serial gate reported START and STOP accepted,
  253 captured frames, PCM peak 760, zero source overruns, zero transport
  drops, and zero fallback use.
- Next step: Regenerate all release assets from the final Agent source, rerun
  the complete release/security gate, then publish 1.3.3 and deploy the Web
  Installer.

## 2026-07-29 11:10 HKT

- Current work: Regenerated all 1.3.3 release artifacts, ran the clean release
  gate, and repaired the exact attached device's damaged Launcher application
  slot without changing product or credential partitions.
- Expected result: Every public artifact verifies from the final source, the
  credential audit remains clean, and both the Launcher slot and running
  Companion remain bootable.
- Result: Achieved. The gate passed 280 product Python tests, 38 audio/installer
  tests, 41 normal host tests, 41 ASan/UBSan host tests, all executable Swift,
  C audio, Node, Go, packaging, signing, checksum, and allowlist checks. The
  full Git/ref/reflog/artifact credential audit reported zero findings. Factory
  SHA-256 is
  `66f6b092cec25de07df71855c5ba6315908a710d16e13333bed902a8d4ec34de`;
  Launcher SHA-256 is
  `4dcf11f084cd419b01221b0162edb3dd3c74774d423d49754b165d3a097e741e`.
  The current official Launcher Beta binary no longer fits this legacy
  `0x150000` app0 slot, so the corrupted range was backed up under the private
  device-backups directory and the fitting official Launcher 2.7.2 app was
  restored at `0x10000`. The write digest verified and did not touch NVS,
  `cardpu@0x170000`, or product storage. Companion then returned as 1.3.3l with
  BLE/Wi-Fi/Agent OK and microphone READY while Agent PID 15317 remained
  unchanged.
- Next step: Commit and push the verified source, publish GitHub Release
  `v1.3.3`, deploy GitHub Pages, and verify the public installer and Factory
  digest.

## 2026-07-29 11:25 HKT

- Current work: Published the verified 1.3.3 source and artifacts, deployed the
  Web Installer, and performed the final public same-origin verification.
- Expected result: Public `main` and annotated `v1.3.3` resolve to the verified
  release source; the Release contains the complete approved asset set; Pages
  serves manifest 1.3.3 and the exact checksum-pinned Factory firmware.
- Result: Achieved. Public `main` and peeled tag `v1.3.3` resolve to
  `9ca06c0b59e59359eba7b5788cd51cc525d24f22`. The GitHub Release contains nine
  verified assets; its Factory digest is
  `sha256:66f6b092cec25de07df71855c5ba6315908a710d16e13333bed902a8d4ec34de`.
  The push-triggered Pages run `30419075407` failed closed because it started
  before the Release existed. After publication, manual workflow run
  `30419438720` completed successfully. The public installer, manifest, and
  same-origin Factory image all returned HTTP 200; the manifest reports 1.3.3
  and the 1,764,704-byte Factory download hashes to the exact manifest value.
  The release source retains the five fixed-PID recovery times of 9.814,
  10.682, 16.329, 16.981, and 18.555 seconds on the authenticated Web path,
  unchanged Agent PID 15317, the successful 16 kHz serial microphone gate,
  and a zero-finding credential audit.
- Next step: Preserve the published artifacts in the main checkout, commit and
  push this release evidence, update workspace memory, and remove the merged
  feature worktree.
