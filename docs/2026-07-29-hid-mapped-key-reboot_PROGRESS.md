## 2026-07-29 16:05 HKT

- Current work: Diagnose reboot when a custom profile maps the `=` key to a HID chord.
- Expected result: Capture enough evidence to isolate the mapped-key path and add a regression guard before changing firmware.
- Result: Achieved. Serial monitoring showed the device rebooted after the mapped-key trigger, and source review isolated the path to mapped-key macro execution plus direct HID report sends from multiple tasks.
- Next step: Serialize ESP HID report delivery, update firmware version, build, flash, and verify the mapped key no longer reboots.

## 2026-07-29 22:08 HKT

- Current work: Route every ESP complete HID report through the dedicated `keyboard-hid` sender task, build 1.3.4/1.3.4l, and repeat the mapped `=` hardware gate.
- Expected result: The configured HID chord is emitted without entering the BLE stack from `product-macro`, with no reset or HID queue failure.
- Result: Achieved. The regression contract failed before the implementation and passed afterward; 41 host tests and 12 focused product tests passed. The attached Launcher device was flashed at `0x170000`, then emitted `mod=0x08 keys=0c` twice while uptime continued beyond 190 seconds. Runtime metrics reported four generated/queued key events, zero queue failures, and 100 microsecond p95 latency. Factory DIRAM headroom is 134377 bytes.
- Next step: Verify packaged image identities and checksums, commit the 1.3.4 fix, push `main`, and record the troubleshooting result in daily memory.

## 2026-07-29 22:15 HKT

- Current work: Complete pre-commit verification and package the final firmware artifacts.
- Expected result: Normal and sanitizer host suites pass, both firmware variants report the intended versions, the public Factory image contains no Wi-Fi credentials, and artifacts are reproducible from the current source tree.
- Result: Achieved for the firmware scope. Normal and sanitizer host suites each passed 41/41; focused firmware/product tests passed 28/28; both firmware builds completed; the public-image and Launcher validators passed. SHA-256: Factory `741eb40b6cd8590c7d6cb87b78d8d8a8ce0eebeb0bc8da548803312205db8249`, app `a06be349293a5ec1dc56c57f8348d777bb063c2ad72629e60c5e82830a9d7710`, Launcher `302ed51b5cc3e571e8c03852e90108db99d5a8cdda8cdcf59bc08a04833546a4`. The broader product test collection passed 204 tests; its three release-packaging checks remain intentionally outside this firmware-only staging change because the public manifest and Windows packages still identify the published 1.3.3 release.
- Next step: Commit and push the firmware-only 1.3.4 fix without changing the published 1.3.3 release manifest.

## 2026-07-29 22:22 HKT

- Current work: Close the firmware-only mapped-key reboot repair.
- Expected result: The reviewed source and tests are committed and pushed while the published 1.3.3 release manifest remains unchanged.
- Result: Achieved. Commit `938e093` was pushed to `origin/main` using the repository-authorized SSH identity. The attached Cardputer remains on the verified 1.3.4l Launcher build.
- Next step: Publish a unified 1.3.4 public release only under a separate explicit release approval.
