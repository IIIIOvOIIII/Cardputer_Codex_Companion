# Factory and Launcher Dual-Release Design

Date: 2026-07-28  
Factory release: `1.3.0`  
Launcher-compatible release: `1.3.0l`

## Goal

Publish Cardputer Codex Companion through two explicit installation channels:

1. an official factory channel that replaces the complete flash layout and
   provides every product feature;
2. a Launcher-compatible channel that keeps M5Launcher and installs the same
   product source into Launcher's managed partition layout.

The Launcher-compatible edition is a build and packaging variant only. It does
not introduce a long-lived branch, a fork of Launcher, or a reduced product
implementation.

## Confirmed Root Cause

The failed Web login was not caused by an incorrect PIN. The device returned
HTTP 401 for an intentionally invalid PIN, but a request authenticated with the
persisted PIN reached `/api/v1/profiles` and returned HTTP 503
`profile_catalog_failed`.

Serial and flash inspection showed that the image had been installed through
M5Launcher into an older Launcher-owned layout:

- the product `wifi_cfg` partition was absent;
- the product `storage` partition was absent;
- profile catalog and pet storage initialization failed;
- the Web application collapsed the authenticated HTTP 503 into the generic
  message `PIN 错误或设备不可达`.

Writing the product factory image at physical offset `0x0`, while preserving and
restoring the existing NVS data for this diagnostic recovery, installed the
expected product partition table. The device then booted version `1.2.3`,
initialized the profile catalog, restored Wi-Fi and BLE state, and reported
`LAN AUTHENTICATED`.

## Release Architecture

### Shared source

Both editions are built from the same `main` source tree and the same feature
set. A build-time firmware version input produces these runtime versions:

- factory firmware: `1.3.0`;
- Launcher-compatible firmware: `1.3.0l`.

The suffix is not implemented through source duplication or a release branch.
The Agent applications remain at the product release version `1.3.0`; only the
Launcher firmware variant carries the `l` suffix.

### Official factory channel

The official artifact is:

`Cardputer-Codex-Companion-1.3.0-factory.bin`

It contains the bootloader, product partition table, initial OTA data, and the
`1.3.0` application, and is written at physical flash offset `0x0`. The layout
is:

| Label | Offset | Size |
| --- | ---: | ---: |
| `nvs` | `0x9000` | `0x6000` |
| `otadata` | `0xf000` | `0x2000` |
| `phy_init` | `0x11000` | `0x1000` |
| `wifi_cfg` | `0x12000` | `0x6000` |
| `ota_0` | `0x20000` | `0x300000` |
| `ota_1` | `0x320000` | `0x300000` |
| `storage` | `0x620000` | `0x1e0000` |

This channel replaces Launcher and is the recommended installation path. A
project-owned Web Serial installer uses a pinned ESP Web Tools client and an
install manifest that writes the factory image at `0x0`. The page must state
that a factory installation resets device setup data, including Wi-Fi, PIN,
profiles, and BLE pairing.

The existing unversioned full-image and app-only names remain as compatibility
aliases for scripts, but public documentation leads with the explicit
versioned factory artifact. Fixed-offset app-only updates are supported only
after the product partition table has been installed by the factory channel.

### Launcher-compatible channel

The Launcher artifact is:

`Cardputer-Codex-Companion-1.3.0l-launcher.bin`

It is a merged image whose embedded table declares the product `storage`
partition and whose file extends exactly to the declared `storage` start at
`0x620000`. It contains no private Wi-Fi NVS and no storage payload. Extending
the file to the partition boundary makes the empty storage declaration
unambiguous to Launcher's merged-image parser.

The compatibility contract is:

- M5Launcher `2.8.0` or later is required;
- Launcher retains its protected application and OTA data partitions;
- Launcher creates or resizes a SPIFFS-subtype partition labelled `storage`;
- the resulting `storage` partition must be at least `0x1e0000` bytes;
- the application may run from any Launcher-selected OTA application offset;
- future updates use Launcher, not the product's fixed-offset app-only image;
- if Launcher cannot allocate the required app and storage space, installation
  fails rather than silently installing a partially functional product.

The repository does not build, patch, distribute, or maintain a Launcher fork.
Compatibility is validated against the public M5Launcher `2.8.x` behavior and
is enforced by the Companion artifact and runtime checks.

## Runtime Partition Contract

Firmware validates its storage dependency before enabling profile and pet
storage. A compatible runtime must find a partition with:

