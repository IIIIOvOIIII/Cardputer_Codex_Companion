# Cardputer BLE Microphone Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a G0-controlled Cardputer speech microphone whose encrypted BLE audio is published on macOS as the system input device `Cardputer Codex Microphone`.

**Architecture:** The firmware captures SPM1423 PDM audio, encodes independent 10 ms IMA-ADPCM blocks, and sends them on three new characteristics in the existing encrypted Companion GATT service without delaying HID. The existing Mac Companion becomes the sole CoreBluetooth owner, decodes and buffers audio, writes 48 kHz mono float samples to a shared ring, and feeds an input-only `AudioServerPlugIn` HAL driver.

**Tech Stack:** ESP-IDF 5.5.4, C++20, M5Unified 0.2.17, NimBLE/ESP HID, Swift 6, CoreBluetooth, Core Audio `AudioServerPlugIn`, XPC, C17 atomics, CMake/CTest, SwiftPM/XCTest, Python/pytest, macOS 14+.

## Global Constraints

- Start execution in a new `feat/ble-microphone` worktree based exactly on commit `31fea44`; do not implement on `main`.
- Initial hardware support is SPM1423 Cardputer/Cardputer v1.1 only: GPIO46 data, GPIO43 clock, 8 MiB flash, no PSRAM.
- Audio payloads must use the existing encrypted BLE connection. Wi-Fi must not carry audio.
- G0 is the only recording start/stop input. Web, LAN, Companion, and GATT control commands must not remotely start capture.
- A valid G0 click is one debounced press-and-release lasting no more than one second. Longer holds do nothing.
- Preferred capture is 24 kHz, 16-bit, mono; 16 kHz is the only fallback. Core Audio output is 48 kHz mono float.
- Audio frames are independent 10 ms IMA-ADPCM blocks. HID always has priority; audio drops instead of blocking.
- Boot, reconnect, Companion restart, and Core Audio restart must never resume recording automatically.
- Production code must not persist, upload, or log audio content.
- Preserve PIN, Wi-Fi, Profiles, pets, BLE bonds, Web auth, Unicode injection, Codex telemetry, and five-page UI behavior.
- Preserve the resource gates: steady internal heap `>= 65536`, largest internal block `>= 32768`, TLS-burst internal heap `>= 40960`, zero allocation failures, HID internal p95 `<= 20000 us`, and at least 20 percent stack headroom.
- Target versions are firmware `1.1.0`, Companion audio protocol `1.0`, and HAL plug-in `1.0.0`.
- A general release requires Developer ID Application/Installer signing and notarization. Without those credentials, label the artifact as a current-Mac development build.
- Commit after every task using `<type>: <subject>`. Do not push until a remote exists.

## File and Module Map

### Protocol

- `protocol/audio-v1/README.md`: wire contract, state/status codes, no-remote-start rule.
- `protocol/audio-v1/fixtures/audio-v1.json`: shared packet and codec vectors.
- `tools/product/validate_audio_vectors.py`: fixture and packet-size validator.

### Firmware

- `firmware/main/product/audio_protocol.hpp/.cpp`: packet headers, parser, serializer, status/control messages.
- `firmware/main/product/ima_adpcm.hpp/.cpp`: allocation-free block encoder.
- `firmware/main/product/microphone_state.hpp/.cpp`: pure G0/readiness/fallback state machine.
- `firmware/main/product/audio_capture.hpp/.cpp`: capture interface and ESP-IDF PDM implementation.
- `firmware/main/product/microphone_controller.hpp/.cpp`: capture/encode/send orchestration and immutable metrics.
- `firmware/main/probe/ble_services.hpp/.cpp`: Audio Data, Control, and Status GATT ownership.
- Existing product controller, UI, Settings, Web, resource metrics, CMake, and tests: thin integration only.

### Mac Companion

- `companion/Sources/ProductAudio/AudioProtocol.swift`: strict packet/status/control parsing.
- `companion/Sources/ProductAudio/IMAADPCM.swift`: block decoder.
- `companion/Sources/ProductAudio/AudioJitterBuffer.swift`: sequence ordering, gap accounting, silence insertion.
- `companion/Sources/ProductAudio/AdaptiveResampler.swift`: 16/24 kHz to 48 kHz mono float with bounded drift correction.
- `companion/Sources/ProductAudio/AudioPipeline.swift`: decode-to-ring orchestration and diagnostics.
- `companion/Sources/ProductAudio/AudioDriverConnection.swift`: authenticated XPC client and producer lease.
- `companion/Sources/ProductGATT/ProductGATTReceiver.swift`: one CoreBluetooth owner for Unicode and audio.

### Shared ring and HAL driver

- `companion/Sources/CAudioBridge/include/CardputerAudioRing.h`: shared C ABI and fixed ring layout.
- `companion/Sources/CAudioBridge/CardputerAudioRing.c`: C17 atomic ring implementation.
- `companion/AudioDriver/CardputerAudioDriver.c`: `AudioServerPlugInDriverInterface` and property dispatch.
- `companion/AudioDriver/CardputerAudioDevice.c/.h`: input-device state and realtime render adapter.
- `companion/AudioDriver/CardputerAudioIPC.c/.h`: launchd bridge server, caller validation, shared FD, lease.
- `companion/AudioHelper/CardputerAudioBridgeMain.c`: root-owned launchd service entry point.
- `companion/AudioHelper/com.lynx.cardputer-audio-bridge.plist`: system Mach service registration.
- `companion/AudioDriver/Info.plist`: HAL bundle and permission to look up the bridge Mach service.
- `scripts/build_audio_driver.sh`: deterministic driver bundle build.
- `scripts/install_audio_driver.sh`: root-only atomic install/uninstall helper used by the CLI.

---

### Task 1: Audio v1 Protocol Contract

**Files:**
- Create: `protocol/audio-v1/README.md`
- Create: `protocol/audio-v1/fixtures/audio-v1.json`
- Create: `tools/product/validate_audio_vectors.py`
- Create: `tools/product/tests/test_audio_vectors.py`
- Create: `firmware/main/product/audio_protocol.hpp`
- Create: `firmware/main/product/audio_protocol.cpp`
- Create: `firmware/test/host/test_audio_protocol.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/main/CMakeLists.txt`

**Interfaces:**
- Produces: `AudioSampleRate`, `AudioFrameHeader`, `AudioFrameView`, `AudioProtocolError`, `encode_audio_packet(...)`, `decode_audio_packet(...)`, `encode_audio_control(...)`, and `decode_audio_control(...)`.
- Consumes: no new project interface.

- [ ] **Step 1: Create the failing fixture and validator tests**

