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
