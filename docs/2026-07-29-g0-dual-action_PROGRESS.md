# G0 Dual-Action Progress

## 2026-07-29 22:35 HKT

- Current work: Confirm the architecture and release boundary for an optional device-global G0 chord followed by the existing microphone toggle.
- Expected result: A backward-compatible design with an explicit execution order, privacy-preserving failure behavior, Web configuration, persistence, HIL coverage, and unified 1.3.4 release scope.
- Result: Achieved. The user approved device-global scope, default disabled behavior, strict chord-before-Mic ordering, and Mic fallback when HID cannot run. The written design uses the unused CRC-covered bytes in the existing settings record and keeps the 56-key Profile schema unchanged.
- Next step: Commit the reviewed design artifact and request written-spec approval before creating the implementation plan.
