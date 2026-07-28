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

## 2026-07-28 11:03 HKT

- Current work: Implemented and packaged the root macOS entry, updated both
  READMEs, and executed the complete 1.2.1 public-release gate.
- Expected result: The source and packaged layouts expose an identical
  `install.sh`; source builds are lazy; status and uninstall never build; all
  existing release and credential gates continue to pass.
- Result: Achieved. The focused installer/package regression completed with
  41 tests passing. The complete gate passed Python 235/235, audio Python
  29/29, normal and sanitizer host 38/38 each, ESP-IDF, Swift/C, Windows
  Go/race, signing, 11 checksum, and public-artifact checks. The credential
  audit scanned 8 refs, 218 reflog commits, 18 unreachable objects, 1488 Git
  blobs, and 462 current/artifact files with zero findings. The first isolated
  run correctly exposed a missing ignored `.tools` link; matching the existing
  worktree toolchain layout resolved the environmental CMake failure.
- Next step: Commit the installer checksum and evidence, integrate the feature
  branch into `main`, copy the verified ignored package entry into the main
  release directory, then push and prove remote commit identity.
