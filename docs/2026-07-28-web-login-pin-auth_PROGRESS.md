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
- Next step: Add failing tests for Launcher image packaging and
  the expanded public artifact set.

## 2026-07-28 15:10 HKT

- Current work: Implement and verify the factory and Launcher firmware
  packaging channels from separate builds of the same source tree.
- Expected result: Produce a Wi-Fi-clean factory `1.3.0` image and an exact
  `0x620000`-byte Launcher `1.3.0l` image with the required storage contract.
- Result: Achieved. Focused packaging tests passed 13/13. Real ESP32-S3 builds
  reported application versions `1.3.0` and `1.3.0l`; the factory artifact
  passed its erased Wi-Fi-range check, and the initial 6,422,528-byte Launcher
  artifact passed application, embedded partition, storage label/size, and
  erased Wi-Fi-range verification. This package shape was later superseded by
  the official Launcher HIL correction recorded below.
- Next step: Add a pure runtime storage-compatibility model and expose
  incompatible Launcher layouts on the Cardputer UI without blocking BLE,
  Wi-Fi, or Web startup.

## 2026-07-28 15:20 HKT

- Current work: Add runtime storage validation before profile and pet storage
  workers start, and expose failures on the Cardputer.
- Expected result: Classify ready, missing, wrong-type, and undersized storage;
  keep non-storage services running; never animate or retry storage workers on
  an incompatible layout.
- Result: Achieved. The new pure storage contract and UI/controller tests passed
  3/3, structural firmware regressions passed 25/25, and a complete ESP32-S3
  Factory build succeeded with 48% of the smallest application partition free.
  Incompatible storage now skips pet/profile workers, completes the startup
  gate, shows the concrete state on DEVICE, and displays reinstall guidance on
  the pet page.
- Next step: Publish the storage state through `/api/v1/status`, return
  structured HTTP 503 errors for storage-dependent APIs, and preserve those
  distinctions in the Web login client.

## 2026-07-28 15:30 HKT

- Current work: Separate Web authentication, reachability, service, and
  partition failures across firmware APIs and the embedded browser client.
- Expected result: Keep authenticated status readable on incompatible layouts,
  return a structured partition HTTP 503 for storage APIs, and never report
  that response as a bad PIN.
- Result: Achieved. Firmware Web JSON tests passed 1/1, browser request/error
  tests passed 5/5, Web asset tests passed 11/11, and the regenerated embedded
  page passed deterministic asset checks. All profile and pet handlers now use
  the shared storage guard, `/api/v1/status` includes storage state and size,
  and a complete ESP32-S3 build succeeded with 48% application-partition
  headroom.
- Next step: Build the project-owned Web Serial Factory installer and
  deterministic GitHub Pages payload.

## 2026-07-28 15:40 HKT

- Current work: Add the project-owned Factory Web Serial installer, its
  deterministic release archive, and the GitHub Pages publication boundary.
- Expected result: Install Factory 1.3.0 at offset zero from a pinned Web
  Serial component, warn about all reset effects, retain a visible
  Launcher-compatible path, and publish no unrelated repository files.
- Result: Achieved. The manifest, installer page, deterministic two-file ZIP
  packager, Pages workflow, release metadata, and public allowlist passed 10/10
  focused tests. Repeated packages were byte-identical, both entries had mode
  0644 and the requested epoch, and both JSON documents parsed successfully.
- Next step: Update the bilingual documentation and full release gate for the
  two installation channels and complete artifact production.

## 2026-07-28 15:50 HKT

- Current work: Complete the bilingual dual-channel operator documentation
  and run the clean non-hardware release gate for 1.3.0/1.3.0l.
- Expected result: Pass Python, C++ host/sanitizer, Node, Swift, Go/race,
  ESP32-S3, signing, deterministic packaging, checksum, allowlist, and
  all-history credential gates; produce every approved release artifact.
- Result: Achieved. Python passed 257/257 plus 38/38 focused product tests;
  normal and ASan/UBSan host suites each passed 40/40; Node passed 5/5; final
  packaging/installer checks passed 34/34; Swift product suites, Go and
  Go-race passed. Factory reports runtime 1.3.0 with 135,057 bytes DIRAM
  headroom, and the initial Launcher package reported runtime 1.3.0l with size
  6,422,528 bytes and a compatible storage declaration. This package shape was
  later superseded by the official Launcher HIL correction recorded below.
  Fourteen checksum entries
  verified, and the exact public allowlist accepted 15 top-level artifacts.
  The security audit enumerated 10 refs, 244 reflog commits, 20 retained
  unreachable objects, 1,643 Git blobs, and 488 current/artifact files with
  zero findings. Key SHA-256 values are Factory
  `2477fdfb9e29ac31e804d5d32de8994afcda42a355bc322d837571f9d805b692`,
  app-only
  `d536f2298b08cb930e503dcfae7a62dd96ac72e8628aa1e987679983bfd670ae`,
  Launcher
  `bd74b976fada8824bc0ca33b05195c9dedfe3c9fd39c2bc6d3ae4ee41fe44269`,
  and Web installer
  `b012acf72757371486f914d73a5cf0f836d93b0486e94054c5ae99366ace7359`.
- Next step: Commit the software-gate evidence, back up the exact attached
  ESP32-S3, then execute Launcher and Factory hardware paths.

## 2026-07-28 16:05 HKT

- Current work: Back up the attached ESP32-S3 and validate the real official
  M5Launcher dynamic-install partition behavior.
