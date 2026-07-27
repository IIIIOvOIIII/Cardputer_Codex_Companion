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