Add fixture cases for a 132-byte 24 kHz packet, a 92-byte 16 kHz packet,
sequence `65535 -> 0`, malformed length, unsupported version, and an attempted
remote-start control opcode that must be rejected.

```python
def test_audio_fixture_has_no_remote_start_opcode(audio_fixture):
    opcodes = {item["name"] for item in audio_fixture["control_opcodes"]}
    assert opcodes == {
        "hello", "sink_ready", "sink_not_ready",
        "set_preferred_rate", "reset_statistics",
    }
```

- [ ] **Step 2: Run the fixture test and verify RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q tools/product/tests/test_audio_vectors.py
```

Expected: FAIL because the fixture and validator do not exist.

- [ ] **Step 3: Add C++ protocol tests before implementation**

Test exact UUID suffixes `0005`, `0006`, `0007`, little-endian fields,
132/92-byte packet limits, invalid flags, invalid rate, mismatched payload
length, and remote-start rejection.

```cpp
std::array<uint8_t, 132> packet{};
std::array<uint8_t, 124> payload{};
size_t written = 0;
assert(encode_audio_packet(
    {.version = 1, .flags = kAudioFlagStart, .sequence = 7,
     .rate = AudioSampleRate::hz24000, .duration_ms = 10},
    payload, packet, &written) == AudioProtocolError::none);
assert(written == 132);
```

- [ ] **Step 4: Run the C++ test and verify RED**

Run:

```bash
cmake -S firmware/test/host -B build/audio-host
cmake --build build/audio-host --target test_audio_protocol -j
ctest --test-dir build/audio-host -R '^audio_protocol$' --output-on-failure
```

Expected: configuration or compile failure because the protocol API is absent.

- [ ] **Step 5: Implement the minimal strict protocol**

Use fixed arrays/spans only. Define the transport header as:

```cpp
struct AudioFrameHeader {
  uint8_t version = 1;
  uint8_t flags = 0;
  uint16_t sequence = 0;
  AudioSampleRate rate = AudioSampleRate::hz24000;
  uint8_t duration_ms = 10;
  uint16_t payload_length = 0;
};
```

Reject unknown versions, bits outside the defined flag mask, durations other
than 10 ms, payloads other than 124 or 84 bytes for the matching rate, and all
unknown control opcodes.

- [ ] **Step 6: Run targeted protocol verification**

Run:

```bash
PYTHONPATH=. uv run pytest -q tools/product/tests/test_audio_vectors.py
cmake --build build/audio-host --target test_audio_protocol -j
ctest --test-dir build/audio-host -R '^audio_protocol$' --output-on-failure
```

Expected: all tests PASS.

- [ ] **Step 7: Commit the protocol**

```bash
git add protocol/audio-v1 tools/product firmware/main/product/audio_protocol.* \
  firmware/test/host/test_audio_protocol.cpp firmware/test/host/CMakeLists.txt \
  firmware/main/CMakeLists.txt
git commit -m "feat: define BLE audio protocol"
```

### Task 2: Cross-Language IMA-ADPCM Codec

**Files:**
- Create: `firmware/main/product/ima_adpcm.hpp`
- Create: `firmware/main/product/ima_adpcm.cpp`
- Create: `firmware/test/host/test_ima_adpcm.cpp`
- Create: `companion/Sources/ProductAudio/AudioProtocol.swift`
- Create: `companion/Sources/ProductAudio/IMAADPCM.swift`
- Create: `companion/Tests/ProductAudioTests/AudioProtocolTests.swift`
- Create: `companion/Tests/ProductAudioTests/IMAADPCMTests.swift`
- Modify: `companion/Package.swift`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/main/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 packet constants and `protocol/audio-v1/fixtures/audio-v1.json`.
- Produces: C++ `ima_adpcm_encode_block(samples, output, written)` and Swift `IMAADPCM.decode(block:sampleCount:)`.

- [ ] **Step 1: Add failing C++ codec tests**

Cover silence, positive and negative ramps, clipping edges, exactly 240 samples
to 124 bytes, exactly 160 samples to 84 bytes, undersized output, and invalid
sample counts.

```cpp
std::array<int16_t, 240> silence{};
std::array<uint8_t, 124> encoded{};
size_t written = 0;
assert(ima_adpcm_encode_block(silence, encoded, &written) ==
       ImaAdpcmError::none);
assert(written == 124);
```

- [ ] **Step 2: Add failing Swift fixture and decode tests**

`AudioWireFrame(data:)` must reject bad protocol fields before invoking the
codec. `IMAADPCM.decode` must produce the sample arrays recorded in the shared
fixture within the exact integer values defined there.

```swift
let samples = try IMAADPCM.decode(block: vector.block, sampleCount: 240)
XCTAssertEqual(samples, vector.decodedSamples)
```

- [ ] **Step 3: Run both suites and verify RED**

Run:

```bash
cmake --build build/audio-host --target test_ima_adpcm -j
ctest --test-dir build/audio-host -R '^ima_adpcm$' --output-on-failure
swift test --package-path companion --filter ProductAudioTests
```

Expected: compile/configuration failure because ProductAudio and codec APIs do
not exist.

- [ ] **Step 4: Implement the codec and Swift target**

Use the standard IMA 89-entry step table and 16-entry index adjustment table.
The first sample is the little-endian predictor; byte 2 is the step index;
byte 3 is zero; remaining samples are low-nibble then high-nibble. Clamp the
predictor and step index after every nibble.

Add `ProductAudio` as a SwiftPM library and `ProductAudioTests` as a test
target with the fixture directory copied as a test resource.

- [ ] **Step 5: Verify codec parity and sanitizers**

Run:

```bash
cmake --build build/audio-host --target test_ima_adpcm -j
ctest --test-dir build/audio-host -R '^ima_adpcm$' --output-on-failure
swift test --package-path companion --filter ProductAudioTests
cmake -S firmware/test/host -B build/audio-host-sanitized \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/audio-host-sanitized --target test_ima_adpcm -j
ctest --test-dir build/audio-host-sanitized -R '^ima_adpcm$' --output-on-failure
```

Expected: all tests PASS with no sanitizer report.

- [ ] **Step 6: Commit the codec**

```bash
git add firmware/main/product/ima_adpcm.* firmware/test/host \
  companion/Package.swift companion/Sources/ProductAudio \
  companion/Tests/ProductAudioTests protocol/audio-v1/fixtures/audio-v1.json
