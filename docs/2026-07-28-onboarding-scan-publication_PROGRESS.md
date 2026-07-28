# Cardputer Onboarding Scan and Public Publication Progress

## 2026-07-28 09:45 HKT

- Current work: Diagnosed the blank-device `SCANNING NETWORKS` reboot loop and
  designed the 1.2.1 stabilization, bilingual README, licensing, and GitHub
  publication boundary.
- Expected result: Identify a repeatable root cause before code changes and
  obtain approval for the smallest robust architecture.
- Result: Achieved. Serial reproduced an `xQueueGenericSend` assertion after
  every scan, and the flashed ELF resolved the backtrace to the default ESP
  event loop releasing its recursive mutex after the heavy scan/UI callback.
  The approved design defers scan processing to the existing `wifi-state`
  task and replaces dynamic scan containers with fixed capacity storage.
- Next step: Add failing dispatch and bounded-selection regression tests, then
  implement the deferred scan path.

## 2026-07-28 10:05 HKT

- Current work: Implemented the deferred scan dispatch, fixed-capacity
  selection, 1.2.1 version surfaces, English-first bilingual documentation,
  Apache-2.0 license, and public Agent guidance.
- Expected result: Keep scan processing out of the default event loop, publish
  at most 12 deterministic networks, and align every live release surface.
- Result: Achieved for source and focused tests. Dispatch and selection
  regression tests passed, the target firmware built, and the README renders
  the public author as `Lynx (hi@iam.lc)`.
- Next step: Flash the attached blank device and complete the serial stability
  gate.

## 2026-07-28 10:28 HKT

- Current work: Recovered the attached device to the product partition layout,
  measured the scan worker, and completed the final 70-second blank-device
  stability gate.
- Expected result: Complete one Wi-Fi scan on firmware 1.2.1 without assertion,
  stack overflow, panic, or reboot, while retaining at least 1 KiB free worker
  stack.
- Result: Achieved. The prior factory partition table first required a full
  image at `0x0`; the product bootloader then loaded `ota_0` at `0x20000`.
  A 4096-byte worker removed the reboot but retained only 848 bytes, so the
  measured budget was set to 4608 bytes. Final observation recorded one boot,
  one `raw=22 published=12` scan, zero failures or reboots, 1360 bytes minimum
  worker stack, and 67,756 bytes minimum internal heap.
- Next step: Run the complete cross-platform release/security gate, regenerate
  checksums, commit the evidence, integrate to `main`, and push GitHub through
  the requested SSH key.

## 2026-07-28 10:34 HKT

- Current work: Executed the complete public-release gate and regenerated all
  1.2.1 firmware, macOS Agent, Windows Agent, installer, and checksum artifacts.
- Expected result: All platform tests, clean builds, signing, packaging,
  allowlisting, checksums and credential audits pass before publication.
- Result: Achieved. Python 228/228, audio-specific Python 29/29, normal host
  38/38, sanitizer host 38/38, ESP-IDF clean build, 135,089-byte DIRAM
  headroom, Swift/C tests, Go/race tests, codesign and all 11 release checksums
  passed. The audit inspected 7 refs, 210 reflog commits, 18 retained
  unreachable objects, 1471 Git blobs and 455 current/artifact files with zero
  findings.
- Next step: Commit the final README/evidence/checksum state, fast-forward
  `main`, configure the requested GitHub remote, push through
  `id_co_openclaw`, and verify the remote commit ID.

## 2026-07-28 10:37 HKT

- Current work: Published the verified 1.2.1 release source and artifacts to
  the requested GitHub repository.
- Expected result: Push `main` with the requested SSH identity and prove the
  remote branch matches the locally verified commit.
- Result: Achieved. GitHub accepted `main`, and `refs/heads/main` matched local
  commit `5a95044f216e40560c8e4d2f4ed68a910cef4765`.
- Next step: Commit and publish this closeout record, recheck the final remote
  commit and release checksums, then remove the completed feature worktree.

## 2026-07-28 17:06 HKT

- Current work: Integrated the approved 1.3.0 Factory and 1.3.0l Launcher
  release, reran the complete public-release gate from merged `main`, and
  returned the attached Cardputer to a credential-free Factory image.
- Expected result: Publish one source state with compatible Factory and
  M5Launcher artifacts, no retained public credentials, and deterministic
  device-write evidence.
