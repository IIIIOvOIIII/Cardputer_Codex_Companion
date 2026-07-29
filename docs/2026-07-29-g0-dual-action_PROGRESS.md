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
