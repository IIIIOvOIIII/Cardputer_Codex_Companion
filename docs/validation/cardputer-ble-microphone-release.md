# Cardputer BLE microphone release validation

## Release identity

- Version: `1.1.5`
- Firmware release rate: 16 kHz. The Audio v1 decoder remains compatible with
  24 kHz frames, but real-hardware transport evidence rejected 24 kHz as the
  release default because it starved concurrent HID traffic.
- Core Audio format: 48 kHz mono 32-bit float input-only
- Virtual device: `Cardputer Codex Microphone`
- Driver signing: current-Mac development ad-hoc signing
- HAL installation target:
  `/Library/Audio/Plug-Ins/HAL/CardputerCodexMicrophone.driver`
- Audio bridge target:
  `/Library/PrivilegedHelperTools/com.lynx.cardputer-audio-bridge`
- LaunchDaemon target:
  `/Library/LaunchDaemons/com.lynx.cardputer-audio-bridge.plist`

## Operation and privacy

- Short press G0 toggles capture; G0 has no long-press action.
- No BLE, Web, Codex, Profile, Wi-Fi, pet, or Mac command can remotely start
  capture.
- BLE disconnect, sink-not-ready, Companion exit, stale producer lease,
  fatal capture error, and reboot stop capture.
- Reconnection only returns the device to `READY`. A new local G0 click is
  required to become live.
- The automated release path stores metrics only and rejects tracked WAV,
  AIFF, CAF, PCM, ADPCM, M4A, or MP3 content artifacts.

## Installation

```bash
scripts/package_mac_installer.sh
dist/CardputerCompanion-mac-installer/install.sh install
dist/CardputerCompanion-mac-installer/install.sh status
```

The installer places the app under `~/Applications`, keeps the PIN-bearing
configuration at mode `0600`, installs and starts the HAL/AudioBridge, and
loads a stable-path LaunchAgent. `uninstall` preserves configuration and logs;
`uninstall --purge` removes them. Normal `run` does not require elevation.
If a later LaunchAgent/bootstrap step fails, a fresh install removes the newly
installed system-audio components, while an upgrade restores and reinstalls
the prior app's HAL/bridge before restarting Core Audio. LaunchAgent status
parsing accepts only launchctl's top-level `state` and `pid` fields.
The development build validates the exact Companion bundle identifier,
ad-hoc signature, and current console UID.
A root-owned launchd Mach service owns the anonymous shared ring and authenticates
the Companion producer plus Apple platform-signed `coreaudiod` consumer. The HAL
plug-in does not try to register a Mach service from inside `coreaudiod`.
A broadly distributable build still requires Developer ID signing and Apple
notarization.

## Recovery behavior

The Companion sends sink-not-ready before normal shutdown. If the HAL producer
heartbeat fails, it suspends the Cardputer sink, rebuilds its launchd audio-bridge
XPC/ring lease, then sends sink-ready. Firmware returns to `READY`; it does not
resume PDM capture. The HAL maps the consumer side of the same bridge-owned ring
and renders digital silence while there is no valid producer.

## Reboot recovery gate

- Five consecutive host-controlled Cardputer resets.
- `MIC READY` within 15 seconds after each reset.
- Agent PID unchanged for the full gate.
- BLE, Wi-Fi, and Agent remain `OK`.
- No panic, abort, allocation failure, or automatic Agent restart.
- Existing `HIL MIC START` and `HIL MIC STOP` succeed after cycle five.

## Final evidence

The corrected release was built, installed, and tested on 2026-07-27:

- app-only firmware SHA-256:
  `59e4e3f755bdb7fdcbd1d5145c495a21a5dfd8039b63380990afa8bab1f69d64`;
- generic full image SHA-256:
  `3971d1cb61a0e7c68513e3ec33dc0e6ef07d9b7fda067ec13b3cabcda31db313`;
- private full image SHA-256:
  `d22597f56a13f47d89e8c08cb48591846cba9b73c6a8a4f254a736cebfcfc7ac`;
- Companion executable SHA-256:
  `01a7f68d67f5501892c092ce082feb04776f93e8b78916f1e1a58e6cc002df9d`;
- packaged installer engine SHA-256:
  `3e18633ca77dbacf40837e2019b74a31b3dff7448b5bfc42c9ed75f2ec42d993`;
- AudioBridge SHA-256:
  `b2effde04edab161ee67f048ad43fbec73b2038180a5c898291d699e8e7ee1eb`;
- HAL executable SHA-256:
  `e5912075df27956128a2b8e30836b412a7371abb1f7b0f17ed2fad1efa38897e`;
- app-only flash at `0x20000` and independent `verify_flash`: digest matched;
- automated release gate: Python 205/205, audio-specific Python 29/29,
  normal host 37/37, sanitizer host 37/37, clean ESP-IDF build, Swift,
  C audio ring/device/IPC, HAL, signatures, and private packaging passed;
- 600-second HIL report
  `build/hil/cardputer-release-1.1.5-600s.json`: 21,508 captured,
  21,501 received, zero source overruns, transport drops, sequence gaps,
  BLE reconnects, allocation failures, or HID queue failures; maximum gap
  105 ms; signal peak 14,579 and RMS 102.56; HID 1000/1000 with 100 us p95;
  steady internal heap 67,200 bytes, largest block 45,056 bytes, TLS-burst
  heap 52,428 bytes, and every task stack gate passed;
- AVFoundation directly read the installed `Cardputer Codex Microphone`.
  With capture stopped it returned fail-safe digital silence at -91 dBFS.
  With the serial HIL equivalent of a G0 click, the device reported LIVE16
  and 109,080 samples measured -41.1 dBFS mean and -13.1 dBFS peak;
- the packaged installer reports `APP OK`, `CONFIG OK`,
  `AGENT RUNNING`, `HAL OK`, `BRIDGE OK`, `AUDIO OK`, and
  `LAN AUTHENTICATED`.

The 1.1.4 display correction eliminated moving-pet horizontal tearing by
decoding the complete 96 x 104 RGB565 frame before one LCD `pushImage`.
Version 1.1.5 preserves that exact transfer boundary but makes the 19,968-byte
frame buffer scoped to one render, releasing it immediately afterward. This
restored steady and TLS-burst heap margins without returning to row-by-row
display writes.

The replacement hardware diagnostic found that ESP-IDF 5.5 left GPIO46 as an
output after `M5.begin()` board probing. Explicitly restoring the microphone
data pin to an unpulled input before the unmodified `M5.Mic.begin()` restored
dynamic PCM. The firmware retains the constant-low-level no-signal guard.

The Mac all-zero input defect was independent of microphone capture. The HAL
now refreshes its shared-ring mapping when the first StartIO arrives, and the
privileged bridge authenticates the actual macOS 14+ Apple platform-signed
`com.apple.audio.Core-Audio-Driver-Service.helper` host running as
`_coreaudiod`. Exact identity checks remain fail-closed.

A failed pre-fix long run also exposed temporary NimBLE mbuf exhaustion. The
16 kHz state machine previously treated the second bad loss window as a
permanent error. It now restarts 16 kHz capture and marks a discontinuity so a
transient BLE backpressure event cannot leave the Mac input permanently silent;
capture backend and no-signal faults still stop with an error.

The final LaunchAgent and privileged AudioBridge run the installed 1.1.5
artifacts. Device status reports version 1.1.5, BLE/Wi-Fi/Companion `OK`,
GATT microphone `READY`, and no microphone error. The PIN-bearing Mac
configuration remains mode `0600`, and the protected LAN status endpoint
authenticates successfully.