git commit -m "feat: add cross-language ADPCM codec"
```

### Task 3: Microphone Privacy and Fallback State Machine

**Files:**
- Create: `firmware/main/product/microphone_state.hpp`
- Create: `firmware/main/product/microphone_state.cpp`
- Create: `firmware/test/host/test_microphone_state.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/main/CMakeLists.txt`

**Interfaces:**
- Consumes: `AudioSampleRate` from Task 1.
- Produces: `MicrophoneState`, `MicrophoneEvent`, `MicrophoneCommand`, `MicrophoneTransition`, and `MicrophoneStateMachine::apply(...)`.

- [ ] **Step 1: Write failing transition tests**

Test boot-off, sink-ready, valid click, start success, click-to-stop,
disconnect-to-unavailable, reconnect-without-resume, reboot-without-resume,
hold/repeat ignored, two bad five-second windows causing one 24-to-16 kHz
fallback, jitter-buffer discontinuity command on fallback, and sustained
16 kHz failure entering error and stop.

```cpp
MicrophoneStateMachine state;
assert(state.apply({.kind = MicrophoneEventKind::sink_ready}).state ==
       MicrophoneState::ready);
assert(state.apply({.kind = MicrophoneEventKind::g0_click}).command ==
       MicrophoneCommand::start_capture_24k);
```

- [ ] **Step 2: Run the test and verify RED**

Run:

```bash
cmake --build build/audio-host --target test_microphone_state -j
ctest --test-dir build/audio-host -R '^microphone_state$' --output-on-failure
```

Expected: compile failure because the state machine is absent.

- [ ] **Step 3: Implement the pure state machine**

Keep time and loss-window calculation outside the class. Feed explicit events:

```cpp
enum class MicrophoneEventKind : uint8_t {
  sink_ready, sink_lost, g0_click, g0_ignored,
  capture_started, capture_stopped, loss_window_good,
  loss_window_bad, fatal_error, reset,
};
```

The state machine returns commands and never performs I2S, BLE, UI, or logging.

- [ ] **Step 4: Run targeted tests**

Run:

```bash
cmake --build build/audio-host --target test_microphone_state -j
ctest --test-dir build/audio-host -R '^microphone_state$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit the state model**

```bash
git add firmware/main/product/microphone_state.* \
  firmware/test/host/test_microphone_state.cpp \
  firmware/test/host/CMakeLists.txt firmware/main/CMakeLists.txt
git commit -m "feat: model microphone privacy state"
```

### Task 4: SPM1423 PDM Capture Adapter

**Files:**
- Create: `firmware/main/product/audio_capture.hpp`
- Create: `firmware/main/product/audio_capture.cpp`
- Create: `firmware/test/host/test_audio_capture.cpp`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/main/idf_component.yml`

**Interfaces:**
- Consumes: `AudioSampleRate`.
- Produces: `IAudioCapture`, `AudioCaptureConfig`, `AudioCaptureResult`, `PdmAudioCapture`, and `make_product_audio_capture()`.

- [ ] **Step 1: Add failing host adapter tests**

Use a fake capture backend to prove exact sample counts, fixed 10 ms windows,
start-before-read, stop idempotence, overrun accounting, and no second
allocation after start.

```cpp
FakeCapture capture;
assert(capture.start({.rate = AudioSampleRate::hz24000}) ==
       AudioCaptureResult::ok);
std::array<int16_t, 240> frame{};
assert(capture.read_frame(frame) == AudioCaptureResult::ok);
```

- [ ] **Step 2: Run the host test and verify RED**

Run:

```bash
cmake --build build/audio-host --target test_audio_capture -j
ctest --test-dir build/audio-host -R '^audio_capture$' --output-on-failure
```

Expected: compile failure because the interface does not exist.

- [ ] **Step 3: Implement the ESP-IDF PDM adapter**

Under `ESP_PLATFORM`, configure I2S PDM RX with GPIO43 clock, GPIO46 input,
16-bit mono, and hardware PDM-to-PCM conversion. Before `i2s_channel_enable`,
call the M5 speaker shutdown path and verify it is not enabled. Preallocate two
DMA-facing frame buffers and one encoder input frame.

```cpp
i2s_pdm_rx_config_t config{
    .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(rate_hz),
    .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {.clk = GPIO_NUM_43, .din = GPIO_NUM_46},
};
```

Use the exact ESP-IDF 5.5.4 field layout found in the pinned SDK headers; do
not copy a newer documentation-only initializer if its ABI differs.

- [ ] **Step 4: Verify host and target compilation**

Run:

```bash
cmake --build build/audio-host --target test_audio_capture -j
ctest --test-dir build/audio-host -R '^audio_capture$' --output-on-failure
scripts/phase0/idf.sh -C firmware reconfigure
scripts/phase0/idf.sh -C firmware build
```

Expected: host test PASS and ESP32-S3 target build PASS.

- [ ] **Step 5: Commit capture support**

```bash
git add firmware/main/product/audio_capture.* firmware/main/CMakeLists.txt \
  firmware/test/host/test_audio_capture.cpp firmware/test/host/CMakeLists.txt \
  firmware/main/idf_component.yml
git commit -m "feat: capture Cardputer PDM audio"
```

### Task 5: Encrypted Audio GATT and HID-Priority Transport

**Files:**
- Modify: `firmware/main/probe/ble_services.hpp`
- Modify: `firmware/main/probe/ble_services.cpp`
- Create: `firmware/main/product/ble_audio_transport.hpp`
- Create: `firmware/main/product/ble_audio_transport.cpp`
- Create: `firmware/test/host/test_ble_audio_transport.cpp`
- Modify: `firmware/test/host/test_ble_manifest.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/main/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 Audio Data/Control/Status messages.
- Produces: `AudioControlHandler`, `AudioSinkStateHandler`, `set_audio_control_handler(...)`, `set_audio_sink_state_handler(...)`, `ble_audio_sink_ready()`, `notify_audio_frame(...)`, `notify_audio_status(...)`, and `BleAudioTransport::try_send(...)`.

- [ ] **Step 1: Extend manifest tests in RED**

Assert exact UUIDs, encryption flags, current-Companion requirement for control,
separate subscriptions from Unicode and HID readiness, and no start opcode.

```cpp
const auto uuids = companion_gatt_uuids();
assert(uuids.audio_data == uuid_from_text(
    "7A100005-2C4D-4F20-9F20-434F44455831"));
assert(manifest.audio_control_requires_current_companion);
```

- [ ] **Step 2: Add failing transport-priority tests**

Use a fake notifier with configurable `ESP_ERR_NO_MEM` behavior. Prove one
attempt per frame, drop-and-count on pressure, no retry sleep, and a HID-ready
predicate that is independent of Audio Data subscription.

- [ ] **Step 3: Run tests and verify RED**

Run:

