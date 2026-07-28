# Launcher Wi-Fi Recovery Progress

## 2026-07-28 21:46 HKT

- Current work: Reproduced and decoded the `1.3.1l` post-Wi-Fi reboot loop on
  the attached Cardputer, then defined the Wi-Fi event isolation, deferred scan,
  and Backspace Y/N full Companion reset design.
- Expected result: Establish an evidence-backed implementation boundary that
  fixes both the reboot and E294 while preserving M5Launcher.
- Result: Achieved. Serial logs show `sys_evt` stack overflow immediately after
  got-IP, and address decoding resolves the application frame to the synchronous
  online notification in `wifi_manager.cpp`. The user approved the recommended
  design and selected full Companion data deletion.
- Next step: Complete written-spec review, create the implementation plan and
  isolated worktree, then write failing tests before production code.

## 2026-07-28 21:52 HKT

- Current work: Converted the approved written specification into a
  test-first implementation and hardware verification plan.
- Expected result: Provide an ordered execution path that fixes the root cause,
  builds both release variants, and prevents destructive recovery testing
  without explicit approval.
- Result: Achieved. The plan includes the event-callback contract, deferred scan
  tests, Backspace Y/N recovery tests, version alignment, dual builds, and
  attached-device serial gates.
- Next step: Create the isolated `fix/launcher-wifi-recovery-1.3.2` worktree and
  establish the test baseline.

## 2026-07-28 22:02 HKT

- Current work: Created the isolated implementation worktree and ran the full
  host baseline before changing production code.
- Expected result: Prove the starting branch is green and isolate regressions
  introduced by this fix.
- Result: Achieved. CMake built successfully and CTest passed 40/40 tests.
- Next step: Add failing Wi-Fi event-boundary, event-mailbox, and scan-retry
  tests, then implement the smallest change that makes them pass.

## 2026-07-28 22:03 HKT

- Current work: Implemented the deferred Wi-Fi event mailbox and scan scheduler,
  added the early Backspace Y/N recovery flow, aligned release versions, and
  built both release layouts.
- Expected result: Keep NVS, UI, web, and onboarding work out of the ESP system
  event task; tolerate transient scan state; and provide a Launcher-safe escape
  from persisted bad state.
- Result: Achieved in software gates. CTest passes 41/41, the focused Python
  contract suite passes except for a release-digest fixture that was updated to
  the initial build, with the final legacy-Launcher compatibility hash recorded
  in the next milestone,
  and Launcher 1.3.2l validates at 6,426,624 bytes with an erased storage
  boundary.
- Next step: Re-run the corrected release gates, then flash only the attached
  device's application partition so the saved Wi-Fi state remains available for
  the hardware regression test.

## 2026-07-28 22:32 HKT

- Current work: Completed the final software gates and state-preserving hardware
  regression on the user's legacy M5Launcher partition layout.
- Expected result: Reach BLE onboarding from the saved Wi-Fi state without E294
  or reset, preserve Launcher-owned data, and prove the Backspace N path locally.
- Result: Achieved. The attached table has `cardpu` at `0x1e0000` and no
  dedicated `assets` partition, so the Launcher application was written only at
  `0x1e0000`; the recovery backend now safely skips absent dedicated storage
  instead of erasing shared `spiffs`. Flash and independent verify matched.
  Firmware reached `192.168.1.195`, `/api/v1/setup` returned `step=ble_pair`,
  the Wi-Fi scan retried and published 12 networks, and serial stayed stable
  beyond 91 seconds with zero E294, stack overflow, panic, second boot, or
  allocation failure. The user held Backspace through a serial reset, observed
  the recovery prompt, pressed N, and the device resumed normally with its
  configuration retained. CTest passes 41/41, ASan/UBSan passes 41/41, and the
  complete product pytest suite passes 195/195.
- Next step: Run the consolidated product release script, record the final
  artifact hashes, then commit, fast-forward `main`, and push.

## 2026-07-28 22:51 HKT

- Current work: Completed the consolidated product release gate and finalized
  the 1.3.2/1.3.2l release artifacts.
- Expected result: Prove the source, firmware variants, installers, checksums,
  sanitizers, packaging, and public-release boundary are ready to commit.
- Result: Achieved. The release script passed 270 Python tests, 41/41 host
  tests in both normal and ASan/UBSan builds, 5/5 web tests, Go tests including
  the race detector, firmware memory/layout gates, all 15 packaged-artifact
  checksum checks, and the full current/history public security audit with zero
  findings. Final SHA-256 values are
  `d289cd62df253c2782e7a0fd952922a32bdcde95e4f4eddfb04ac4c7db7c27ce`
  for the 1.3.2 Factory image and
  `0b6c2ff575d493f735e1b8c9fd7dfac52ce2079a3740275c069cf4f6d97cc901`
  for the 1.3.2l Launcher image.
- Next step: Commit the reviewed change, fast-forward `main`, push the source,
  and materialize the verified firmware artifacts in the main checkout.
