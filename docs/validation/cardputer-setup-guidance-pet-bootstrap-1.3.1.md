# Cardputer setup guidance and Pet bootstrap 1.3.1 validation

## Release identity

- Factory version: `1.3.1`
- M5Launcher-compatible version: `1.3.1l`
- Target: M5Stack Cardputer / ESP32-S3 revision 0.2
- Hardware port: `/dev/cu.usbmodem21101`
- Attached-device application offset: `0x20000`
- Device address: `192.168.1.195`
- Observation date: 2026-07-28 HKT

## Behavior delivered

The first-run SETUP flow now gives the user an actionable path instead of only
showing a waiting state:

- the Bluetooth step asks the user to open the computer Bluetooth settings,
  search for `Cardputer Codex`, select Pair/Connect, and enter the computer's
  displayed code on the Cardputer;
- the Agent step keeps the device IP and PIN visible and includes the short
  macOS and Windows installation commands;
- the first authenticated Agent heartbeat commits onboarding completion and
  then shows a transient guide with the Settings shortcut, Web URL, device PIN,
  and `PRESS ANY KEY`;
- the acknowledgement key is consumed locally and cannot leak into HID;
- completion remains persisted before the guide is displayed, so a reboot
  skips the guide and safely enters the Pet page.

Both desktop Agents now treat a device `needs_snapshot` response as a forced
Pet synchronization request. The forced attempt runs before the snapshot
upload, does not wait for the regular 30-second cadence, and is deduplicated
against a Pet attempt already made in the same Agent loop.

## Automated evidence

The complete release gate passed:

- Python product suite: 263/263;
- audio and installer Python suite: 38/38;
- normal firmware host suite: 40/40;
- ASan/UBSan firmware host suite: 40/40;
- Node Web suite: 5/5;
- clean ESP-IDF Factory 1.3.1 target build: passed;
- target DIRAM headroom: 135,057 bytes;
- Swift application build plus ProductAudio, ProductGATT, ProductPet,
  telemetry, and configuration executable gates: passed;
- C audio ring, device, and IPC gates: passed;
- macOS packaging and signing gates: passed;
- Windows Go and race tests: passed;
- Factory and Launcher image validation: passed;
- focused packaging suite: 34/34;
- all 14 entries in `dist/1.3.1-SHA256SUMS`: passed;
- public-artifact allowlist: 15 approved top-level artifacts;
- current tree, all refs, reflogs, and retained unreachable-object credential
  audit: 0 findings.

The local Swift package's aggregate `swift test` command cannot import
`XCTest` with the installed toolchain. This does not affect the repository's
release gate: its executable Swift tests and release build all passed.

The SETUP pages were verified by the firmware host suite rather than by
clearing the attached device's initialized NVS. Tests cover the exact BLE,
Agent, and completion-guide copy; the persisted-complete/transient-guide
boundary; any-key acknowledgement; reboot behavior; and local key capture.

## Artifacts

- Factory full image:
  `d160bd57953650f516eb727bf087895ded595570288a569844078471f90b0a7c`
  (`1,761,568` bytes)
- application image:
  `a3f377fb8341c68dd1495df47af0c8ca604e1bbfdddc5cb51579c36fd84682f2`
  (`1,630,496` bytes)
- M5Launcher-compatible image:
  `459b8386db1d87278d9d4446c712dc90ce37553d7aee9e12db5c0b40a8dd51e9`
  (`6,426,624` bytes)

## Attached-device deployment

The device partition table was read before mutation. It contains `ota_0` at
`0x20000`, so the application-only deployment preserved NVS, Wi-Fi, profiles,
Pet data, and onboarding state. The 1.3.1 application image was written at
`0x20000`; esptool verified the write digest and an independent
`verify_flash` operation matched all `0x18e120` bytes.

The setup endpoint then reported:

```json
{"product":"Cardputer Codex Companion","version":"1.3.1","complete":true,"step":"complete"}
```

The installed per-user macOS Agent application was updated to 1.3.1 and its
LaunchAgent was restarted. The system-wide audio driver was intentionally left
unchanged because the delivered behavior does not modify the audio path or
require a disruptive Core Audio restart.

## Immediate Pet bootstrap gate

The Agent was first restarted to establish a fresh cadence baseline, then the
Cardputer was reset. The observed Pet transaction sequence was:

```text
baseline=303 after=304 api_ready_s=19 forced_pet_sync_s=24 cadence_remaining_gt_s=6
```

The new transaction happened after the device requested a snapshot and more
than six seconds before the first ordinary 30-second cadence attempt could
occur. This proves the first device load actively retrieves the Pet.

## Firmware stability gate

The deployed device was reset and observed over USB serial for 30 seconds. The
sample recorded:

- exactly one USB-triggered reset;
- `App version: 1.3.1`;
- persisted onboarding load `step=7`;
- `product runtime started`;
- 166,812 bytes free heap after profile-catalog load;
- 104,764 bytes free heap before HTTPS startup;
- zero allocation failures;
- no second reset, panic, Guru Meditation, abort, stack overflow, watchdog, or
  brownout through the last 29-second runtime sample.

## Result

Passed for implementation, complete release gate, state-preserving hardware
deployment, immediate Pet bootstrap, and attached-device stability. Public
GitHub Release and Pages publication are recorded separately after remote
verification.
