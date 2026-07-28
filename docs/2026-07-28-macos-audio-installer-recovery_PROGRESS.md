# macOS Audio Installer Recovery Progress

## 2026-07-28 13:25 HKT

- Current work: Diagnose the failed clean macOS install, incomplete unload, and
  high-CPU Core Audio process from live state and unified logs.
- Expected result: Identify whether the HAL failed to enumerate or the
  installer misclassified a successful load.
- Result: Achieved. Logs prove the Cardputer input device was activated and
  selected by macOS. `system_profiler` became uninitialized during the restart,
  timed out, triggered a false rollback, and left the old loaded device without
  its bridge. The old cleanup path did not verify `coreaudiod` PID turnover.
- Next step: Add failing regression tests for lightweight enumeration, exact
  password prompts, verified Core Audio restart, and stale-device uninstall.

## 2026-07-28 13:31 HKT

- Current work: Implement the 1.2.3 macOS audio installer recovery and package
  the matching Companion probe.
- Expected result: Replace the unstable `system_profiler` check, make Core
  Audio restarts observable, remove stale loaded devices, and explain each
  administrator-password request.
- Result: Achieved. The installer now uses the Companion
  `audio-device-status` command, treats probe failures as unknown, verifies
  `coreaudiod` PID turnover, handles stale devices even when files are already
  absent, and uses operation-specific `sudo` prompts.
- Next step: Build all 1.2.3 release artifacts and run the strict product
  release gate.

## 2026-07-28 13:41 HKT

- Current work: Validate the complete 1.2.3 public release and the recovered
  local Core Audio state.
- Expected result: All firmware, host, macOS, Windows, packaging, checksum, and
  public-security gates pass; no stale profiler process remains; `coreaudiod`
  returns to normal CPU usage.
- Result: Achieved. `scripts/verify_product_release.sh` passed, including 245
  Python tests, 38 audio tests, 38 normal and 38 sanitized host firmware tests,
  all product Swift/C audio tests, Go and Windows packaging checks, public
  firmware validation, exact artifact allowlisting, checksums, and a
  zero-finding credentials audit. The live machine is clean, has no
  `system_profiler` process, and `coreaudiod` PID 418 measured 0.0% CPU.
- Next step: Commit and push the reviewed 1.2.3 source, documentation, release
  manifest, tests, and checksum manifest.
