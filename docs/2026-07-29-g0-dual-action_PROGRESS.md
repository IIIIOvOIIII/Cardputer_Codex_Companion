# G0 Dual-Action Progress

## 2026-07-29 22:35 HKT

- Current work: Confirm the architecture and release boundary for an optional device-global G0 chord followed by the existing microphone toggle.
- Expected result: A backward-compatible design with an explicit execution order, privacy-preserving failure behavior, Web configuration, persistence, HIL coverage, and unified 1.3.4 release scope.
- Result: Achieved. The user approved device-global scope, default disabled behavior, strict chord-before-Mic ordering, and Mic fallback when HID cannot run. The written design uses the unused CRC-covered bytes in the existing settings record and keeps the 56-key Profile schema unchanged.
- Next step: Commit the reviewed design artifact and request written-spec approval before creating the implementation plan.

## 2026-07-29 22:58 HKT

- Current work: Translate the approved G0 dual-action design into a test-driven implementation, hardware-validation, and public-release plan.
- Expected result: A file-specific plan that preserves old settings, proves exact HID-before-Mic ordering, includes queue/HID fallback behavior, exercises the attached Cardputer, and publishes every active surface as 1.3.4/1.3.4l.
- Result: Achieved. The implementation plan contains eight bounded tasks covering persistence, runtime dispatch, authenticated Web API, Web UI/assets, serial HIL, version unification, release builds/HIL, and fail-closed GitHub Release/Pages publication.
- Next step: Review the plan for placeholders and ambiguous verification, commit it, then choose the execution mode.

## 2026-07-29 22:58 HKT

- Current work: Implement the backward-compatible persistent G0 chord fields using test-driven development.
- Expected result: Old 12-byte settings records decode with G0 disabled; valid enabled/retained chords round-trip; invalid masks/usages and failed commits cannot alter active settings.
- Result: Achieved. The focused test was observed failing on the missing fields, then passed in normal and ASan/UBSan builds. Bytes 6/7 now carry usage and enabled/modifier flags under the existing CRC while schema version and record length remain unchanged.
- Next step: Add the deterministic macro-task G0 dual-action executor and queue fallback.

## 2026-07-29 23:00 HKT

- Current work: Route an enabled G0 short press through the macro task while preserving the privacy-critical Mic fallback.
- Expected result: Disabled G0 toggles only Mic; enabled G0 executes the HID chord before Mic; failed chord or full macro queue still toggles Mic exactly once; long press remains ignored.
- Result: Achieved. RED tests first failed on the missing executor and controller dispatch contract. The new typed macro invocation snapshots the stored chord, executes it through `MacroEngine` and the dedicated keyboard-HID path, then enqueues the Mic click; queue failure falls back directly. Normal and ASan/UBSan host suites pass 42/42 and the HID concurrency contract passes 3/3.
- Next step: Add the paired GET/PUT G0 Settings API and controller callbacks.