- label `storage`;
- data type and SPIFFS subtype;
- size greater than or equal to `0x1e0000`.

Validation yields one of:

- `ready`;
- `missing`;
- `wrong_type`;
- `too_small`.

The value is exposed through the existing status model and `/api/v1/status`.
Factory and Launcher installs use the same validation. No code assumes that
`storage` is at the factory offset when accessing it.

If validation fails:

- the device displays `PARTITION ERROR` and a concise reinstall instruction;
- BLE HID and setup recovery remain available where their own dependencies are
  intact;
- profile and pet writes remain disabled;
- profile APIs return HTTP 503 with a structured
  `partition_incompatible` error and the concrete validation reason;
- the firmware does not reboot or retry storage initialization indefinitely.

## Web Authentication Error Semantics

The Web login flow must preserve the distinction between authentication,
connectivity, and storage compatibility:

| Condition | User-visible result |
| --- | --- |
| PIN is not eight digits | `请输入 8 位数字 PIN` |
| Device returns HTTP 401/403 | `PIN 错误` |
| Browser cannot reach the HTTPS endpoint | `设备不可达，请检查 IP、Wi-Fi 和证书访问` |
| Authenticated profile request returns partition HTTP 503 | `设备分区不兼容，请重新安装官方 Factory 固件或 Launcher 2.8+ 兼容固件` |
| Other authenticated server error | `设备服务暂不可用，请稍后重试` |

The login screen authenticates against the status endpoint first. A successful
status response establishes that the PIN is correct even if loading the profile
catalog subsequently fails. Errors must retain the HTTP status and JSON error
code through the JavaScript request helper instead of being reduced to a plain
message.

## Packaging and Publication

The `1.3.0` release publishes:

- factory firmware `1.3.0`;
- app-only firmware `1.3.0`;
- Launcher-compatible firmware `1.3.0l`;
- macOS Agent and audio installer `1.3.0`;
- Windows Agent packages `1.3.0`;
- the project-owned Web Serial installer files;
- a single `1.3.0-SHA256SUMS` manifest covering every public artifact.

Build metadata records the shared source commit, the two firmware runtime
versions, the minimum Launcher version, and the required storage contract.
Release verification rejects a Launcher artifact that:

- contains a non-erased `wifi_cfg` payload;
- ends before or after `0x620000`;
- lacks the `storage` table entry;
- declares a storage size below `0x1e0000`;
- embeds a runtime version other than `1.3.0l`.

README files explain which channel to choose, that factory installation removes
Launcher, and that Launcher users must update Launcher to at least `2.8.0`
before installation.

## Verification

### Automated

- Version consistency tests cover factory `1.3.0`, Launcher `1.3.0l`, and Agent
  `1.3.0`.
- Packaging tests parse both images and prove their embedded application
  versions and partition declarations.
- Launcher-package tests prove the exact `0x620000` file length and erased
  configuration/storage boundary.
- Runtime host tests cover `ready`, `missing`, `wrong_type`, and `too_small`.
- Web tests exercise HTTP 401, network failure, partition HTTP 503, and generic
  authenticated server failure as distinct visible messages.
- Existing firmware, Agent, installer, credential-audit, checksum, and
  reproducibility gates remain mandatory.

### Hardware

The attached 8 MiB Cardputer is verified through both paths:

1. install the `1.3.0l` artifact from M5Launcher `2.8.x`, confirm Launcher is
   retained, runtime version is `1.3.0l`, storage is ready, Web PIN login loads
   profiles, pets synchronize, BLE HID works, and Agent heartbeat authenticates;
2. install the `1.3.0` factory artifact at `0x0`, confirm the product layout,
   runtime version, setup flow, Web PIN login, profiles, pets, BLE HID, and Agent
   heartbeat.

The final device state is the official `1.3.0` factory build. Hardware logs must
show no panic, watchdog reset, reboot loop, or profile catalog initialization
failure.

## Explicit Non-Goals

- No M5Launcher fork or patch distribution.
- No support promise for Launcher versions earlier than `2.8.0`.
- No separate Launcher source branch.
- No reduction of pet, profile, Web, BLE HID, microphone, or Agent features in
  the Launcher edition.
- No attempt to make the product's dual-OTA fixed layout coexist with Launcher.
- No automatic migration of credentials or profiles between factory and
  Launcher layouts in public artifacts.