- Expected result: Preserve recoverable device state and install `1.3.0l`
  without replacing Launcher.
- Result: Partial, then corrected. The exact device backup was stored under
  `~/Library/Application Support/CardputerCodexCompanion/device-backups/`
  as `30eda0c880a4-pre-1.3.0-20260728T1518HKT`, with directory mode 0700 and
  file mode 0600. NVS SHA-256 is
  `477ab041b89d623135261e9da576c6b92d737b3af8483dae95ba5591cf60a1f1`;
  partition-table SHA-256 is
  `86b614c316870c2e147c5644b5855a918388182b91260bab1927241d53773adb`;
  storage SHA-256 is
  `6b3bdf3946c4761700be5210c4a8df6a8a454f1d9592b9413e3f417b83059ad0`;
  and Wi-Fi configuration SHA-256 is
  `1df8949b2e345ab8c00cb81fb6b83686e20a4080f969e5cd8b8d520a07cdaba2`.
  Official Launcher creates a dynamic partition only when the artifact
  carries payload in that region, and its resulting label is `assets`.
- Next step: Add a Launcher-only `assets` partition contract and an erased
  4 KiB payload sector, then repeat the hardware installation.

## 2026-07-28 16:25 HKT

- Current work: Exercise Web status, Profile list/get/create/update/activate,
  pet synchronization, BLE, and Agent heartbeat on the Launcher-installed
  firmware under concurrent TLS activity.
- Expected result: Keep Launcher, report runtime `1.3.0l`, expose ready
  1,920 KiB storage, and persist custom text and HID bindings across reset.
- Result: Achieved after resource fixes. The Launcher artifact is now
  `0x621000` bytes: application data ends before `0x620000`, followed by one
  erased 4 KiB partition-creation sector. Runtime reported `1.3.0l`, BLE,
  Wi-Fi and Agent were OK, storage was READY at 1,966,080 bytes, unauthenticated
  status returned HTTP 401, and all storage APIs returned HTTP 200. A custom
  UTF-8 text binding and Alt+V HID chord survived hard reset. Root causes of
  the observed Profile reboots were a full cJSON tree for 224 bindings,
  20 KiB Profile temporaries and 4 KiB catalog chunks on the 4.8 KiB HTTPS
  stack, and throwing heap allocation during TLS fragmentation. Encoding is
  now a two-pass single-allocation serializer; constrained runtime allocations
  are nothrow and return HTTP 503; catalog verification reuses the caller
  Profile and heap chunk.
- Next step: Validate the clean and configured Factory installation paths.

## 2026-07-28 16:45 HKT

- Current work: Validate Factory first boot after full erase, then temporarily
  restore the backed-up NVS and Wi-Fi partitions to exercise the configured
  runtime and Machine Agent.
- Expected result: Prove the setup Wi-Fi scan no longer reboots and the same
  source works with the official Factory storage label.
- Result: Achieved. The clean Factory boot reported version `1.3.0`, completed
  one Wi-Fi scan, then ran for 45 seconds with one expected serial-open reset,
  zero panic/assert markers, stable approximately 68 KiB free internal heap,
  and monotonically increasing uptime. The configured pass reported BLE,
  Wi-Fi, and Agent OK, `storage` READY at 1,966,080 bytes, HTTP 401 without a
  PIN, and SAFE Profile revision 1 with 224 bindings.
- Next step: Run the final clean release gate, publish the branch, then erase
  and flash its exact Factory artifact so the device is left at first-run
  setup.

## 2026-07-28 16:46 HKT

- Current work: Apply the user's instruction to skip the remaining Launcher
  extended observation.
- Expected result: Preserve the completed eight stable rounds as evidence and
  continue without waiting for the planned ten-minute soak.
- Result: Achieved. Eight consecutive status/Profile/pet rounds returned HTTP
  200, BLE stayed OK, Agent became OK from round two onward, and its PID did
  not change. The omitted extended soak will not be represented as completed.
- Next step: Complete automated release gates and public release publication.

## 2026-07-28 16:52 HKT

- Current work: Run the final clean dual-release gate after all Launcher HIL,
  Profile resource, encoding, and documentation corrections.
- Expected result: Rebuild every public artifact from source and pass all
  platform, sanitizer, packaging, checksum, allowlist, and credential-history
  controls.
- Result: Achieved. Python passed 259/259 plus 38/38 audio-focused tests;
  normal and ASan/UBSan firmware host suites each passed 40/40; Node passed
  5/5; final installer/package checks passed 34/34; Swift product suites and
  Go plus Go race passed. Factory retained 135,057 bytes DIRAM headroom.
  Fourteen published checksum entries verified and the 15-entry public
  allowlist passed. The security audit enumerated 10 refs, 245 reflog commits,
  20 retained unreachable objects, 1,653 Git blobs, and 488 current/artifact
  files with zero findings. Final SHA-256 values are Factory
  `f9ece7fefc9c4f26452547c326a8000908db1bf9776dc55075a60cc5856b1f61`,
  app-only
  `eebf8109cec0c6cd46b42326db831e32fd3b1f1176ba6a42379697f7c8eea37b`,
  Launcher
  `7365785aa092fad1e88d0c83d1b575c6dc2c78dbcab7baa8587c52063114662b`,
  and Web installer
  `b012acf72757371486f914d73a5cf0f836d93b0486e94054c5ae99366ace7359`.
- Next step: Commit and integrate the verified source, publish v1.3.0 and
  GitHub Pages, then leave the attached device on the exact clean Factory
  artifact.
