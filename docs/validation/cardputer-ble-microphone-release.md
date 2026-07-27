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

The final release was built and tested on 2026-07-27:

- app-only firmware SHA-256:
  `662c82033442f0a07017894101930a8a583a2d1ec86023eabc9fe96d4a8f8bce`;
- private full image SHA-256:
  `463cbff6425ff72cd556b963be2be37c651edf7c13da0da5c5b7a699eaa99467`;
- Companion executable SHA-256:
  `875f3ea2ced69c5073f9fd63415fdbf775df1eb3abdf8568c8668d791ddb8713`;
- app-only flash at `0x20000` and independent `verify_flash`: digest matched;
- 1800-second USB-triggered real-device HIL:
  64,293 captured / 64,287 received, zero source overrun, transport drop,
  sequence gap, allocation failure, or BLE reconnect; maximum gap 107 ms;
- concurrent HID: 1000 generated / 1000 queued, zero failure, p95 100 us;
- minimum steady internal heap 70,208 bytes, steady largest block 43,008
  bytes, and TLS-burst heap 56,880 bytes; every task stack gate passed.

After the HIL client disconnected, the development ad-hoc signature required
renewed macOS Bluetooth approval because its CDHash changed. The final
LaunchAgent runs the released bundle and repeated device status checks report
version 1.1.1, BLE/Wi-Fi/Agent `OK`, and microphone `READY`.
