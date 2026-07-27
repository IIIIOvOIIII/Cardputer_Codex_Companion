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

## 2026-07-28 00:53 HKT

- Current work: Implemented the Windows Agent secure configuration foundation,
  DPAPI protection boundary, certificate-pinned HTTPS device client, bounded
  retry behavior, manual-address fallback, and initial command-line pairing
  and heartbeat entry points.
- Expected result: Pairing material is never persisted in plaintext, the first
  authenticated request captures the device certificate fingerprint, every
  later request rejects certificate changes, and both supported Windows
  architectures compile without CGO.
- Result: Achieved. The Go unit suite passes, including atomic private-file,
  redaction, authentication, pinning, retry, and product-fixture tests.
  Cross-compilation produced valid PE32+ x86-64 and ARM64 executables using
  Windows DPAPI with no plaintext storage fallback.
- Next step: Add Codex session telemetry and action execution to the Windows
  Agent, keeping the product-v1 status and action contracts aligned with the
  existing macOS implementation.

## 2026-07-28 01:01 HKT

- Current work: Added the supervised Windows Codex app-server process,
  JSON-RPC correlation and approval handling, active-session normalization,
  rollout metadata telemetry, optional quota classification, action routing,
  status posting, and persistent PIN migration.
- Expected result: The Windows Agent restarts a failed Codex child within a
  bound, isolates malformed or oversized rollout events, never serializes
  unavailable limits, and mirrors the macOS session/action contract.
- Result: Achieved. The complete Go suite, `go vet`, race-enabled Codex/app/
  device/config tests, and amd64 plus ARM64 Windows cross-builds pass. Tests
  cover clean shutdown, startup recovery, response correlation, timeout
  cancellation, active session/model/Thinking/Fast telemetry, limit omission,
  all supported actions, status authentication, and PIN migration.
- Next step: Port deterministic pet selection, WebP composition, CCPT v1
  encoding, strict proven-period normalization, and resumable synchronization
  with byte-level Swift compatibility evidence.

## 2026-07-28 01:15 HKT

- Current work: Ported official/custom pet selection, WebP decoding, alpha and
  background composition, strict pixel-proven period expansion, CCPT v1
  encoding, digest calculation, device HTTP methods, resumable 8192-byte
  upload, synchronization cadence, and snapshot pet metadata to Windows.
- Expected result: Windows and macOS produce identical firmware bundles from
  identical inputs; unchanged device digests avoid uploads, and interrupted
  transfers resume only from a valid device-reported offset.
- Result: Achieved. A shared product fixture proves the exact 25,772-byte
  bundle and both SHA-256 values in Swift and Go. A real Rocky WebP
  compatibility probe also produced the same 472,264-byte output and identical
  content/upload digests on both implementations. Go unit, vet, race,
  authenticated device-client, amd64/ARM64 cross-build, and Swift ProductPet
  tests pass.
- Next step: Add deterministic Windows build/package scripts, per-user
  Scheduled Task auto-start, status and redacted doctor commands, and complete
  uninstall symmetry without a driver, service, UAC, or embedded pairing data.

## 2026-07-28 01:24 HKT

- Current work: Completed Windows first-pair CLI, masked PIN entry, status and
  redacted doctor commands, bounded logs, deterministic dual-architecture
  builds, reproducible portable archives, per-user NSIS install, least-
  privilege login Scheduled Task, Start Menu entries, and purge uninstall.
- Expected result: Windows users can install and pair without command-line
  secrets or elevation; Agent restart is user-scoped, and uninstall removes
  its task, binaries, configuration, logs, and shortcuts without touching a
  driver or service.
- Result: Achieved at build/static gate. Five packaging policy tests pass,
  rebuilds are byte-reproducible, produced binaries are PE32+ x86-64 and ARM64,
  and the x64 setup is a valid NSIS GUI executable. Go race and cross-build
  gates pass. The all-history/current-artifact security audit remains clean at
  7 refs, 198 reflog commits, 17 retained unreachable objects, 1369 Git blobs,
  and 433 current/artifact files.
