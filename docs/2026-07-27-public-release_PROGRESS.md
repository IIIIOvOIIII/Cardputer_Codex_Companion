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
