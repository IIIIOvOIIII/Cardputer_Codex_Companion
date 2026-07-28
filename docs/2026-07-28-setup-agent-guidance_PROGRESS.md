# SETUP Agent Guidance Progress

## 2026-07-28 11:13 HKT

- Current work: Inspected the SETUP renderer, onboarding Agent-step content,
  runtime IP/PIN sources, and macOS installer input validation; recorded the
  user-approved design.
- Expected result: Establish a minimal implementation boundary that changes
  only SETUP text size, makes Agent onboarding self-sufficient, and preserves
  stored configuration compatibility.
- Result: Achieved. The approved boundary targets firmware `1.2.2`, uses
  RFC1918 IPv4 input for interactive installation, and leaves normal page fonts
  unchanged.
- Next step: Write the TDD implementation plan, implement the focused changes,
  compile the firmware, and run release verification.

## 2026-07-28 11:29 HKT

- Current work: Completed TDD implementation, active version synchronization,
  ESP32-S3 build, and firmware packaging.
- Expected result: SETUP alone renders at `1x`; Agent onboarding shows IP and
  PIN while awaiting heartbeat; the macOS installer prompts for an RFC1918 IP;
  app-only and full `1.2.2` firmware images pass repository gates.
- Result: Achieved. Python focused gates passed with 40 tests, all 38 C++ host
  tests passed, Go Agent tests passed, and all 5 Windows packaging tests passed.
  ESP-IDF built app version `1.2.2` at `0x18c580` bytes with 48 percent of the
  smallest app partition free. DIRAM headroom is 135089 bytes. Public firmware
  verification confirmed that `wifi_cfg` is erased in the full image. SHA-256:
  app-only `e096bbf81cb877077b2151ad70ca182a6f39e29c03a7181ee981928d6343b47a`;
  full `883dde9aca6a182c7751fbc29b10b9e54383603e37a891b2ab69a51f4278461c`.
- Next step: Run the final verification suite, integrate the isolated branch
  into `main`, push it, and copy the verified firmware images to the stable
  project `dist/` paths.

## 2026-07-28 11:35 HKT

- Current work: Completed final verification and published the review branch.
- Expected result: Preserve a reviewable branch and PR while making the
  verified firmware available outside the temporary worktree.
- Result: Achieved. Final gates passed with 242 Python tests, 38 C++ host tests,
  and all Windows Agent Go packages. Branch
  `fix/setup-agent-guidance-1.2.2` was pushed and GitHub PR #1 was opened
  against `main`. Versioned app-only and full images were copied to the main
  project `dist/` directory and their SHA-256 values matched the build outputs.
- Next step: Review and merge PR #1, then flash the full image for first-run
  hardware validation.

## 2026-07-28 11:48 HKT

- Current work: Reproduced the reported root installer failure from the
  unmerged `main` checkout and added an end-to-end regression test for the
  packaged root `install.sh` entry.
- Expected result: Running `./install.sh install` must prompt for
  `Cardputer IP:`, normalize `192.168.1.195` to its HTTPS LAN URL internally,
  and never expose the old HTTPS URL prompt.
- Result: Achieved on the review branch. The new test failed on `main` with
  `Cardputer HTTPS URL:` and passed on
  `fix/setup-agent-guidance-1.2.2`; the focused root and macOS installer suite
  passed 20/20.
- Next step: Integrate the review branch into `main`, rerun the full release
  gate from the actual project root, rebuild the firmware artifacts, and push
  the integrated result.

## 2026-07-28 11:59 HKT

- Current work: Integrated the 1.2.2 review branch into `main`, rebuilt the
  macOS application and firmware, and ran the complete release gate from an
  isolated clean worktree.
- Expected result: The project-root `./install.sh install` accepts a bare
  RFC1918 IP, all 1.2.2 artifacts are internally consistent, and the public
  release gate passes without relying on stale 1.2.1 outputs.
- Result: Achieved. An isolated real install accepted `192.168.1.195` and
  reported `Installed Cardputer Companion 1.2.2.` The full gate passed 238
  primary Python tests, 31 installer/audio tests, 38 normal and 38 sanitizer
  firmware host tests, Windows Go and race tests, ESP-IDF firmware compilation,
  all 11 release hashes, and a zero-finding credential audit. The stale tracked
  `1.2.1-SHA256SUMS` release record was replaced by the generated 1.2.2 record.
  After rebuilding the main-worktree Windows binaries, the final integrated
  Python suite passed 243/243.
- Next step: Run the final integrated regression suite, commit and push
  `main`, and report the corrected install command and artifact paths.
