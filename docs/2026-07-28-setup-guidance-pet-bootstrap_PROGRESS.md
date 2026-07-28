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
