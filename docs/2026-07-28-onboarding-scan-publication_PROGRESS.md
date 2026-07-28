# Cardputer Onboarding Scan and Public Publication Progress

## 2026-07-28 09:45 HKT

- Current work: Diagnosed the blank-device `SCANNING NETWORKS` reboot loop and
  designed the 1.2.1 stabilization, bilingual README, licensing, and GitHub
  publication boundary.
- Expected result: Identify a repeatable root cause before code changes and
  obtain approval for the smallest robust architecture.
- Result: Achieved. Serial reproduced an `xQueueGenericSend` assertion after
  every scan, and the flashed ELF resolved the backtrace to the default ESP
  event loop releasing its recursive mutex after the heavy scan/UI callback.
  The approved design defers scan processing to the existing `wifi-state`
  task and replaces dynamic scan containers with fixed capacity storage.
- Next step: Add failing dispatch and bounded-selection regression tests, then
  implement the deferred scan path.