```bash
cmake --build build/audio-host --target test_ble_manifest test_ble_audio_transport -j
ctest --test-dir build/audio-host \
  -R '^(ble_manifest|ble_audio_transport)$' --output-on-failure
```

Expected: assertions or compile failures because audio characteristics are
absent.

- [ ] **Step 4: Implement characteristics and bounded sending**

Add Audio Data/Control/Status to the existing custom service. Use encrypted
notify/write flags. Track three subscriptions independently:

```cpp
struct BleAudioSubscriptionState {
  bool data_notify = false;
  bool status_notify = false;
  bool companion_bound = false;
  bool encrypted = false;
};
```

Only `data_notify && companion_bound && encrypted` produces sink-ready.
`BleAudioTransport::try_send` calls the notifier once and converts transient
buffer failure to a drop metric.

- [ ] **Step 5: Run host and ESP-IDF builds**

Run:

```bash
cmake --build build/audio-host --target test_ble_manifest test_ble_audio_transport -j
ctest --test-dir build/audio-host \
  -R '^(ble_manifest|ble_audio_transport)$' --output-on-failure
scripts/phase0/idf.sh -C firmware build
```

Expected: PASS.

- [ ] **Step 6: Commit GATT transport**

```bash
git add firmware/main/probe/ble_services.* \
  firmware/main/product/ble_audio_transport.* firmware/test/host \
  firmware/main/CMakeLists.txt
git commit -m "feat: add encrypted BLE audio transport"
```

### Task 6: Firmware Microphone Controller and G0 Runtime

**Files:**
- Create: `firmware/main/product/microphone_controller.hpp`
- Create: `firmware/main/product/microphone_controller.cpp`
- Create: `firmware/test/host/test_microphone_controller.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/main/probe/resource_metrics.hpp`
- Modify: `firmware/main/probe/resource_metrics.cpp`
- Modify: `firmware/test/host/test_resource_metrics.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/main/CMakeLists.txt`

**Interfaces:**
- Consumes: `IAudioCapture`, encoder, `BleAudioTransport`, and state machine.
- Produces: `MicrophoneController::on_sink_ready`, `on_g0_click`,
  `on_g0_ignored`, `run_once`, `stop_for_disconnect`, and
  `MicrophoneSnapshot snapshot() const`.

- [ ] **Step 1: Add failing controller tests**

Use fake capture and transport objects to prove:

- unavailable click never starts capture;
- ready click starts 24 kHz;
- 240 samples become one 132-byte packet;
- sequence wraps correctly;
- a send failure increments audio drops without blocking;
- two bad windows trigger one 16 kHz restart and discontinuity;
- 16 kHz sustained failure stops in error;
- disconnect stops capture and clears queued audio;
- reconnect does not restart;
- G0 holds longer than one second are ignored.

- [ ] **Step 2: Run the controller test and verify RED**

Run:

```bash
cmake --build build/audio-host --target test_microphone_controller -j
ctest --test-dir build/audio-host -R '^microphone_controller$' --output-on-failure
```

Expected: compile failure because the controller is absent.

- [ ] **Step 3: Implement fixed-task orchestration**

Create one static audio task and one fixed-depth frame queue. `run_once` reads,
encodes, and attempts one notification. Do not call display, Web, NVS, Profile,
or HTTPS from the audio task. Publish snapshots through atomics or a short
dedicated mutex.

Replace current G0 mode handling in `ui_task`:

```cpp
if (M5.BtnA.wasReleased()) {
  const uint32_t held_ms = /* measured from press edge */;
  if (held_ms <= 1000) {
    g_microphone.on_g0_click();
  } else {
    g_microphone.on_g0_ignored();
  }
}
```

Remove `release_and_set_mode` calls from G0. Do not yet add Settings or display
copy; that is Task 13.

- [ ] **Step 4: Extend runtime metrics**

Add capture and audio-task stack handles plus:

```cpp
struct AudioRuntimeMetrics {
  uint32_t captured_frames;
  uint32_t source_overruns;
  uint32_t transport_drops;
  uint32_t fallback_count;
};
```

Preserve all existing threshold values and increase fixed task-metric capacity
only by the exact new measured tasks.

- [ ] **Step 5: Run targeted and target tests**

Run:

```bash
cmake --build build/audio-host \
  --target test_microphone_controller test_resource_metrics -j
ctest --test-dir build/audio-host \
  -R '^(microphone_controller|resource_metrics)$' --output-on-failure
scripts/phase0/idf.sh -C firmware build
```

Expected: PASS.

- [ ] **Step 6: Commit the runtime**

```bash
git add firmware/main/product/microphone_controller.* \
  firmware/main/product/product_controller.cpp \
  firmware/main/probe/resource_metrics.* firmware/test/host \
  firmware/main/CMakeLists.txt
git commit -m "feat: run G0 microphone capture"
```

### Task 7: Mac Audio Decode, Jitter, and Resampling

**Files:**
- Create: `companion/Sources/ProductAudio/AudioJitterBuffer.swift`
- Create: `companion/Sources/ProductAudio/AdaptiveResampler.swift`
- Create: `companion/Sources/ProductAudio/AudioPipeline.swift`
- Create: `companion/Tests/ProductAudioTests/AudioJitterBufferTests.swift`
- Create: `companion/Tests/ProductAudioTests/AdaptiveResamplerTests.swift`
- Create: `companion/Tests/ProductAudioTests/AudioPipelineTests.swift`
- Modify: `companion/Package.swift`

**Interfaces:**
- Consumes: Task 2 `AudioWireFrame` and `IMAADPCM`.
- Produces: `AudioJitterBuffer`, `AdaptiveResampler`, `AudioPipeline`,
  `AudioPipelineMetrics`, and `AudioSampleSink`.

- [ ] **Step 1: Add failing jitter-buffer tests**

Cover in-order frames, one missing frame producing exactly 10 ms of silence,
duplicates ignored, late packets ignored, sequence wrap, start/discontinuity
flush, 60–100 ms target depth, and no retransmission.

- [ ] **Step 2: Add failing resampler tests**

For 24 kHz input, one second produces 48,000 samples. For 16 kHz, one second
also produces 48,000. Silence remains bit-exact zero. Watermark correction is
bounded to `+/-500 ppm` and never changes channel count.

```swift
let output = resampler.convert(input, sourceRate: 24_000)
XCTAssertEqual(output.count, 48_000)
XCTAssertEqual(output.max()!, 0, accuracy: 0)
```

- [ ] **Step 3: Add failing pipeline tests**

Inject encoded fixture frames and assert decoded sample count, gap metrics,
rate-change flush, sink write sizes, and no audio-content logging callback.

- [ ] **Step 4: Run tests and verify RED**

Run:

```bash
swift test --package-path companion --filter ProductAudioTests
```

