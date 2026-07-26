# Cardputer BLE microphone release validation

## Release identity

- Version: `1.1.0`
- Firmware rate preference: 24 kHz, with one bounded 16 kHz fallback
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

The final artifact hashes, serial path, installed driver version, LaunchAgent
PID, status endpoint, flash verification, and physical G0/30-minute HIL result
are recorded in the BLE microphone progress document after deployment. The
physical gate is intentionally performed last.
