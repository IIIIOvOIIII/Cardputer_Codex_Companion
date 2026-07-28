[简体中文](README.zh-CN.md)

# Cardputer Codex Companion

Cardputer Codex Companion turns an [M5Stack Cardputer](https://docs.m5stack.com/en/core/Cardputer) into a LAN-only Codex remote display, a programmable Bluetooth keyboard, and an optional wireless microphone for macOS.

Current release: **1.2.1**

## Overview

The firmware combines four roles:

- a standard Bluetooth Low Energy HID keyboard;
- a 240×135 animated Codex companion and session-status display;
- an authenticated HTTPS configuration console on the local network;
- a Machine Agent endpoint for Codex sessions, actions, pet sync, Unicode input, and—on macOS—microphone audio.

The public firmware contains no Wi-Fi credentials, device PIN, pairing data, private certificate, or remote-control service. A new device is commissioned entirely on the Cardputer and remains accessible only from its LAN.

## Features

- Complete 56-key Cardputer keyboard scanning with BLE HID reports.
- Four-layer keyboard profiles with pass-through keys, HID shortcuts, UTF-8 strings, multi-step sequences, device actions, and Codex actions.
- Direct HID output for shortcuts such as `Alt+V`; no Machine Agent is required for HID-representable shortcuts or ASCII text.
- Authenticated Web editor for key mappings, Wi-Fi, PIN, profiles, pets, display settings, and timing.
- Persistent eight-digit PIN that changes only when explicitly rotated.
- Codex session title, model, Fast mode, thinking level, approvals, inputs, and available limits on the device display.
- Five device pages: Pets, Device, Codex, Sync, and Settings.
- User-selectable animated pets at roughly 2–3 frames per second.
- Device-led first-run setup for Wi-Fi, BLE pairing, and Machine Agent installation.
- Encrypted BLE Unicode transport for Chinese and other non-HID text on macOS.
- Encrypted 24 kHz IMA-ADPCM microphone transport to the macOS virtual input device.
- Reproducible public release gate with host tests, sanitizers, firmware memory checks, platform tests, artifact allowlisting, checksums, and an all-history credential audit.

## Supported Platforms

| Component | Supported target | Notes |
| --- | --- | --- |
| Firmware | M5Stack Cardputer / ESP32-S3 | Required |
| BLE keyboard | macOS, Windows, iPadOS, iOS, and other BLE HID hosts | Native HID path |
| Machine Agent | macOS 14 or later | Full feature set |
| Machine Agent | Windows 10 22H2 / Windows 11 | Codex state/actions and pet sync |
| Bluetooth microphone | macOS only | Installs a HAL driver and AudioBridge |
| Unicode GATT injection | macOS only | Windows support is not included in 1.2.1 |

The firmware does not provide Internet or out-of-LAN Codex control.

## Firmware Installation

### Requirements

- M5Stack Cardputer and a data-capable USB-C cable.
- Python 3 with [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/installation.html).
- The `1.2.1` release artifacts and `1.2.1-SHA256SUMS`.

Verify the downloaded files before flashing:

```bash
shasum -a 256 -c 1.2.1-SHA256SUMS
```

### New or factory-reset device

Use the generic full image. Replace the port with the one shown on your computer.

macOS example:

```bash
python3 -m esptool --chip esp32s3 \
  --port /dev/cu.usbmodemXXXX -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x0 cardputer_codex_companion-full.bin
```

Windows example:

```powershell
py -m esptool --chip esp32s3 `
  --port COM5 -b 460800 `
  --before default_reset --after hard_reset `
  write_flash 0x0 cardputer_codex_companion-full.bin
```

The full public image deliberately leaves the Wi-Fi configuration partition erased.
Factory firmware, M5Launcher, and other third-party firmware may use a
different partition table. Always perform this full-image installation once
before using the application-only upgrade command below.

### State-preserving upgrade

To preserve the existing PIN, Wi-Fi, profiles, pets, onboarding record, and BLE bonds, flash only the application image at `0x20000`:

```bash
python3 -m esptool --chip esp32s3 \
  --port /dev/cu.usbmodemXXXX -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x20000 cardputer_codex_companion.bin
```

Never write the application-only image at `0x0`.

## First-Run Setup

A blank device must pass three gates. Normal HID output and the regular Web console remain disabled until setup is complete.

1. **Connect Wi-Fi on the Cardputer.** The device scans nearby networks. Use `;` and `.` to move, Enter to select, and the backtick key to go back. Enter the Wi-Fi password on the Cardputer; it is masked and is persisted only after the device receives an IP address. Hidden SSIDs are supported.
2. **Pair Bluetooth from the computer.** Open the host Bluetooth settings and pair **Cardputer Codex**. If a passkey is requested, enter it on the Cardputer and press Enter. Setup advances only after an authenticated bond and a live HID subscription exist.
3. **Install and pair the Machine Agent.** Use the device IP and the eight-digit PIN displayed on the Cardputer. The first authenticated heartbeat completes setup.

After setup, open `https://CARDPUTER_IP/`, accept the device-generated local certificate, and sign in with the same PIN.

## Machine Agent

The Agent runs Codex locally through `codex app-server --listen stdio://`, syncs session state to the Cardputer, and forwards authenticated Codex actions. Pairing records the Cardputer certificate fingerprint so an unexpected certificate change is rejected.

### macOS

From a source checkout, use the installer at the project root:

```bash
./install.sh install
./install.sh status
```

If `dist/CardputerCompanion.app` is absent, `install` builds it before
dispatching to the protected Python installer. `status` and `uninstall` never
trigger a build.

The prebuilt `CardputerCompanion-mac-installer` release directory exposes the
same commands:

```bash
cd CardputerCompanion-mac-installer
./install.sh install
./install.sh status
```

The installer interactively requests `https://CARDPUTER_IP` and the device PIN. The PIN is masked and stored in a mode-`0600` configuration file; it is never placed in the command line, LaunchAgent plist, or logs.

The installation contains:

- `~/Applications/CardputerCompanion.app`;
- a per-user LaunchAgent;
- `Cardputer Codex Microphone` Core Audio HAL driver;
- the privileged AudioBridge required by the virtual microphone.

For low-level audio diagnosis only:

```bash
CardputerCompanion.app/Contents/MacOS/cardputer-companion doctor audio
sudo CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  install-audio-driver
sudo CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  uninstall-audio-driver
```

Uninstall the runtime while keeping pairing configuration and logs:

```bash
./install.sh uninstall
```

Perform a clean uninstall, including pairing configuration and logs:

```bash
./install.sh uninstall --purge
```

### Windows

On Windows x64, run:

```text
CardputerCompanion-1.2.1-windows-x64-setup.exe
```

The per-user installer writes to `%LOCALAPPDATA%\CardputerCodexCompanion`, creates a least-privilege logon Scheduled Task, and adds Pair Device, Status, Doctor, and Uninstall shortcuts to the Start Menu. It does not install a driver or Windows service and does not require administrator rights.

For Windows ARM64, extract `CardputerCompanion-1.2.1-windows-arm64.zip` and run:

```text
cardputer-agent.exe pair
cardputer-agent.exe status
cardputer-agent.exe doctor
```

The PIN is masked and protected with Windows DPAPI for the current user. Uninstall from **Installed apps** or the Start Menu. Windows uninstall removes the Agent, task, configuration, logs, shortcuts, and uninstall registration.

Windows 1.2.1 does not include Unicode GATT injection or the Bluetooth microphone.

## Web Console and Device Controls

Browse to `https://CARDPUTER_IP/` and authenticate with the device PIN.

- Click a physical key to edit its actual action.
- Capture a shortcut by focusing the shortcut field and pressing the desired combination.
- UTF-8 string actions support Chinese and other Unicode text through the macOS Agent.
- Settings can rotate the PIN, change Wi-Fi, switch profiles, tune display behavior, and select pets.

Device navigation:

- `Fn+;` / `Fn+.`: up/down;
- `Fn+,` / `Fn+/`: previous/next page;
- bare `; . , /` plus Enter: navigate Settings;
- backtick: cancel the current editor or return from any non-pet page;
- G0 short press: start/stop the macOS microphone after the audio link is ready.

Keys are forwarded to BLE HID only from the Pets page. BLE passkey entry always has the highest input priority.
G0 short press is the only recording control. After a reboot, disconnect, or
Agent exit, the microphone returns to READY and does not automatically resume
recording. The Pets status markers `B/W/M` report BLE, Wi-Fi, and authenticated
Machine Agent connectivity.

## Build and Verification

Prerequisites include ESP-IDF 5.5.4, CMake, Python 3.11 with `uv`, Swift 6, Go, and NSIS.

Run the complete public release gate:

```bash
scripts/verify_product_release.sh
```

Useful focused checks:

```bash
PYTHONPATH=. uv run pytest -q
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
scripts/build_web_assets.py --check
```

The complete gate builds:

- `dist/cardputer_codex_companion-full.bin`;
- `dist/cardputer_codex_companion.bin`;
- `dist/CardputerCompanion-mac-installer/`;
- Windows x64 installer and amd64/ARM64 portable archives;
- `dist/1.2.1-SHA256SUMS`.

See [PUBLIC_RELEASE.md](docs/PUBLIC_RELEASE.md) for the security and artifact boundary and [WINDOWS_AGENT.md](docs/WINDOWS_AGENT.md) for Windows-specific details.

## Security and Privacy

- Device control is LAN-only and PIN-authenticated.
- Public firmware contains no Wi-Fi, PIN, pairing, or private certificate data.
- Wi-Fi passwords and PINs are never logged.
- HTTPS uses a device-generated P-256 identity.
- Agent pairing pins the device certificate fingerprint.
- Public release checks inspect Git refs, reflogs, retained unreachable objects, current files, and artifacts without printing candidate secrets.
- `build/private/`, `dist/private/`, private NVS blobs, audio captures, and credential-bearing images are forbidden release inputs.

Please report security issues privately to the repository owner rather than opening a public issue with credentials or logs.

## Author

Created and maintained by **Lynx** ([hi@iam.lc](mailto:hi@iam.lc)).

## License

Copyright 2026 Lynx.

Licensed under the [Apache License 2.0](LICENSE).