Expected: compile failures for missing jitter, resampler, and pipeline types.

- [ ] **Step 5: Implement bounded pipeline**

Use fixed-capacity arrays/deques with explicit maximum frames. Decode and
resample on a dedicated serial queue, never on a Core Audio callback. Define:

```swift
public protocol AudioSampleSink: AnyObject {
    func write(samples: UnsafeBufferPointer<Float>) -> Int
    func reset()
}
```

The pipeline inserts zeroes for missing frames and resets on start,
discontinuity, or sample-rate change.

- [ ] **Step 6: Verify Swift tests**

Run:

```bash
swift test --package-path companion --filter ProductAudioTests
```

Expected: PASS.

- [ ] **Step 7: Commit the Mac audio core**

```bash
git add companion/Sources/ProductAudio companion/Tests/ProductAudioTests \
  companion/Package.swift
git commit -m "feat: add Mac audio decode pipeline"
```

### Task 8: Unified CoreBluetooth Receiver and Feasibility Probe

**Files:**
- Modify: `companion/Sources/ProductGATT/ProductGATTReceiver.swift`
- Create: `companion/Sources/ProductGATT/ProductGATTConnection.swift`
- Create: `companion/Tests/ProductGATTTests/AudioGATTReceiverTests.swift`
- Modify: `companion/Package.swift`
- Modify: `companion/Sources/cardputer-companion/Configuration.swift`
- Modify: `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Create: `scripts/product/run_audio_feasibility_hil.py`
- Create: `tools/product/tests/test_audio_feasibility_hil.py`

**Interfaces:**
- Consumes: Task 1 UUIDs and Task 7 `AudioPipeline`.
- Produces: one `ProductGATTConnection`, `ProductGATTReceiver.start(audioSink:)`,
  sink-ready/control writes, Audio Data/Status subscriptions, and
  `cardputer-companion audio-probe --duration 600 --metrics PATH`.

- [ ] **Step 1: Add failing CoreBluetooth contract tests**

Extract pure discovery/reconnect decisions so tests prove:

- one central manager and one peripheral;
- Unicode and Audio Data/Status are discovered together;
- BIND1 completes before sink-ready;
- sink-not-ready is written before intentional shutdown;
- disconnect clears audio subscription and pipeline;
- audio parse errors do not disable Unicode.

- [ ] **Step 2: Add failing CLI and HIL parser tests**

The probe accepts only a bounded duration from 10 to 1800 seconds and writes
metrics JSON containing counts and rates, never PCM/ADPCM bytes.

```python
assert set(report) >= {
    "duration_seconds", "captured_frames", "received_frames",
    "source_overruns", "transport_drops", "sequence_gaps",
    "max_gap_ms", "sample_rate_hz",
}
assert "audio" not in report
```

- [ ] **Step 3: Run tests and verify RED**

Run:

```bash
swift test --package-path companion --filter ProductGATTTests
PYTHONPATH=. uv run pytest -q tools/product/tests/test_audio_feasibility_hil.py
```

Expected: FAIL because unified audio discovery and probe command are absent.

- [ ] **Step 4: Refactor to one BLE owner**

Move scanning, connection, discovery, reconnect, and control writes into
`ProductGATTConnection`. Keep Unicode reassembly and AudioPipeline consumers
separate. Do not create a second `CBCentralManager`.

- [ ] **Step 5: Implement metrics-only audio probe**

`audio-probe` waits for the physical G0 start, consumes audio into an in-memory
null sink, prints live non-content metrics, and writes a final JSON report.
The Python HIL runner captures serial resource lines, starts the probe, and
evaluates the exact Phase 1 thresholds.

- [ ] **Step 6: Run targeted tests and release build**

Run:

```bash
swift test --package-path companion --filter ProductGATTTests
PYTHONPATH=. uv run pytest -q tools/product/tests/test_audio_feasibility_hil.py
scripts/build_companion.sh
dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion --version
```

Expected: PASS and a runnable app bundle.

- [ ] **Step 7: Commit unified BLE receive**

```bash
git add companion scripts/product/run_audio_feasibility_hil.py \
  tools/product/tests/test_audio_feasibility_hil.py
git commit -m "feat: receive BLE microphone audio on Mac"
```

### Task 9: Real-Hardware 24 kHz Feasibility Gate

**Files:**
- Modify: `docs/2026-07-26-cardputer-ble-microphone_PROGRESS.md`
- Create: `docs/validation/cardputer-ble-audio-feasibility.md`
- Conditionally modify: `firmware/main/product/microphone_state.hpp`
- Conditionally modify: tests that assert the preferred release rate

**Interfaces:**
- Consumes: Tasks 4–8 target firmware, probe, metrics, and existing hardware.
- Produces: a recorded go/no-go decision for 24 kHz or a committed 16 kHz
  release default.

- [ ] **Step 1: Run all pre-flash checks**

Run:

```bash
PYTHONPATH=. uv run pytest -q
cmake --build build/audio-host -j
ctest --test-dir build/audio-host --output-on-failure
swift test --package-path companion
scripts/phase0/idf.sh -C firmware build
```

Expected: PASS before touching the device.

- [ ] **Step 2: Flash the app partition only**

Resolve exactly one Cardputer serial path, record it, then write only:

```bash
python -m esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX \
  --before default_reset --after hard_reset \
  write_flash 0x20000 firmware/build/cardputer_codex_companion.bin
```

Expected: esptool write verification succeeds and existing PIN/Wi-Fi/Profile/
pet/bond state remains present.

- [ ] **Step 3: Run the ten-minute 24 kHz gate**

Run the probe and HIL runner while typing real HID events:

```bash
PYTHONPATH=. uv run python scripts/product/run_audio_feasibility_hil.py \
  --port /dev/cu.usbmodemXXXX \
  --companion dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  --duration 600 \
  --output build/hil/audio-feasibility.json
```

Expected:

- loss below 1 percent;
- no gap over 150 ms;
- no BLE reconnect;
- HID internal p95 at or below 20 ms;
- all heap, largest-block, TLS-burst, allocation, and stack gates pass.

- [ ] **Step 4: Apply the gate decision**

If all criteria pass, retain 24 kHz as preferred. If bounded tuning still
fails, change only the release default to 16 kHz, update the matching tests,
rerun the same ten-minute gate at 16 kHz, and record the 24 kHz evidence as a
rejected release mode. Do not weaken any threshold.

- [ ] **Step 5: Verify no regression after the decision**

Run:

```bash
PYTHONPATH=. uv run pytest -q
ctest --test-dir build/audio-host --output-on-failure
swift test --package-path companion
scripts/phase0/idf.sh -C firmware build
```

Expected: PASS.

- [ ] **Step 6: Commit feasibility evidence**

```bash
git add docs/validation/cardputer-ble-audio-feasibility.md \
  docs/2026-07-26-cardputer-ble-microphone_PROGRESS.md \
  firmware/main/product/microphone_state.hpp firmware/test/host
