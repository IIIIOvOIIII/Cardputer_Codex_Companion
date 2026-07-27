# Cardputer BLE microphone release validation

## Release identity

- Version: `1.1.1`
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
scripts/build_companion.sh
sudo dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  install-audio-driver
dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion doctor audio
```

Normal `run` does not require elevation. The development build validates the
exact Companion bundle identifier, ad-hoc signature, and current console UID.
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

## Final evidence

The corrected release was built and tested on 2026-07-27:

- app-only firmware SHA-256:
  `70c5a5426a4cc6f93abbe08599c0c5e8ec4a509796a7a665dbde144eced45e9a`;
- private full image SHA-256:
  `a5f9ca147212b5772d8ece49ab3ab30e1da90899fa6879885821addeadac3c46`;
- Companion executable SHA-256:
  `166ad3626196b63a01683e5cdc496f30ed3b8b4a606dedd9066e729a28515d13`;
- AudioBridge SHA-256:
  `b2effde04edab161ee67f048ad43fbec73b2038180a5c898291d699e8e7ee1eb`;
- HAL executable SHA-256:
  `f2027535a5c82a0ad325761983ca2079c582e575be5825e8f56c4de096d28bc2`;
- app-only flash at `0x20000` and independent `verify_flash`: digest matched;
- automated release gate: Python 192/192, audio-specific Python 17/17,
  normal host 36/36, sanitizer host 36/36, clean ESP-IDF build, Swift,
  C audio ring/device/IPC, HAL, signatures, and private packaging passed;
- 1800-second HIL report
  `build/hil/cardputer-audio-self-heal-final-1800s.json`: 64,711 captured,
  64,698 received, zero source overruns, transport drops, sequence gaps,
  BLE reconnects, allocation failures, or HID queue failures; maximum gap
  147 ms; signal peak 32,768 and RMS 309.19; HID 1000/1000 with 100 us p95;
  steady internal heap 66,904 bytes, largest block 45,056 bytes, TLS-burst
  heap 55,448 bytes, and every task stack gate passed;
- AVFoundation directly read the installed `Cardputer Codex Microphone`:
  quiet peak/RMS were -38.53/-53.11 dBFS across 120,960 samples, and a short
  local speech stimulus raised them to -29.81/-46.41 dBFS across 121,140
  samples.

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

The final LaunchAgent runs the released bundle. Device status reports version
1.1.1, BLE/Wi-Fi `OK`, GATT microphone `READY`, and no microphone error. The
replacement device's LAN Companion PIN has not been copied into the existing
Mac configuration, so the independent LAN heartbeat currently reports
Companion `OFFLINE`; this does not affect BLE audio or the Core Audio device.
