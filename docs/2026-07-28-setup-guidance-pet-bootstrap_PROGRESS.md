# SETUP Guidance and Pet Bootstrap Progress

## 2026-07-28 19:08 HKT

- Current work: Audited the existing onboarding state machine, SETUP renderer,
  authenticated heartbeat transition, Mac and Windows Pet cadence, release
  process, and attached-device deployment boundary; recorded the approved
  design.
- Expected result: Define a backward-compatible transient completion guide,
  actionable BLE/Agent copy, immediate `needs_snapshot` Pet synchronization,
  and a complete 1.3.1/1.3.1l publication boundary before implementation.
- Result: Achieved. The user selected a transient completion guide that is
  skipped after reboot and approved a complete public 1.3.1 release. The design
  preserves onboarding record schema 1, consumes the acknowledgement key
  locally, keeps the existing cadence, and forces only the missing Pet attempt
  when a device requests a snapshot.
- Next step: Review and commit the design, obtain written-spec approval, then
  create the TDD implementation plan.

## 2026-07-28 19:32 HKT

- Current work: Converted the approved written design into a task-by-task TDD
  implementation and deployment plan.
- Expected result: Every firmware, macOS, Windows, versioning, hardware, and
  public-release change has an explicit RED/GREEN command, interface boundary,
  commit point, and acceptance check.
- Result: Achieved. The plan contains five independently reviewable tasks and
  preserves the transient completion decision, same-loop Pet de-duplication,
  stored schema compatibility, 1.3.1/1.3.1l version boundary, and complete
  release/HIL gate.
- Next step: Execute the plan inline, beginning with failing firmware
  onboarding tests.

## 2026-07-28 19:50 HKT

- Current work: Completed the firmware SETUP copy/transient completion state,
  macOS forced Pet bootstrap, Windows forced Pet bootstrap, and updated the
  release contract expectations to 1.3.1/1.3.1l.
- Expected result: Each behavior is demonstrated RED before implementation,
  then passes its focused host, Swift executable, packaging, Go, and race
  tests; version sources and tests agree before the full release build.
- Result: Achieved. Firmware host tests pass 40/40, macOS Pet and packaging
  tests pass, Windows tests and race tests pass, and the version-focused
  firmware/Python checks are green. The unsupported aggregate `swift test`
  path still cannot import XCTest in this local Swift toolchain; the
  repository's executable Swift gates pass.
- Next step: Run the complete release gate, replace the provisional Factory
  digest with the actual 1.3.1 artifact hash, then perform attached-device HIL
  verification.

## 2026-07-28 20:42 HKT

- Current work: Completed the full 1.3.1/1.3.1l release gate, deployed the
  application image to the attached Cardputer without clearing user state,
  updated the installed per-user macOS Agent, and exercised the Pet bootstrap
  and firmware stability paths.
- Expected result: All release artifacts pass the cross-platform gate and
  credential audit; the device runs 1.3.1 without rebooting; a device snapshot
  request triggers a Pet transaction before the ordinary 30-second cadence.
- Result: Achieved. The complete release gate passed, all 14 checksums matched,
  the credential audit found zero findings, esptool independently verified the
  application partition, the setup API reported 1.3.1, Pet transaction 304
  occurred at 24 seconds with more than six seconds of cadence remaining, and
  the 30-second serial sample had one expected reset with no firmware fault or
  second reboot.
- Next step: Commit the validation evidence, fast-forward `main`, rerun the
  final merged-tree checks, publish tag and GitHub Release, deploy Pages, and
  verify the same-origin Web Installer manifest and Factory image.

## 2026-07-28 20:50 HKT

- Current work: Published the verified 1.3.1/1.3.1l source and artifacts,
  reran the Pages deployment after the release became available, and verified
  every public installation surface.
- Expected result: Public `main`, annotated `v1.3.1`, the GitHub Release, and
  the same-origin Web Serial installer all expose the approved release and
  exact Factory digest.
- Result: Achieved. Public `main` and the tag resolve to
  `566f908815cf4daf44d9108dfcb004340600d24e`; the Release contains nine
  verified assets; Pages run `30358658105` attempt 2 succeeded; and the live
  page, manifest, and Factory image return HTTP 200. The live manifest reports
  1.3.1 and the 1,761,568-byte Factory image hashes to
  `d160bd57953650f516eb727bf087895ded595570288a569844078471f90b0a7c`.
  The public release guidance was also aligned from 1.3.0 to 1.3.1.
- Next step: Run the focused post-publication verification, commit and push
  this closeout evidence, then hand off the release and installer URLs.