- Next step: Advance every live firmware, macOS, Windows, installer, manifest,
  and documentation version surface to 1.2.0, then run the complete public
  release build and security gate.

## 2026-07-28 01:29 HKT

- Current work: Advanced firmware, macOS App/HAL/Agent, Windows Agent/build/
  installer, macOS installer validation, machine-readable manifest, README,
  user guide, Windows guide, and public-release policy to version 1.2.0.
- Expected result: Every live release surface agrees on 1.2.0 and documents
  the exact device-led onboarding flow, platform capability boundary,
  backtick behavior, generic-image security policy, and clean uninstall.
- Result: Achieved. Twenty-three focused version, driver bundle, macOS
  installer, and Windows packaging tests pass after rebuilding the driver
  artifact. The live release scope contains no remaining 1.1.8 or 1.0.31
  reference; historical validation documents were intentionally left
  unchanged.
- Next step: Enforce the final artifact allowlist, rebuild all public outputs
  from a clean generated state, verify blank public Wi-Fi NVS, generate the
  checksum manifest, and write the 1.2.0 release evidence report.

## 2026-07-28 01:42 HKT

- Current work: Rebuilt the complete generic public release from a clean
  generated state, enforced the final artifact allowlist, verified the erased
  Wi-Fi partition, generated checksums, and reran the all-history/current-
  artifact credential audit.
- Expected result: Version 1.2.0 has reproducible firmware and Agent packages,
  no credential-bearing public artifact, passing host/sanitizer/platform
  gates, and a reviewable release evidence report.
- Result: Achieved. Python gates passed 223 + 29 + 25 tests; normal and
  ASan/UBSan host suites passed 38/38 each; Swift, C17 audio and Go race gates
  passed. The application leaves 48% of its partition and 143,497 bytes of
  DIRAM free. The public `wifi_cfg` range is erased, all 11 top-level release
  entries match the allowlist, checksums verify, and the audit found 0 issues
  across 7 refs, 200 reflog commits, 17 unreachable objects, 1,400 Git blobs
  and 445 current/artifact files.
- Next step: Commit the release evidence, perform the final branch review and
  fast-forward integration, copy the verified public outputs into the main
  worktree, then purge the installed Mac Agent/driver and private local build
  outputs as the final destructive step.

## 2026-07-28 01:54 HKT

- Current work: Closed the final reproducibility gap exposed by repeated
  release-gate runs.
- Expected result: Rebuilding firmware and the Windows installer from the
  same source produces byte-identical public artifacts and leaves the checksum
  manifest unchanged.
- Result: Achieved. ESP-IDF reproducible-build mode removes compile timestamps,
  NSIS no longer embeds input-file modification times, and regression tests
  enforce both settings. Two independent firmware builds matched at the
  application and full-image levels; two independent NSIS builds matched in
  size and SHA-256. The complete release gate passed with 223 + 29 + 25 Python
  tests, 38/38 normal host tests, 38/38 ASan/UBSan host tests, Swift/C17/Go
  platform gates, blank Wi-Fi NVS, checksum validation and 0 audit findings.
- Next step: Commit the reproducibility fix and updated release evidence,
  then fast-forward the approved branch into `main`.

## 2026-07-28 02:00 HKT

- Current work: Fast-forwarded the approved feature branch into `main`,
  copied only the verified public outputs, removed the old local private
  release directories, and reran the entire release gate from the main
  worktree.
- Expected result: The integrated source and final deliverables pass the same
  release/security gates and the checksum manifest identifies the actual
  main-worktree artifacts.
- Result: Achieved. The integrated gate passed 223 + 29 + 25 Python tests,
  38/38 normal host tests, 38/38 ASan/UBSan tests, the ESP32-S3 build,
  Swift/C17/Go race gates, codesign checks, erased `wifi_cfg`, the 11-entry
  public allowlist and checksum verification. The final audit found 0 issues
  across 7 refs, 202 reflog commits, 18 unreachable objects, 1,423 Git blobs
  and 450 current/artifact files.
- Next step: Commit the main-worktree checksums and evidence, then run the
  authorized Mac Agent/HAL/AudioBridge purge and verify the machine is clean
  for new-device onboarding.
