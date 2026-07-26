# Cardputer BLE Microphone and macOS Virtual Input Design

## Goal

Extend Cardputer Codex Companion `1.0.31` with a speech microphone path whose
audio payload travels over the existing encrypted Bluetooth Low Energy
connection. A short G0 click toggles capture. macOS publishes the received
audio as a system input device named `Cardputer Codex Microphone`.

The release target is firmware `1.1.0`, Companion audio protocol `1.0`, and a
matching `1.0.0` Core Audio plug-in.

## Confirmed Product Decisions

- macOS must expose a system-wide microphone usable by Codex and other apps.
- Audio payloads travel over BLE, not Wi-Fi.
- G0 is a short-click recording toggle.
- All former G0 functions move to Settings. G0 has no long-press or hidden
  recovery action.
- The preferred capture format is 24 kHz, 16-bit, mono speech.
- The implementation uses custom encrypted Audio GATT plus a macOS virtual
  input device.
- The current BLE HID keyboard, Unicode GATT path, LAN-only Web console,
  Codex status, pet synchronization, Profiles, and persisted settings remain
  supported.

## Hardware and Platform Boundary

The initial target is the original Cardputer and Cardputer v1.1 with:

- ESP32-S3;
- 8 MiB flash;
- no PSRAM;
- SPM1423 PDM microphone;
- microphone data on GPIO46;
- microphone clock on GPIO43.

GPIO43 is also used by the onboard speaker path. The microphone controller
must stop and disable the speaker before enabling microphone capture. The
first release does not support Cardputer-Adv, whose ES8311 audio architecture
is different.

ESP32-S3 does not support Bluetooth Classic, so it cannot implement the HFP
microphone profile used by conventional Bluetooth headsets. Bluetooth LE
Audio requires the Bluetooth 5.2 isochronous transport and is not available
on this hardware. The Mac therefore sees:

- `Cardputer Codex` as the existing BLE keyboard in Bluetooth settings; and
- `Cardputer Codex Microphone` as a Core Audio virtual input device.

It does not see a native HFP or LE Audio headset.

The macOS baseline remains macOS 14 or newer. The current Mac is the required
release-validation machine. Other macOS releases and Intel Macs are not
claimed until separately tested.

## Considered Architectures

### Custom BLE audio plus a Core Audio virtual microphone -- selected

Cardputer captures speech, encodes independent IMA-ADPCM blocks, and sends
them through new encrypted GATT characteristics on the existing connection.
The Mac Companion decodes and buffers the stream, then supplies an
`AudioServerPlugIn` input device.

This route keeps the audio payload on Bluetooth, preserves the existing bond,
and lets any Core Audio client use the microphone.

### Raw 24 kHz PCM over BLE -- rejected

Uncompressed 24 kHz, 16-bit, mono PCM requires 384 kbit/s before ATT overhead.
That leaves too little margin for HID, Unicode GATT, connection scheduling,
and the no-PSRAM firmware. It is not an acceptable release risk.

### BLE control with Wi-Fi audio -- deferred fallback

Using BLE for activation and Wi-Fi for Opus or PCM would provide more
throughput, but would violate the confirmed Bluetooth transport requirement.
It is not part of the first release.

## End-to-End Architecture

```text
Cardputer SPM1423
  -> PDM/I2S capture
  -> 24 kHz / 16-bit / mono PCM
  -> independent 10 ms IMA-ADPCM blocks
  -> encrypted BLE Audio Data notifications
  -> Mac Companion decode and jitter buffer
  -> 24 kHz to 48 kHz resampling
  -> preallocated shared audio ring
  -> Core Audio HAL plug-in
  -> Cardputer Codex Microphone
  -> Codex, meeting apps, or another Core Audio client
```

Wi-Fi is not in the audio data or control path. Existing LAN synchronization
may continue concurrently.

## Firmware Components

### MicrophoneController

`MicrophoneController` is the single owner of microphone state. It:

- consumes debounced G0 click events;
- checks Mac sink readiness;
- starts and stops capture;
- coordinates rate fallback;
- publishes UI and Web status;
- stops capture on disconnect, unsubscribe, or transport failure.

It exposes commands and immutable status snapshots. UI code, Web handlers,
and BLE callbacks do not manipulate I2S or DMA directly.

### AudioCapture

`AudioCapture` owns PDM/I2S RX, GPIO46, GPIO43, DMA, and fixed capture buffers.
It uses the ESP-IDF I2S PDM RX API through a bounded adapter rather than
placing recording logic in `product_controller.cpp`.

The implementation must:

- disable the speaker before microphone initialization;
- allocate all DMA and working buffers before entering `LIVE`;
- read fixed 10 ms sample windows;
- avoid filesystem, HTTPS, Profile, display, and BLE host calls in the capture
  callback;
