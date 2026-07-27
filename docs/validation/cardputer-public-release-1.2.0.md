# Cardputer Codex Companion 1.2.0 release validation

Validated on 2026-07-28 HKT from branch
`feat/public-release-onboarding`.

## Release result

The generic public release gate passed. The firmware full image contains an
erased `wifi_cfg` partition and no embedded Wi-Fi credential or pairing
material. First-run completion requires device Wi-Fi association, a bonded,
encrypted, authenticated and subscribed BLE HID connection, and an
authenticated Machine Agent heartbeat.

All non-pet pages accept backtick to cancel any unsaved edit and return to the
pet page. During onboarding, backtick steps back within the wizard instead.

## Automated evidence

- Python release tests: 223 passed.
- Audio/installer Python tests: 29 passed.
- Firmware host tests: 38 passed.
- ASan/UBSan firmware host tests: 38 passed.
- Final package tests: 25 passed.
- Swift ProductAudio, ProductGATT, ProductPet, ProductTelemetry and
  ProductConfiguration executables: passed.
- C17 audio ring, HAL device and IPC tests: passed.
- Windows Go tests and race-enabled tests: passed.
- ESP32-S3 firmware application size: `0x18c3c0` bytes with 48% of the
  smallest application partition free.
- Firmware static size: 1,622,829 bytes; DIRAM headroom: 143,497 bytes.
- Public full-image `wifi_cfg`: erased at `0x12000`, length `0x6000`.
- macOS application, HAL and bridge signatures: verified.
- Final public artifact allowlist: 11 approved top-level entries.
- Security audit: 7 refs, 201 reflog commits, 18 retained unreachable
  objects, 1,415 Git blobs and 450 current/artifact files scanned; 0 findings.
- Two independent firmware builds and two Windows installer builds produced
  byte-identical outputs.

## Artifact checksums

The authoritative manifest is `dist/1.2.0-SHA256SUMS`.

| Artifact | SHA-256 |
| --- | --- |
| `release/product-release.json` | `bd844c3171852568d5cc4fddd776134c4e86f32e7ebaadc7010461e7cbbc0e67` |
| `dist/cardputer_codex_companion.bin` | `5caffdc4568b17e24af45da03efd6f4f207f2f0c2c2406a9732746302620ca36` |
| `dist/cardputer_codex_companion-full.bin` | `17260f68349f297b53b41d5fe49f39d73bc09b207ebbc5277435a2e4d64cc525` |
| `dist/CardputerCompanion-1.2.0-windows-amd64.zip` | `e1ee6c6ce2f10dc36efaab1caf7a9f139cb13626d95da1881ed60c88a4efe2a9` |
| `dist/CardputerCompanion-1.2.0-windows-arm64.zip` | `ac384e421f0ddd055d4a44f43471b874e98ab5c500ea26ad19ee5a94257dbc17` |
| `dist/CardputerCompanion-1.2.0-windows-x64-setup.exe` | `03d94b3dcf6c7b6dbe62c0720714c1667391d4693746892fa9360c558bed98dd` |

The complete manifest additionally covers the macOS Agent, AudioBridge, HAL,
installer entry point and installer implementation.

## Remaining physical gates

- The Windows packages were cross-built and statically verified on macOS.
  A real Windows install, login-task restart, Codex connection and uninstall
  must still be run on Windows.
- A new-device end-to-end run remains pending until a fresh Cardputer is
  flashed and the on-device Wi-Fi, BLE and Agent onboarding steps are
  completed.
