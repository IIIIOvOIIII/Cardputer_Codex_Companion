# Phase 0 Firmware Build

- Build date: 2026-07-24 15:14 HKT
- Source commit: `51363e092df6fc1be57f0a6323551b2fcb54439a`
- Target: ESP32-S3, 8 MiB Flash, no PSRAM
- Toolchain: ESP-IDF `v5.5.4` at `735507283d5b2f9fb363a1901172dbd9e847945d`
- Build method: `fullclean` followed by `build` and `merge-bin`

## Deliverables

| Artifact | Flash offset | Bytes | SHA-256 |
|---|---:|---:|---|
| `firmware/build/cardputer_codex_phase0-full.bin` | `0x0` | 766,528 | `525a2a7b0130089ae0ca2aefd868b121b8cb51f57ea636bc8cae309ae21f3b96` |
| `firmware/build/cardputer_codex_phase0.bin` | `0x10000` | 700,992 | `0458d987288abdde9af080c7cd64150943677b7537aa333b536bb43f72811025` |
| `firmware/build/bootloader/bootloader.bin` | `0x0` | 20,832 | `b2529f617dc535e3e88aac9cdb9b5c8193543eda842fc5ec258cee3e496b2f9f` |
| `firmware/build/partition_table/partition-table.bin` | `0x8000` | 3,072 | `7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820` |

The full image combines the bootloader, partition table and application and is
ready to flash at offset `0x0`. The standalone application image must only be
flashed at offset `0x10000`.

`esptool image_info` accepted the application image with a valid checksum and
validation hash. The application uses `0xab240` bytes of its `0x100000` factory
partition, leaving `0x54dc0` bytes (33%).

## Verification

- Phase 0 Python tests: 57 passed.
- Firmware host tests: 11 passed.
- ASan/UBSan firmware host tests: 11 passed.
- ESP-IDF clean target build: passed.
- BLE/HID fork, protocol-label, private TLS API and keyboard/network dependency
  scans: empty.
- Independent Task 8 and whole-firmware reviews: no new code-quality findings.

No physical device was connected. These files are compiled and structurally
validated but have not been flashed or exercised on a Cardputer.