- stop DMA before releasing buffers;
- report overruns through counters rather than blocking.

### AudioCodec

`AudioCodec` encodes each 10 ms window as an independent IMA-ADPCM block.
Every block carries its own predictor and step index. Losing one notification
therefore does not corrupt later blocks.

The codec is deterministic and allocation-free. C++ and Swift implementations
share committed golden vectors.

### BleAudioTransport

`BleAudioTransport` is a bounded, lower-priority producer of GATT
notifications. HID is always higher priority. When NimBLE cannot accept an
audio frame immediately, the frame is dropped and counted; the audio path
must never wait in a way that delays a keyboard report.

The existing maximum of one BLE connection remains unchanged.

## Audio GATT Protocol

The existing Companion service UUID remains:

`7A100001-2C4D-4F20-9F20-434F44455831`

The following characteristics are added:

- Audio Data Notify:
  `7A100005-2C4D-4F20-9F20-434F44455831`
- Audio Control Write:
  `7A100006-2C4D-4F20-9F20-434F44455831`
- Audio Status Notify:
  `7A100007-2C4D-4F20-9F20-434F44455831`

All three require the current connection to be bonded and encrypted. Audio
Control also requires the existing current-Companion binding. Audio Data and
Audio Status remain inactive until the Mac subscribes.

Audio Control protocol `1.0` permits:

- protocol and capability negotiation;
- sink-ready and sink-not-ready publication;
- preferred-rate selection between 24 kHz and 16 kHz;
- statistics reset for diagnostics.

It deliberately has no remote start-capture command. G0 is the only recording
start and stop input in the first release.

Each Audio Data notification contains an eight-byte transport header followed
by one IMA-ADPCM block:

| Field | Size | Meaning |
| --- | ---: | --- |
| protocol version | 1 | `1` |
| flags | 1 | start, discontinuity, degraded-rate |
| sequence | 2 | wrapping little-endian frame sequence |
| rate code | 1 | 24 kHz or 16 kHz |
| duration | 1 | `10` milliseconds |
| payload length | 2 | little-endian ADPCM block length |

At 24 kHz, a frame contains 240 samples. The independent ADPCM block uses a
four-byte predictor/index header and 120 encoded bytes, so the complete
notification is 132 bytes. At 16 kHz it is 92 bytes. Both remain below the
common macOS negotiated ATT payload size without depending on a 517-byte MTU.

Audio Status reports:

- current microphone state;
- active sample rate;
- source overruns;
- transport drops;
- sequence discontinuities;
- last non-sensitive error code.

Counters saturate rather than wrap. The protocol never sends recorded content
through Web or LAN routes.

## G0 and Privacy State Machine

The state model is:

```text
UNAVAILABLE
READY
STARTING
LIVE_24K
LIVE_16K
STOPPING
ERROR
```

Rules:

1. Boot always begins with capture off.
2. `UNAVAILABLE` means the Mac Audio Data sink or HAL bridge is not ready.
3. A short G0 click in `READY` enters `STARTING`, then `LIVE_24K`.
4. A short G0 click in either `LIVE` state enters `STOPPING`, then `READY`.
5. A short G0 click in `UNAVAILABLE` does not start capture and displays
   `MIC: MAC NOT READY`.
6. A valid click is one debounced press-and-release lasting no more than one
   second. Button hold, repeat, and longer releases do not toggle capture.
7. BLE disconnect, Audio Data unsubscribe, sink-not-ready, Companion exit, or
   unrecoverable transport failure stops DMA and returns to `UNAVAILABLE`.
8. Reconnection returns only to `READY`. It never resumes recording.
9. A reboot never restores a previous live state.
10. After a two-second warmup, two consecutive five-second windows above one
    percent source-plus-transport loss, or any ten consecutive missing frames,
    moves the current recording session once to `LIVE_16K`. The device
    publishes the new status, and the Mac flushes the old-rate jitter buffer.
    It does not move back to 24 kHz until a later recording.
11. If the same sustained-failure rule is met at 16 kHz, the controller enters
    `ERROR` and stops capture; there is no lower automatic rate.
12. Profile, PIN, Wi-Fi, pet, Web, and Codex operations cannot start capture.

No production component records audio to disk. Tests may use generated vectors
or in-memory analysis. A temporary human-voice file is permitted only with
explicit operator action during a diagnostic run and must not be part of the
automated release path.

## Device UI and Settings

Every page status bar adds one stable microphone indicator:

- `MIC --`;
- `MIC READY`;
- red `MIC 24K`;
- orange `MIC 16K`;
- `MIC ERR`.

The indicator does not blink. On the PET page, a persistent red microphone
glyph is also visible while capture is live. The DEVICE page adds:

`MIC: READY | LIVE24 | LIVE16 | OFFLINE | ERROR`

