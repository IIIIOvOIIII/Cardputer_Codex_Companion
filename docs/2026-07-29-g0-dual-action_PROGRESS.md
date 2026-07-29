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

## 2026-07-29 23:02 HKT

- Current work: Add authenticated G0 chord GET/PUT endpoints without creating a second persistence owner.
- Expected result: Both routes require the device PIN, validate the single-key chord schema, distinguish invalid input from NVS failure, and apply only through controller callbacks.
- Result: Achieved. RED host/API-contract tests failed on missing types and handlers, then passed after adding the 20-route contract, bounded cJSON validation, controller getter/apply callbacks, and the dedicated settings mutex. Normal and ASan/UBSan host suites pass 42/42, API contracts pass 4/4, and a real ESP-IDF 5.5.4 target build completed with app version 1.3.4.
- Next step: Add the G0 dual-action card to Web Settings and regenerate embedded assets.

## 2026-07-29 23:19 HKT

- Current work: Add the optional G0 chord controls to the authenticated Web Settings page and regenerate the embedded firmware assets.
- Expected result: Users can capture one HID chord, explicitly enable or disable it, preserve the captured chord while disabled, and receive an in-page save result.
- Result: Achieved. The Web UI now loads and saves `/api/v1/settings/g0-chord`, keeps G0 state separate from Profile key editing, and uses the existing result dialog. The focused Python suite passes 12/12, Node passes 7/7, generated assets are current, and the ESP-IDF target build succeeds at `0x1905b0` bytes.
- Next step: Add a deterministic serial HIL path for G0 and prove ordering/fallback behavior on the attached Cardputer.

## 2026-07-29 23:25 HKT

- Current work: Add a repeatable serial G0 command and a metrics-only hardware proof runner.
- Expected result: `HIL G0 CLICK` enters the exact physical short-press dispatcher; the runner configures a test chord without exposing the PIN, proves queue/completion and a Mic transition, detects resets/queue failures, restores prior settings, and records no HID or audio content.
- Result: Achieved for the implementation gate. LF/CRLF/split/rejection parser cases pass; the HIL runner uses a mode-0600 transient curl config and emits only bounded metrics. Normal and ASan/UBSan host suites pass 42/42, focused Python tests pass 8/8, and the ESP-IDF target build succeeds at `0x190670` bytes.
- Next step: Unify all active product, Agent, installer, manifest, and documentation version surfaces as 1.3.4/1.3.4l.