- Result: Achieved, with the user-requested extended Launcher observation
  skipped. The merged gate passed 259 Python tests, 38 focused audio tests,
  40 normal and 40 sanitizer host tests, Node, Swift, Go/race, 34 final
  installer tests, 14 checksums, a 15-entry artifact allowlist, and an
  all-history audit covering 11 refs, 247 reflog commits, 20 retained
  unreachable objects, 1,678 Git blobs, and 491 current/artifact files with
  zero findings. Launcher HIL previously passed profile create/update/activate,
  UTF-8 text, Alt+V HID, hard-reset persistence, and eight stress rounds.
  The final Factory image was written after a chip erase and accepted by the
  ESP32-S3 ROM hash check. A post-boot full-image readback differed only in the
  expected mutable NVS/OTA range `0x9000..0xf01f`; all remaining image bytes
  matched.
- Next step: Publish this closeout commit, tag `v1.3.0`, attach the verified
  artifacts to the public GitHub release, verify GitHub Pages, and remove the
  completed feature worktree.

## 2026-07-28 17:32 HKT

- Current work: Completed the public GitHub publication and verified every
  externally referenced installation surface.
- Expected result: `main`, `v1.3.0`, the GitHub Release, and the Web Serial
  installer all expose the approved 1.3.0/1.3.0l artifacts.
- Result: Achieved. The annotated `v1.3.0` tag resolves to release source
  commit `c17d1af01efc40a9a54c4f7ce3abe68465845cd4`. The public Release contains
  nine assets whose GitHub-reported SHA-256 digests match the local verified
  artifacts. The first Pages attempt failed because the new public repository
  had no Pages site; enabling the GitHub Actions source and rerunning the same
  job succeeded. The public installer returns HTTP 200, reports version 1.3.0,
  and references the published Factory asset.
- Next step: Remove the merged feature worktree, confirm the final clean Git
  state and unloaded local Agent, and hand off the public URLs and artifact
  paths.

## 2026-07-28 17:35 HKT

- Current work: Corrected the Web Serial installer URL in both public READMEs.
- Expected result: The documented installer address resolves to the deployed
  GitHub Pages site.
- Result: Achieved. The previous hostname contained one extra `o`; both
  READMEs now use `iiiiovoiiii.github.io`. The corrected installer returned
  HTTP 200 and its manifest resolved version 1.3.0 to the published Factory
  release asset.
- Next step: Publish the documentation correction and complete the final
  repository/status verification.

## 2026-07-28 18:38 HKT

- Current work: Diagnosed the public Web Serial `Failed to fetch` error and
  rebuilt the installer publication path around a verified same-origin
  Factory image.
- Expected result: The Pages site serves the 1.3.0 Factory binary beside its
  manifest, refuses a mismatched release digest, and keeps the downloadable
  web-installer ZIP self-contained.
- Result: Achieved locally. The GitHub Release redirect omitted CORS response
  headers, so browser firmware fetches failed even though the release asset
  itself returned HTTP 200. The manifest now uses a relative Factory path;
  Pages staging pins SHA-256
  `173d8331576739210c724407ecd5b8e957866efd9e7779e02ba6106dc304bb22`;
  the focused suite passed 17/17; and the complete release gate passed 262
  Python tests, 38 audio tests, 40 normal and 40 sanitizer host tests, all
  platform builds and 14 checksums, plus a zero-finding credential audit.
  Factory and Launcher hashes remained unchanged.
- Next step: Commit and push the fix, wait for the Pages deployment, verify the
  public same-origin firmware hash, and replace the affected GitHub Release
  installer/checksum assets.

## 2026-07-28 18:49 HKT

- Current work: Published and externally verified the Web Serial delivery
  repair, then replaced the two affected v1.3.0 Release attachments.
- Expected result: A browser can fetch the full Factory image from the Pages
  origin, and GitHub Release exposes the matching self-contained offline
  installer and checksum list.
- Result: Achieved. Commit `5066c1d` reached public `main`; Pages workflow run
  `30351615333` completed successfully. The live manifest uses the relative
  Factory path, the Pages binary returns HTTP 200 with
  `Access-Control-Allow-Origin: *`, and its 1,760,560 bytes hash to
  `173d8331576739210c724407ecd5b8e957866efd9e7779e02ba6106dc304bb22`.
  The live page presents the 1.3.0 install control with no initial console
  warnings or errors. GitHub now reports the replacement web-installer ZIP at
  SHA-256
  `35a47e8b07bc701f45a71fd8cae7b82d09e1396e3cad72d2c4fa0c647c26b844`
  and the checksum list at
  `552f61181369e52c94059a1fb4191f0eec920f81037658080e9e6c2cd0cff209`.
- Next step: Commit this closeout evidence, verify the final remote/source and
  release state once more, and hand off the repaired installer URL.