The existing scroll model handles the additional row.

Short-lived in-page errors include:

- `MAC NOT READY`;
- `MIC INIT FAILED`;
- `BLE AUDIO BUSY`;
- `AUDIO DRIVER MISMATCH`.

Settings changes:

- add `Input Mode` for the former Keyboard/Codex G0 toggle;
- retain SAFE as the immutable option in `Keyboard Profile`;
- selecting SAFE sends HID Release All before activation.

G0 has no long-press action. Settings navigation remains device-local and
continues to work even when the active Profile is invalid. The Web console
shows microphone state, sample rate, aggregate drop rate, and last error as
read-only data. It has no capture-control endpoint.

## Mac Companion Components

### Unified CoreBluetooth ownership

The current `ProductGATTReceiver` evolves into a connection owner that
discovers and subscribes to both Unicode and audio characteristics. The product
must not start a second `CBCentralManager` or a second connection loop for
audio.

### AudioGATTReceiver

`AudioGATTReceiver`:

- validates protocol version, lengths, flags, and sequence;
- rejects malformed frames without affecting Unicode input;
- decodes IMA-ADPCM on a dedicated serial queue;
- reports gaps to the jitter buffer;
- updates bounded diagnostics.

### AudioPipeline

`AudioPipeline` provides:

- a target jitter window of 60 to 100 ms;
- silence insertion for missing late frames;
- no retransmission of stale audio;
- mono 48 kHz float output;
- bounded drift correction based on ring-buffer watermarks;
- preallocated buffers on the steady-state path.

The end-to-end p95 latency target is 250 ms or less.

### CardputerAudioBridge

The bridge connects the user process to the HAL plug-in through an authenticated
XPC control channel and a preallocated shared-memory single-producer,
single-consumer ring.

The plug-in creates the shared memory and gives a writable mapping only to an
authenticated Companion. The Core Audio side maps it read-only. Only one
producer lease may be active. The lease expires after approximately two
seconds without a valid producer heartbeat.

The Core Audio realtime callback only performs bounded atomic ring reads and
silence fill. It performs no BLE, XPC, filesystem, logging, locking, or dynamic
allocation.

## Core Audio HAL Plug-in

`CardputerCodexMicrophone.driver` uses Apple's `AudioServerPlugIn` interface.
It publishes one input-only device:

- name: `Cardputer Codex Microphone`;
- one mono input stream;
- 48 kHz;
- 32-bit float PCM;
- no output stream.

The device remains enumerated while installed. If there is no producer, the
producer lease expires, the ring underruns, or protocol versions do not match,
the device outputs digital silence.

The plug-in and Companion share one release signing identity. The XPC endpoint
validates the caller's code-signing identifier and Team ID before sharing the
ring. Development-only ad-hoc builds must be visibly marked and restricted to
the current Mac; they are not a substitute for release signing.

## Packaging and Operations

The Companion distribution adds:

- `CardputerCodexMicrophone.driver`;
- `cardputer-companion install-audio-driver`;
- `cardputer-companion uninstall-audio-driver`;
- `cardputer-companion doctor audio`.

The plug-in installs under:

`/Library/Audio/Plug-Ins/HAL/CardputerCodexMicrophone.driver`

Administrator authorization is required only for install, upgrade, and
uninstall. The supported activation path is the Apple-documented audio-service
restart or a Mac restart. Normal Companion startup does not require elevation.

`doctor audio` checks:

- installed and running plug-in version;
- Core Audio device enumeration;
- XPC authentication;
- ring creation and silence flow;
- Bluetooth audio characteristic discovery and subscription;
- negotiated protocol and sample rate.

Companion LaunchAgent startup establishes the HAL bridge and Audio GATT
subscription but never starts microphone capture.

A broadly distributable package requires Developer ID Application and
Installer signatures plus notarization. Without those credentials, the valid
deliverable is a locally installable, current-Mac development build.

## Failure Handling

- Companion restart: HAL stays enumerated and silent; after reconnection the
  Cardputer becomes `READY`, not live.
- BLE disconnect: Cardputer stops PDM/DMA; HAL outputs silence.
- Notification loss: Mac inserts silence and continues with the next
  independently decodable frame.
- NimBLE buffer pressure: audio drops; HID is not delayed.
- Companion hang: producer lease expires and the HAL ring is silenced.
- Core Audio restart: Companion recreates XPC and ring state; the device does
  not resume capture.
- Driver/Companion protocol mismatch: no ring publication, silent device, and
  explicit diagnostic status.
- Capture or encoder failure: stop the audio task and preserve BLE HID,
  Wi-Fi, Web, UI, and Companion synchronization.

## Implementation Phases and Gates

### Phase 1: protocol and feasibility