git commit -m "docs: record BLE audio feasibility gate"
```

### Task 10: Cross-Process Atomic Audio Ring

**Files:**
- Create: `companion/Sources/CAudioBridge/include/CardputerAudioRing.h`
- Create: `companion/Sources/CAudioBridge/CardputerAudioRing.c`
- Create: `companion/Tests/CAudioBridgeTests/CardputerAudioRingTests.swift`
- Create: `companion/Sources/ProductAudio/SharedAudioRing.swift`
- Create: `companion/Tests/ProductAudioTests/SharedAudioRingTests.swift`
- Create: `scripts/test_audio_ring.sh`
- Modify: `companion/Package.swift`

**Interfaces:**
- Consumes: Task 7 `AudioSampleSink`.
- Produces: C functions `cardputer_audio_ring_initialize`,
  `cardputer_audio_ring_write`, `cardputer_audio_ring_read`,
  `cardputer_audio_ring_reset`, `cardputer_audio_ring_heartbeat`, and Swift
  `SharedAudioRing`.

- [ ] **Step 1: Add failing C-ABI ring tests**

Use Swift XCTest against the imported C API to cover ABI magic/version, exact
16,384-frame capacity, wraparound, partial write/read, overflow dropping newest
input, underflow zero-fill, reset, producer heartbeat, and concurrent
single-producer/single-consumer stress.

```c
CardputerAudioRing ring;
cardputer_audio_ring_initialize(&ring);
float input[480] = {0.25f};
assert(cardputer_audio_ring_write(&ring, input, 480) == 480);
```

- [ ] **Step 2: Add failing Swift mapping tests**

Create an anonymous temporary FD, `ftruncate` it to
`sizeof(CardputerAudioRing)`, map it, write 480 samples through Swift, and read
them through the C API.

- [ ] **Step 3: Run tests and verify RED**

Run:

```bash
swift test --package-path companion --filter CAudioBridgeTests
swift test --package-path companion --filter ProductAudioTests.SharedAudioRingTests
```

Expected: target/type failures because the ring does not exist.

- [ ] **Step 4: Implement fixed C17 atomic ring**

The shared header contains no Swift/Foundation types:

```c
#define CARDPUTER_AUDIO_RING_MAGIC 0x43414D49u
#define CARDPUTER_AUDIO_RING_VERSION 1u
#define CARDPUTER_AUDIO_RING_CAPACITY 16384u
```

Use `_Atomic uint64_t` monotonic read/write counters with acquire/release
ordering. Realtime read must copy available frames and zero-fill the remainder
without locks or allocation.

`scripts/test_audio_ring.sh` compiles the C implementation with a minimal C17
smoke main under AddressSanitizer and UndefinedBehaviorSanitizer, then executes
it. This catches C ABI and atomic memory errors independently of Swift.

- [ ] **Step 5: Verify ring and sanitizer behavior**

Run:

```bash
swift test --package-path companion --filter CAudioBridgeTests
swift test --package-path companion --filter ProductAudioTests.SharedAudioRingTests
scripts/test_audio_ring.sh
```

Expected: PASS.

- [ ] **Step 6: Commit the ring**

```bash
git add companion/Sources/CAudioBridge companion/Sources/ProductAudio \
  companion/Tests/CAudioBridgeTests companion/Tests/ProductAudioTests \
  companion/Package.swift scripts/test_audio_ring.sh
git commit -m "feat: add shared audio ring"
```

### Task 11: Input-Only Core Audio HAL Device

**Files:**
- Create: `companion/AudioDriver/CardputerAudioDevice.h`
- Create: `companion/AudioDriver/CardputerAudioDevice.c`
- Create: `companion/AudioDriver/CardputerAudioDriver.c`
- Create: `companion/AudioDriver/Info.plist`
- Create: `companion/AudioDriver/Tests/CardputerAudioDeviceTests.c`
- Create: `scripts/build_audio_driver.sh`
- Create: `tools/product/tests/test_audio_driver_bundle.py`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: Task 10 ring C ABI.
- Produces: `CardputerAudioDriverFactory`, one input device, one input stream,
  and `cardputer_audio_device_render(...)`.

- [ ] **Step 1: Add failing pure device tests**

Test one input stream, zero output streams, 48 kHz only, mono float format,
stable object IDs, start/stop client counts, silence without a valid ring,
ring samples with a valid producer, and exact zero-fill on underflow.

- [ ] **Step 2: Add failing bundle manifest tests**

Require:

- bundle ID `com.lynx.cardputer-codex-microphone.driver`;
- executable `CardputerCodexMicrophone`;
- version `1.0.0`;
- `AudioServerPlugIn_MachServices` containing
  `com.lynx.cardputer-codex-microphone.ipc`;
- no output-device declaration;
- linked CoreAudio/CoreFoundation/Security/XPC frameworks.

- [ ] **Step 3: Run tests and verify RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q tools/product/tests/test_audio_driver_bundle.py
scripts/build_audio_driver.sh --test
```

Expected: FAIL because the driver sources and bundle are absent.

- [ ] **Step 4: Implement the minimal HAL property surface**

Model the Apple sample's `AudioServerPlugInDriverInterface`, but implement only
the required plug-in, device, stream, and controls. The render path is:

```c
uint32_t cardputer_audio_device_render(
    CardputerAudioDevice *device,
    float *output,
    uint32_t frame_count) {
  return cardputer_audio_ring_read_or_silence(
      device->ring, output, frame_count);
}
```

The driver must not call Core Audio client HAL APIs from inside the plug-in.
Use the SDK `AudioServerPlugIn.h` UUID constants and Apple's documented
factory/property contract.

- [ ] **Step 5: Implement deterministic driver build**

`scripts/build_audio_driver.sh` uses `xcrun --sdk macosx clang`, C17, macOS 14
deployment target, warnings-as-errors, and builds:

`dist/CardputerCodexMicrophone.driver/Contents/MacOS/CardputerCodexMicrophone`

Development mode signs ad-hoc and stamps a development marker in the bundle.

- [ ] **Step 6: Verify driver core and bundle**

Run:

```bash
scripts/build_audio_driver.sh --test
PYTHONPATH=. uv run pytest -q tools/product/tests/test_audio_driver_bundle.py
codesign --verify --deep --strict dist/CardputerCodexMicrophone.driver
```

