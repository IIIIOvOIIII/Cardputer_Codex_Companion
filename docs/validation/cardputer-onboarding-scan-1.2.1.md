# Cardputer onboarding scan 1.2.1 validation

## Release identity

- Version: `1.2.1`
- Target: M5Stack Cardputer / ESP32-S3 revision 0.2
- Flash size: 8 MiB
- Hardware port: `/dev/cu.usbmodem21101`
- First-install application offset: `0x20000`
- Observation date: 2026-07-28 HKT

## Root cause and correction

The original blank-device firmware processed `WIFI_EVENT_SCAN_DONE` inside the
default ESP event-loop task. That callback retrieved all access-point records,
allocated and sorted dynamic containers, updated onboarding state, and rendered
the UI while the event loop still owned its recursive mutex. The device
reproducibly asserted in `xQueueGenericSend` when the event loop released that
mutex.

Version 1.2.1 reduces the event callback to one atomic pending flag. The
existing `wifi-state` task retrieves at most 48 records, deduplicates them
without dynamic containers, and publishes the strongest 12 in deterministic
order. Hardware testing then exposed that the original 2304-byte
`wifi-state` stack could not safely include the onboarding/UI callback. The
measured release budget is 4608 bytes.

The attached device initially still carried the M5 factory partition table,
whose application slots begin at `0x10000` and `0x170000`. An application-only
write at `0x20000` is invalid on that layout. The generic full image was
therefore written at `0x0` first. Its boot log confirmed the product partition
table and loaded `ota_0` from `0x20000`. Application-only updates at `0x20000`
are valid only after that first full-image installation.

## Automated evidence

The focused regression suite passed:

- scan callback dispatch and bounded selection tests: passed;
- duplicate SSID, strongest-signal, capacity and deterministic-order host
  cases: passed;
- firmware static stack budget test: passed;
- target ESP-IDF 5.5.4 build: passed.

The complete public-release gate also passed:

- Python: 228/228;
- audio-specific Python: 29/29;
- normal firmware host: 38/38;
- ASan/UBSan firmware host: 38/38;
- clean ESP-IDF target build and partition validation: passed;
- target DIRAM headroom: 135,089 bytes;
- Swift ProductAudio, ProductGATT, ProductPet, telemetry and configuration
  tests: passed;
- C audio ring/device/IPC tests: passed;
- Windows Go and race tests: passed;
- macOS/Windows packaging, codesign and checksum verification: passed;
- current tree, all Git refs, reflogs and retained unreachable object
  credential audit: 0 findings.

The final application image is 1,622,848 bytes and its SHA-256 is:

`ebd3bd3e940f78ac2f2f64766da1a66e6b95c66440cdde9b4d733ab803671015`

The application was written at `0x20000`; esptool verified both the write hash
and an independent `verify_flash` digest.

The final generic full-image SHA-256 is:

`bded5b989f4c2962fc79c1f41ed7f3db7139b1fde6fb70a97d3070ff068f4a5f`

`dist/1.2.1-SHA256SUMS` independently verifies every delivered release
artifact.

## Hardware stability gate

The device was reset through its USB Serial/JTAG control lines and observed for
70 seconds at 115200 baud. The gate recorded:

- exactly one `App version: 1.2.1` boot;
- exactly one completed scan (`raw=22`, `published=12`);
- zero stack overflows;
- zero assertions;
- zero Guru Meditation errors;
- zero firmware reboots;
- minimum post-scan internal heap: 67,756 bytes;
- configured `wifi-state` stack: 4608 bytes;
- minimum post-scan `wifi-state` free stack: 1360 bytes.

The scan completed at approximately 5.9 seconds. Heap and stack readings
remained unchanged through the final sample at approximately 69.5 seconds.

## Result

Passed. A blank Cardputer reaches the Wi-Fi selection screen after scanning and
does not restart. The event loop remains lightweight, the worker path is
bounded, and the measured worker stack remains above the project's 1 KiB
minimum safety margin.
