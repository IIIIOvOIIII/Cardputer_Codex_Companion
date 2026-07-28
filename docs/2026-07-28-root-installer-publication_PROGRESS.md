# Root macOS Installer Publication Progress

## 2026-07-28 10:48 HKT

- Current work: Defined the public root-level macOS installer entry point and
  its source/release layout boundary.
- Expected result: One `./install.sh` interface for macOS install, status,
  uninstall, and purge without duplicating installer logic or changing the
  Windows path.
- Result: Achieved at design level. The selected context-aware wrapper keeps
  `mac_installer.py` authoritative and makes source builds lazy.
- Next step: Complete design review, write the executable implementation plan,
  then implement through failing wrapper and packaging tests.
