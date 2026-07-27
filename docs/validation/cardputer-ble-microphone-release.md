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
  `ff23437a1200b0489e9eac0f0339c2b4d320fd8d1d79c877f83748d0143339d7`;
- private full image SHA-256:
  `db31b2242f473a8b3e006f907c20da6afdcc4aafbe97d6f71ff38d4025a7f190`;
- Companion executable SHA-256:
  `0de564c02a003d6715713084ae995ffb682f0d86cd86aca3b3cf26be80ef8456`;
- app-only flash at `0x20000` and independent `verify_flash`: digest matched;
- automated release gate: Python 191/191, audio-specific Python 17/17,
  normal host 36/36, sanitizer host 36/36, clean ESP-IDF build, Swift,
  C audio ring, HAL, signatures, and private packaging passed;
- USB-triggered no-signal guard test: firmware stopped after 17 captured
  frames with raw PCM peak/mean-absolute both equal to 8, zero source
  overrun, transport drop, sequence gap, allocation failure, or BLE
  reconnect.

The earlier 1800-second transport run is not valid evidence of acoustic
capture: its gate accepted constant non-zero PCM. Cold power-cycle diagnostics
measured a valid approximately 2.085 MHz clock on GPIO43 but no data edges on
GPIO46. Both current and legacy M5.Mic driver paths failed to obtain acoustic
data. The corrected firmware detects 16 consecutive constant low-level frames,
stops capture, and reports `MIC_NO_SIGNAL`; the HIL gate now requires signal
peak and RMS above 16.

The final LaunchAgent runs the released bundle and device status reports
version 1.1.1 with BLE/Wi-Fi/Agent `OK` and microphone `READY` before local
activation. Acoustic release validation remains blocked until the SPM1423,
its power/soldering, or the GPIO46 board connection is repaired and the HIL
passes with real signal.