1. Commit protocol structures and C++/Swift golden vectors.
2. Capture SPM1423 speech at 24 kHz on the real Cardputer.
3. Stream to an in-memory Mac receiver without installing HAL.
4. Run ten minutes with concurrent HID.

The 24 kHz gate requires:

- audio frame loss below 1 percent;
- no contiguous audio gap over 150 ms;
- no BLE reconnect;
- HID internal queue p95 at or below 20 ms;
- no allocation failure;
- steady internal heap at least 64 KiB;
- largest internal block at least 32 KiB;
- TLS-burst internal heap at least 40 KiB;
- every measured task retaining at least 20 percent stack headroom.

If 24 kHz fails after bounded transport tuning, 16 kHz becomes the release
default. The project does not trade keyboard stability for 24 kHz.

### Phase 2: firmware product integration

Implement the four firmware components, Audio GATT, state machine, G0 input,
UI, read-only Web status, metrics, and host tests. Preserve all existing
release gates.

### Phase 3: macOS pipeline and HAL

Implement unified CoreBluetooth ownership, decoder, jitter buffer, resampler,
ring, XPC authentication, HAL device, installer commands, and doctor checks.

### Phase 4: recovery and packaging

Exercise restart, disconnect, unsubscribe, version mismatch, queue pressure,
and installation upgrade paths. Package firmware, Companion, and the plug-in
without secrets.

### Phase 5: release HIL

Run the complete existing release gate plus a 30-minute real-hardware soak
with:

- continuous microphone transport;
- real HID typing;
- Web requests;
- Codex status synchronization;
- pet synchronization;
- deliberate Companion and BLE reconnect cases.

Release acceptance requires:

- p95 capture-to-Core-Audio latency at or below 250 ms;
- total audio frame loss below 1 percent;
- no contiguous gap over 150 ms outside deliberate reconnect windows;
- 1,000 HID test events with no missing report;
- all existing heap, stack, allocation, and HID gates;
- no panic, watchdog, stack overflow, reboot, or persistent reconnect loop;
- G0 on/off visibly matching Core Audio signal and silence;
- disconnect and reboot never resuming capture automatically.

Final hardware installation uses app-only flashing at `0x20000` to preserve
PIN, Wi-Fi, Profiles, pet data, and BLE bonds. The Companion and HAL artifacts
are installed separately.

## Automated Test Boundary

Required automated coverage includes:

- C++/Swift ADPCM golden-vector parity;
- frame header and malformed-input tests;
- sequence wrap, loss, duplication, and discontinuity tests;
- microphone state-machine tests;
- G0 click/hold/repeat tests;
- no remote-start command or route;
- GATT encryption, binding, and subscription predicates;
- audio-drop-before-HID priority tests;
- jitter-buffer and silence-fill tests;
- rate fallback and no-oscillation tests;
- resampler and ring-buffer tests;
- single-producer lease tests;
- HAL property, format, and silence tests;
- driver/Companion version mismatch tests;
- install, upgrade, doctor, and uninstall manifest tests;
- all existing firmware, Web, Companion, sanitizer, partition, and packaging
  checks.

Automation cannot replace physical checks of microphone signal quality,
macOS microphone enumeration, real application input, G0 feedback, BLE
coexistence, or sustained resource behavior.

## Explicit Non-Goals

- native HFP or Bluetooth LE Audio enumeration;
- Wi-Fi audio in the first release;
- Cardputer-Adv support;
- stereo or 48 kHz capture on Cardputer;
- speaker output, full-duplex calls, acoustic echo cancellation, or sidetone;
- wake words, offline speech recognition, or automatic text injection;
- audio recording, history, or cloud upload;
- remote Web or LAN recording activation;
- automatic change of the macOS default input device;
- automatic approval of application microphone permissions;
- multiple Cardputers or Companion producer processes feeding one virtual
  device; Core Audio may still serve the one virtual input to normal clients;
- universal unsigned distribution.

## Primary References

- M5Stack Cardputer hardware and SPM1423 pin map:
  <https://docs.m5stack.com/en/core/Cardputer>
- M5Stack Cardputer microphone example:
  <https://docs.m5stack.com/en/arduino/m5cardputer/mic>
- ESP-IDF ESP32-S3 PDM/I2S RX documentation:
  <https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/i2s.html>
- ESP-IDF Bluetooth Classic controller support table:
  <https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32/api-guides/classic-bt/overview.html>
- Bluetooth LE Audio requirement for LE isochronous channels:
  <https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-guides/esp-ble-audio/ble-audio-introduction.html>
- Apple Audio Server Driver Plug-in guidance:
  <https://developer.apple.com/documentation/coreaudio/creating-an-audio-server-driver-plug-in>
- Apple AudioServerPlugIn Mach service guidance:
  <https://developer.apple.com/library/archive/qa/qa1811/_index.html>