Expected: PASS.

- [ ] **Step 7: Commit the HAL core**

```bash
git add companion/AudioDriver scripts/build_audio_driver.sh \
  tools/product/tests/test_audio_driver_bundle.py .gitignore
git commit -m "feat: add Cardputer virtual microphone driver"
```

### Task 12: Authenticated XPC Producer Lease

**Files:**
- Create: `companion/AudioDriver/CardputerAudioIPC.h`
- Create: `companion/AudioDriver/CardputerAudioIPC.c`
- Create: `companion/AudioDriver/Tests/CardputerAudioIPCTests.c`
- Create: `companion/AudioHelper/CardputerAudioBridgeMain.c`
- Create: `companion/AudioHelper/com.lynx.cardputer-audio-bridge.plist`
- Create: `companion/Sources/ProductAudio/AudioDriverConnection.swift`
- Create: `companion/Tests/ProductAudioTests/AudioDriverConnectionTests.swift`
- Modify: `companion/AudioDriver/CardputerAudioDriver.c`
- Modify: `companion/AudioDriver/Info.plist`
- Modify: `companion/Package.swift`
- Modify: `scripts/build_audio_driver.sh`

**Interfaces:**
- Consumes: Task 10 ring and Task 11 device.
- Produces: XPC operations `hello`, `claim`, `heartbeat`, `release`, anonymous
  shared-ring FD transfer, caller validation, and a two-second producer lease.

- [ ] **Step 1: Add failing IPC policy tests**

Use injected audit-token validation to prove:

- wrong bundle identifier rejected;
- wrong Team ID rejected in release mode;
- ad-hoc mode requires the exact expected identifier and invoking console UID;
- a second producer is rejected while a lease is live;
- a stale lease may be replaced;
- protocol mismatch receives no FD;
- release clears and silences the ring.

- [ ] **Step 2: Add failing Swift connection tests**

Use a fake XPC transport to assert the exact handshake:

```swift
hello(version: 1)
claim()
heartbeat(every: .milliseconds(500))
release()
```

Verify sink-ready is not exposed until the FD is mapped and the first
heartbeat succeeds.

- [ ] **Step 3: Run tests and verify RED**

Run:

```bash
scripts/build_audio_driver.sh --test
swift test --package-path companion --filter AudioDriverConnectionTests
```

Expected: FAIL because IPC and connection types are absent.

- [ ] **Step 4: Implement secure XPC and anonymous shared memory**

The root-owned launchd bridge creates an anonymous `shm_open` object with a
random name, unlinks it immediately, sizes and maps it, and transfers duplicate
FDs over XPC only after validating the Companion producer and Apple
platform-signed `coreaudiod` consumer. The HAL plug-in and Companion are
outbound clients. Do not publish a global writable path or TCP/Unix listener.

Validate release builds with Security.framework using the connection audit
token, bundle identifier, and configured Team ID. Compile ad-hoc acceptance
only when `CARDPUTER_AUDIO_DEVELOPMENT=1`.

- [ ] **Step 5: Connect the AudioPipeline**

`AudioDriverConnection` conforms to `AudioSampleSink`, writes 48 kHz float
frames to the mapped ring, heartbeats every 500 ms, resets on reconnect, and
publishes sink-not-ready before teardown.

- [ ] **Step 6: Verify IPC, Swift, and realtime constraints**

Run:

```bash
scripts/build_audio_driver.sh --test
swift test --package-path companion --filter ProductAudioTests
PYTHONPATH=. uv run pytest -q tools/product/tests/test_audio_driver_bundle.py
```

Expected: PASS. Review the realtime render path to confirm no XPC, logs, locks,
filesystem calls, or allocation.

- [ ] **Step 7: Commit IPC integration**

```bash
git add companion/AudioDriver companion/Sources/ProductAudio \
  companion/Tests/ProductAudioTests companion/Package.swift \
  scripts/build_audio_driver.sh
git commit -m "feat: bridge Companion audio to Core Audio"
```

### Task 13: Driver Install, Doctor, and Product UI Integration

**Files:**
- Create: `scripts/install_audio_driver.sh`
- Create: `tools/product/tests/test_audio_driver_installer.py`
- Modify: `companion/Sources/cardputer-companion/Configuration.swift`
- Modify: `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Modify: `companion/AppBundle/Info.plist`
- Modify: `companion/AppBundle/CardputerCompanion.entitlements`
- Modify: `scripts/build_companion.sh`
- Modify: `firmware/main/product/ui_model.hpp`
- Modify: `firmware/main/product/ui_model.cpp`
- Modify: `firmware/main/product/display.cpp`
- Modify: `firmware/main/product/settings_menu.hpp`
- Modify: `firmware/main/product/settings_menu.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/main/product/product_web.cpp`
- Modify: `firmware/test/host/test_ui_model.cpp`
- Modify: `firmware/test/host/test_settings_menu.cpp`
- Modify: `firmware/test/host/test_product_web.cpp`
- Create: `companion/Tests/ProductContractsTests/ConfigurationTests.swift`

**Interfaces:**
- Consumes: Task 6 `MicrophoneSnapshot` and Task 12 `AudioDriverConnection`.
- Produces: install/uninstall/doctor audio CLI, all-page microphone status,
  DEVICE row, PET live glyph, Settings `Input Mode`, SAFE migration, and
  read-only Web audio status.

- [ ] **Step 1: Add failing CLI and installer tests**

Test:

- `doctor audio`;
- `install-audio-driver`;
- `uninstall-audio-driver`;
- non-root mutation exits with a command that explicitly requires `sudo`;
- install source is the bundled driver only;
- atomic replacement under `/Library/Audio/Plug-Ins/HAL`;
- uninstall targets only `CardputerCodexMicrophone.driver`;
- normal `run` never asks for elevation.

- [ ] **Step 2: Add failing firmware UI and Settings tests**

Assert every status bar state, red PET live indicator model, DEVICE microphone
row, no blinking state, one-second error copy, `Input Mode` Settings item,
SAFE selection issuing HID Release All, and no G0 mode/SAFE action.

- [ ] **Step 3: Add failing Web boundary tests**

Require read-only fields:

```json
{
  "microphone": {
    "state": "READY",
    "sample_rate_hz": 0,
    "drop_percent": 0,
    "last_error": "NONE"
  }
}
```

Search the route manifest and handlers to prove no route or action can start or
stop capture.

- [ ] **Step 4: Run tests and verify RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_driver_installer.py
swift test --package-path companion --filter ProductContractsTests
cmake --build build/audio-host \
  --target test_ui_model test_settings_menu test_product_web -j
ctest --test-dir build/audio-host \
  -R '^(ui_model|settings_menu|product_web)$' --output-on-failure
```

