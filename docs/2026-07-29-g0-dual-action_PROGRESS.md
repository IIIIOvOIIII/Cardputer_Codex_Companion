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

## 2026-07-29 23:28 HKT

- Current work: Unify every active firmware, Agent, installer, manifest, Web Installer, documentation, and release-test version surface.
- Expected result: Factory and both Agents identify as 1.3.4; Launcher identifies as 1.3.4l; active release paths contain no 1.3.3 reference.
- Result: Achieved. The version tests were first observed failing against stale 1.3.3 surfaces. All active references now use 1.3.4/1.3.4l; source-level release/Web tests pass 10/10 and Windows packaging source tests pass 3/3. The old Factory digest remains intentionally temporary until the clean Task 7 build replaces it.
- Next step: Run the complete release build/gates, replace the manifest digest, flash only the attached device's application slot, and execute the G0 HIL gate.

## 2026-07-30 00:04 HKT

- Current work: Build, flash, and validate the 1.3.4/1.3.4l release on the attached M5Launcher Cardputer without altering persisted configuration.
- Expected result: The Launcher app fits the real `cardpu` partition, boots without reset, passes enabled and disabled G0 HIL, persists settings across reboot, and executes a physical G0 short press through the chord-before-Mic path.
- Result: Achieved after one fail-closed correction. The first 1,640,048-byte Launcher app exceeded the installed M5Launcher `cardpu` partition (`0x190000`, 1,638,400 bytes) by 1,648 bytes and the bootloader rejected it. A new release gate now enforces that exact upper bound, and Launcher-only `-Os` on the product component reduced the app to 1,610,448 bytes while leaving Factory compilation unchanged. Flash write and readback verification passed at `0x170000`; the device then ran beyond 33 seconds with zero allocation, reset, or HID queue failures. Enabled HIL returned `QUEUED`, completed the dual action, and transitioned Mic from READY to LIVE16. Disabled HIL returned `MIC_ONLY` without a dual-action completion. The enabled setting survived a hardware reboot and was restored afterward. A user-triggered physical G0 press produced `g0 dual action queued` followed 30 ms later by `completed result=1`; Mic and the original disabled G0 configuration were restored after the gate.
- Next step: Commit the hardware compatibility correction, rerun the complete release gate from the final source state, then merge and publish GitHub Release v1.3.4 plus Pages.

## 2026-07-30 00:09 HKT

- Current work: Run the complete release gate against the committed 1.3.4 source and final artifacts.
- Expected result: Every firmware, Agent, installer, signing, checksum, memory, and security gate passes before any public tag or Release is created.
- Result: Achieved. The gate passed 292 product Python tests, the audio/installer suite, 42 normal and 42 sanitizer firmware host tests, Web/Node, Swift, C, Go and Go race checks, Factory/Launcher verification, Mac signing, Windows/macOS/Web packaging, all 15 published checksums, and the complete public-artifact allowlist. Factory SHA-256 is `e42fbdaa9e6eb626be6c2814ddb3b52d0f3b0c87bc639142ed77cefa46176ca2`; Launcher SHA-256 is `21b3fcc4c9aaeb20529e38cc39d22b4695902ac59e117d2d26eea3340fc9ccb3`. The credential/history audit scanned 20 refs, 284 reflog commits, 21 retained unreachable objects, 1,957 Git blobs, and 525 current/artifact files with zero findings.
- Next step: Fast-forward `main`, push it, create annotated `v1.3.4`, publish all release assets, and deploy the fail-closed Pages Web Installer.

## 2026-07-30 00:29 HKT

- Current work: Publish and independently verify GitHub Release `v1.3.4` and the fail-closed GitHub Pages Web Installer.
- Expected result: The annotated remote tag resolves to the hardware-verified commit; all nine public assets match their local SHA-256 digests; Pages deploys from that commit and serves the pinned Factory image.
- Result: Achieved. Remote `v1.3.4` resolves to `ee1053d04bbb04a9b0ddf7c4f7085e0d60af0e15`. The non-draft, non-prerelease GitHub Release exposes nine assets and every GitHub asset digest matches its local file. Pages run `30470692017` completed successfully from the same commit. The live manifest reports `1.3.4`, and the downloaded 1,771,120-byte Factory image has SHA-256 `e42fbdaa9e6eb626be6c2814ddb3b52d0f3b0c87bc639142ed77cefa46176ca2`.
- Next step: Commit and push the publication closeout documentation, record the operational result, and run the final source/release/live-state verification.