Expected: FAIL because product integration is absent.

- [ ] **Step 5: Implement root-only install and doctor**

Bundle the driver under
`CardputerCompanion.app/Contents/Resources/`. The CLI invokes the repository
helper only for development; the built app resolves its own Resources path.
Install uses a staging directory, verifies bundle ID/version/signature, then
renames into the HAL directory. Uninstall validates the exact canonical path
before removal.

`doctor audio` checks installed version, Core Audio enumeration, XPC handshake,
ring silence, GATT audio discovery, subscription, and negotiated rate.

- [ ] **Step 6: Implement firmware UI, Settings, and Web status**

Add `UiMicrophoneState` and `UiModel::set_microphone(...)`. Render a persistent
indicator without full-screen redraw. Add `Input Mode` to Settings and route
SAFE through existing Profile activation with Release All. Publish only
snapshot metrics through the existing status endpoint.

- [ ] **Step 7: Run targeted verification**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_driver_installer.py
swift test --package-path companion
cmake --build build/audio-host \
  --target test_ui_model test_settings_menu test_product_web -j
ctest --test-dir build/audio-host \
  -R '^(ui_model|settings_menu|product_web)$' --output-on-failure
scripts/build_audio_driver.sh
scripts/build_companion.sh
```

Expected: PASS.

- [ ] **Step 8: Commit product integration**

```bash
git add scripts companion firmware/main/product firmware/test/host \
  tools/product/tests/test_audio_driver_installer.py
git commit -m "feat: integrate Cardputer microphone controls"
```

### Task 14: Recovery, Release, Installation, and HIL

**Files:**
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify: `companion/AppBundle/Info.plist`
- Modify: `scripts/verify_product_release.sh`
- Modify: `README.md`
- Modify: `docs/validation/product-release.md`
- Create: `docs/validation/cardputer-ble-microphone-release.md`
- Modify: `docs/2026-07-26-cardputer-ble-microphone_PROGRESS.md`
- Modify: `/Users/nicholasliao/clawd/memory/2026-07-26.md`

**Interfaces:**
- Consumes: all prior tasks.
- Produces: version `1.1.0`, complete release gate, locally installed HAL/
  Companion, app-only flashed firmware, independent flash verification, and
  30-minute evidence.

- [ ] **Step 1: Add recovery and release-gate tests**

Cover Companion restart, Core Audio restart, BLE unsubscribe/reconnect,
driver mismatch, source overrun, NimBLE pressure, ring underflow, stale producer
lease, reboot privacy, and exact version parity across firmware/UI/app/driver.

- [ ] **Step 2: Run full RED-sensitive checks before version bump**

Run:

```bash
PYTHONPATH=. uv run pytest -q
cmake --build build/audio-host -j
ctest --test-dir build/audio-host --output-on-failure
swift test --package-path companion
```

Expected: any newly added version/recovery assertions fail until integration
and version updates are complete.

- [ ] **Step 3: Complete recovery wiring and bump versions**

Set firmware `PROJECT_VER` and product version to `1.1.0`; set Companion and
driver bundle versions consistently. Ensure all teardown paths publish
sink-not-ready, stop DMA, clear queues/rings, and leave capture off.

- [ ] **Step 4: Extend the release script**

Add audio protocol validation, ProductAudio tests, C ring tests, driver test
build, driver bundle validation, Companion bundle resources, absence of audio
content artifacts, and secret exclusion. Preserve all existing checks.

- [ ] **Step 5: Run the complete release gate**

Run:

```bash
scripts/verify_product_release.sh
git diff --check
```

Expected:

- all Python tests pass;
- all normal and ASan/UBSan firmware host tests pass;
- ESP-IDF 5.5.4 clean target build passes;
- all Swift tests and release build pass;
- HAL test build, bundle, and signature verification pass;
- Web, partitions, generic/private packaging, and secret checks pass.

- [ ] **Step 6: Install the development HAL and Companion**

For the current Mac only:

```bash
sudo dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  install-audio-driver
dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion doctor audio
```

Restart the supported audio service or the Mac as instructed by the installer.
Reinstall the LaunchAgent using the existing project helper and verify the
actual running executable path.

- [ ] **Step 7: App-only flash and independent verification**

Resolve the unique serial device. Write the final app image at `0x20000`, then
run an independent `verify_flash` against the final on-disk binary. Do not
write a full image unless preservation is explicitly waived.

- [ ] **Step 8: Run the 30-minute concurrent HIL**

Exercise:

- continuous microphone audio;
- 1,000 real HID events;
- Web and Codex/pet synchronization;
- Companion restart;
- Core Audio restart;
- BLE disconnect/reconnect;
- G0 off/on and reboot privacy.

Acceptance:

- capture-to-Core-Audio p95 `<=250 ms`;
- total loss `<1%`;
- no non-deliberate gap over 150 ms;
- zero missing HID reports;
- all heap/stack/HID/allocation gates;
- no panic, watchdog, overflow, reboot, or reconnect loop;
- disconnect and reboot never resume capture.

- [ ] **Step 9: Record artifacts and hashes**

Record exact paths, sizes, SHA-256 values, firmware version, driver version,
Companion executable hash, serial port, installed HAL path, LaunchAgent PID,
status endpoint, and HIL report path. State whether signing is ad-hoc or
Developer ID/notarized.

- [ ] **Step 10: Update docs and task memory**

Document setup, G0 operation, Settings migration, choosing the virtual input,
privacy behavior, driver install/uninstall, doctor, fallback rate, recovery,
and current platform boundary. Update the progress document and daily workspace
memory without secrets.

- [ ] **Step 11: Commit final release**

```bash
git add firmware companion scripts tools protocol README.md docs
git commit -m "chore: release Cardputer microphone 1.1.0"
```

- [ ] **Step 12: Final repository and remote check**

Run:

```bash
git status --short --branch
git log -5 --oneline --decorate
git remote -v
```

Expected: clean worktree. If a remote exists, push the feature branch and
verify the remote commit. If no remote exists, report that push was impossible
without inventing one.

## Plan Completion Criteria

The implementation is complete only when:

1. every task checkbox is complete;
2. the 24/16 kHz feasibility decision is backed by real-device evidence;
3. the system Core Audio input device is enumerated on the current Mac;
4. G0 signal/silence behavior is physically verified;
5. HID remains reliable during audio;
6. disconnect and reboot privacy behavior is verified;
7. full release and 30-minute HIL gates pass;
8. final artifacts are installed/flashed and independently verified;
9. documentation, progress, and workspace daily memory are current;
10. all commits are present and the worktree is clean.
