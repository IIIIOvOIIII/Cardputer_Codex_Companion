# USB HIL Microphone Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Automate G0-equivalent microphone start/stop over the physical USB serial connection, freeze PET animation during capture, and make the audio HIL exercise the real macOS virtual microphone path.

**Architecture:** Add an allocation-free C++ line parser and idempotent microphone-action policy, then poll it non-blockingly from the existing UI task and enqueue the existing G0 state-machine event. Refactor the Python HIL serial reader into one duplex owner that starts and stops capture with exact commands. Replace the probe's null sink with the existing authenticated HAL bridge so HIL audio reaches `Cardputer Codex Microphone`.

**Tech Stack:** C++20, ESP-IDF 5.5.4, FreeRTOS, ESP VFS USB serial/JTAG console, Python 3.11 with pytest, Swift 6, CoreBluetooth, XPC, Core Audio HAL.

## Global Constraints

- USB commands are exactly `HIL MIC START\n` and `HIL MIC STOP\n`.
- No Web, HTTPS, BLE, Companion GATT, or Codex Agent microphone-start route may be added.
- START must use the existing G0 event and retain encrypted BLE, Companion binding, notification subscription, protocol, and `sink_ready` gates.
- STOP must be idempotent and must run during every HIL cleanup path.
- Serial control and firmware parsing must not allocate after startup.
- PET animation is frozen only in `STARTING`, `LIVE24`, `LIVE16`, and `STOPPING`; status rendering remains active.
- HIL output remains metrics-only and cannot persist PCM, ADPCM, payloads, or decoded samples.
- A serial acknowledgement is not proof of capture; the first received audio frame remains the timer gate.
- The concurrent HID gate uses exactly `HIL HID START\n`, queues 1,000 neutral
  usage-0 events through `KeyboardProbe`, and never mutates HID metrics directly.

---

## File Map

- Create `firmware/main/product/hil_serial_control.hpp`: bounded parser and pure microphone command policy.
- Create `firmware/main/product/hil_serial_control.cpp`: allocation-free parser implementation.
- Create `firmware/test/host/test_hil_serial_control.cpp`: command, overflow, fragmentation, state-policy, and animation tests.
- Modify `firmware/test/host/CMakeLists.txt`: register the host test.
- Modify `firmware/main/CMakeLists.txt`: include the firmware module.
- Modify `firmware/main/product/product_controller.cpp`: non-blocking USB polling, acknowledgements, existing event-queue integration, and animation gating.
- Modify `scripts/product/run_audio_feasibility_hil.py`: one duplex serial owner, automatic START/STOP, readiness observation, and cleanup.
- Modify `tools/product/tests/test_audio_feasibility_hil.py`: real pipe/socket behavior and orchestration tests.
- Modify `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`: route probe samples through `AudioBridgeCoordinator`.
- Create `companion/Sources/ProductAudio/AudioProbeSinkGate.swift`: pure probe bridge-readiness gate.
- Create `companion/Tests/ProductAudioTests/AudioProbeSinkGateTests.swift`: bridge readiness tests.
- Modify `companion/Tests/ProductAudioTests/main.swift`: execute the new tests.
- Modify `docs/2026-07-26-cardputer-ble-microphone_PROGRESS.md`: record RED/GREEN, deployment, and HIL evidence.

### Task 1: Allocation-Free USB Command Parser and Policy

**Files:**
- Create: `firmware/main/product/hil_serial_control.hpp`
- Create: `firmware/main/product/hil_serial_control.cpp`
- Create: `firmware/test/host/test_hil_serial_control.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`

**Interfaces:**
- Consumes: `MicrophoneState` from `product/microphone_state.hpp`.
- Produces:
  - `enum class HilMicrophoneCommand : uint8_t { none, start, stop };`
  - `class HilSerialCommandParser { HilMicrophoneCommand consume(uint8_t byte); void reset(); };`
  - `MicrophoneEventKind hil_microphone_event(HilMicrophoneCommand, MicrophoneState);`
  - `bool pet_animation_allowed(MicrophoneState);`

- [ ] **Step 1: Write the failing parser and policy test**

```cpp
HilSerialCommandParser parser;
assert(feed(parser, "HIL MIC START\n") == HilMicrophoneCommand::start);
assert(feed(parser, "HIL MIC STOP\n") == HilMicrophoneCommand::stop);
assert(feed(parser, "HIL MIC START") == HilMicrophoneCommand::none);
assert(feed(parser, "HIL MIC START NOW\n") == HilMicrophoneCommand::none);
assert(feed(parser, std::string(80, 'A') + "\n") == HilMicrophoneCommand::none);

assert(hil_microphone_event(HilMicrophoneCommand::start,
                            MicrophoneState::ready) ==
       MicrophoneEventKind::g0_click);
assert(hil_microphone_event(HilMicrophoneCommand::start,
                            MicrophoneState::live24) ==
       MicrophoneEventKind::g0_ignored);
assert(hil_microphone_event(HilMicrophoneCommand::stop,
                            MicrophoneState::live24) ==
       MicrophoneEventKind::g0_click);
assert(hil_microphone_event(HilMicrophoneCommand::stop,
                            MicrophoneState::ready) ==
       MicrophoneEventKind::g0_ignored);

assert(!pet_animation_allowed(MicrophoneState::starting));
assert(!pet_animation_allowed(MicrophoneState::live24));
assert(!pet_animation_allowed(MicrophoneState::live16));
assert(!pet_animation_allowed(MicrophoneState::stopping));
assert(pet_animation_allowed(MicrophoneState::ready));
assert(pet_animation_allowed(MicrophoneState::error));
```

- [ ] **Step 2: Run the test and verify RED**

Run:

```bash
cmake -S firmware/test/host -B build/firmware-host
cmake --build build/firmware-host --target test_hil_serial_control
```

Expected: configuration or compilation fails because `hil_serial_control.hpp`
and its interfaces do not exist.

- [ ] **Step 3: Implement the bounded parser and pure policy**

Use a fixed `std::array<char, 32>` plus length and overflow fields. Append bytes
until `\n`; trim one preceding `\r`; compare the completed span against the two
literal commands; discard the line after overflow. Return `g0_click` only for
READY+START or active/pending+STOP, otherwise return `g0_ignored`.

- [ ] **Step 4: Run the focused test and full host tests**

Run:

```bash
cmake --build build/firmware-host --target test_hil_serial_control
ctest --test-dir build/firmware-host --output-on-failure
```

Expected: focused test passes and all host tests pass.

- [ ] **Step 5: Commit the parser**

```bash
git add firmware/main/product/hil_serial_control.hpp \
  firmware/main/product/hil_serial_control.cpp \
  firmware/test/host/test_hil_serial_control.cpp \
  firmware/test/host/CMakeLists.txt
git commit -m "feat: add USB microphone HIL commands"
```

### Task 2: Firmware USB Runtime and PET Animation Isolation

**Files:**
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/test/host/test_hil_serial_control.cpp`

**Interfaces:**
- Consumes: Task 1 parser, policy, `g_microphone->snapshot()`, and `enqueue_microphone_event`.
- Produces: non-blocking USB command runtime and deterministic serial acknowledgement lines.

- [ ] **Step 1: Extend the host test with acknowledgement decisions**

Add a pure formatter input:

```cpp
const HilCommandDecision accepted = hil_command_decision(
    HilMicrophoneCommand::start, MicrophoneState::ready);
assert(accepted.event == MicrophoneEventKind::g0_click);
assert(accepted.accepted);

const HilCommandDecision rejected = hil_command_decision(
    HilMicrophoneCommand::start, MicrophoneState::unavailable);
assert(rejected.event == MicrophoneEventKind::g0_ignored);
assert(!rejected.accepted);
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
cmake --build build/firmware-host --target test_hil_serial_control
```

Expected: compilation fails because `HilCommandDecision` and
`hil_command_decision` do not exist.

- [ ] **Step 3: Implement the decision and runtime integration**

Add `HilCommandDecision` to the pure module. In `product_runtime_start`, set
`STDIN_FILENO` to `O_NONBLOCK` without changing stdout. In each UI loop, read at
most 32 bytes, feed them to the parser, snapshot microphone state, and enqueue
the returned event. Emit exactly one of:

```text
HIL MIC START ACCEPTED
HIL MIC START REJECTED
HIL MIC STOP ACCEPTED
HIL MIC STOP NOOP
```

Do not call `IAudioCapture` or BLE functions from the UI task.

Before advancing `frame_index` or calling the PET frame renderer, evaluate
`pet_animation_allowed(g_microphone->snapshot().state)`. When false, keep the
current frame and allow normal UI revision/status rendering.

- [ ] **Step 4: Verify host, sanitizer, and ESP-IDF target builds**

Run:

```bash
cmake --build build/firmware-host --target test_hil_serial_control
ctest --test-dir build/firmware-host --output-on-failure
cmake -S firmware/test/host -B build/firmware-host-sanitize \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/firmware-host-sanitize
ctest --test-dir build/firmware-host-sanitize --output-on-failure
bash -lc 'source .tools/esp-idf/export.sh >/dev/null && idf.py -C firmware build'
```

Expected: all host/sanitizer tests pass and the ESP-IDF image links.

- [ ] **Step 5: Commit the runtime integration**

```bash
git add firmware/main/CMakeLists.txt \
  firmware/main/product/product_controller.cpp \
  firmware/test/host/test_hil_serial_control.cpp
git commit -m "feat: automate microphone control over USB"
```

### Task 3: Duplex Serial HIL Automation

**Files:**
- Modify: `scripts/product/run_audio_feasibility_hil.py`
- Modify: `tools/product/tests/test_audio_feasibility_hil.py`

**Interfaces:**
- Consumes: exact firmware lines from Tasks 1-2.
- Produces:
  - `class SerialMonitor`
  - `SerialMonitor.start()`
  - `SerialMonitor.send(command: bytes)`
  - `SerialMonitor.wait_for(pattern: str, timeout: float) -> bool`
  - `SerialMonitor.stop()`

- [ ] **Step 1: Write failing real-descriptor tests**

Use `socket.socketpair()` so the test exercises real duplex descriptors:

```python
monitor_side, device_side = socket.socketpair()
monitor = module.SerialMonitor(monitor_side.fileno(), samples)
monitor.start()
device_side.sendall(b"BLE audio sink ready=1\n")
assert monitor.wait_for("BLE audio sink ready=1", timeout=1)
monitor.send(b"HIL MIC START\n")
assert device_side.recv(64) == b"HIL MIC START\n"
monitor.stop()
```

Add an orchestration fake whose child exits by timeout and assert its recorded
commands are exactly:

```python
[b"HIL MIC START\n", b"HIL MIC STOP\n"]
```

- [ ] **Step 2: Run the tests and verify RED**

Run:

```bash
uv run pytest -q tools/product/tests/test_audio_feasibility_hil.py
```

Expected: failures because `SerialMonitor` and automatic cleanup do not exist.

- [ ] **Step 3: Implement one duplex serial owner**

Open the device once with `os.O_RDWR | os.O_NONBLOCK`. The reader thread retains
JSON metric parsing and also stores bounded recent text lines under a
`threading.Condition`. `send` loops over partial `os.write` results and fails
on zero progress. Launch the audio probe, wait up to 30 seconds for
`BLE audio sink ready=1`, send START, and retain the Companion first-frame
start gate. In the outermost `finally`, send STOP before closing the descriptor;
cleanup write errors are logged without replacing an earlier exception.

- [ ] **Step 4: Run focused and complete Python gates**

Run:

```bash
uv run pytest -q tools/product/tests/test_audio_feasibility_hil.py
uv run pytest -q tools/product/tests
```

Expected: all tests pass and metrics-content prohibitions remain green.

- [ ] **Step 5: Commit the automated runner**

```bash
git add scripts/product/run_audio_feasibility_hil.py \
  tools/product/tests/test_audio_feasibility_hil.py
git commit -m "test: automate microphone HIL over USB"
```

### Task 4: Route Probe Audio Through the HAL Bridge

**Files:**
- Create: `companion/Sources/ProductAudio/AudioProbeSinkGate.swift`
- Create: `companion/Tests/ProductAudioTests/AudioProbeSinkGateTests.swift`
- Modify: `companion/Tests/ProductAudioTests/main.swift`
- Modify: `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`

**Interfaces:**
- Consumes: `AudioBridgeCoordinator.reconnectIfNeeded()` and
  `ProductGATTReceiver.start(audioSink:)`.
- Produces:
  - `enum AudioProbeSinkGateResult { case waiting, ready, timedOut }`
  - `struct AudioProbeSinkGate { mutating func observe(bridgeReady: Bool) -> AudioProbeSinkGateResult }`

- [ ] **Step 1: Write the failing bridge-readiness test**

```swift
var gate = AudioProbeSinkGate(timeoutSeconds: 3)
precondition(gate.observe(bridgeReady: false) == .waiting)
precondition(gate.observe(bridgeReady: true) == .ready)

gate = AudioProbeSinkGate(timeoutSeconds: 2)
precondition(gate.observe(bridgeReady: false) == .waiting)
precondition(gate.observe(bridgeReady: false) == .timedOut)
```

- [ ] **Step 2: Run the ProductAudio tests and verify RED**

Run:

```bash
swift run --package-path companion -c debug product-audio-tests
```

Expected: compilation fails because `AudioProbeSinkGate` does not exist.

- [ ] **Step 3: Implement the gate and real probe sink**

In `audioProbe`, create `AudioBridgeCoordinator`, retry
`reconnectIfNeeded()` once per second through the bounded gate, and fail with
`AudioProbeError.audioBridgeUnavailable` if it never becomes ready. Start the
receiver with `audioBridge`, not `NullAudioSink`. Defer receiver stop followed
by bridge stop. Keep the existing received-frame timer gate and metrics report
unchanged.

- [ ] **Step 4: Verify Swift debug/release and audio doctor**

Run:

```bash
swift run --package-path companion -c debug product-audio-tests
swift run --package-path companion -c release product-audio-tests
scripts/build_companion.sh
dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion doctor audio
```

Expected: ProductAudio tests pass in both modes; with the normal LaunchAgent
stopped, doctor reports driver, Core Audio input, XPC, shared ring, BLE audio
characteristics, subscriptions, protocol, and preferred rate as OK.

- [ ] **Step 5: Commit the real audio probe path**

```bash
git add companion/Sources/ProductAudio/AudioProbeSinkGate.swift \
  companion/Tests/ProductAudioTests/AudioProbeSinkGateTests.swift \
  companion/Tests/ProductAudioTests/main.swift \
  companion/Sources/cardputer-companion/CardputerCompanionMain.swift
git commit -m "fix: feed HIL audio into Core Audio"
```

### Task 5: Build, Deploy, and Automated Short Reproduction

**Files:**
- Modify: `docs/2026-07-26-cardputer-ble-microphone_PROGRESS.md`

**Interfaces:**
- Consumes: final firmware, Companion, HAL, and automated HIL runner.
- Produces: hardware evidence for the existing 8-second MIC failure without a physical G0 press.

- [ ] **Step 1: Run the complete automated release gate**

Run:

```bash
scripts/verify_product_release.sh
```

Expected: Python, C/C++, sanitizer, Swift, ESP-IDF, bundle, signing, partition,
DIRAM, and private packaging gates all pass.

- [ ] **Step 2: Install the rebuilt Companion/HAL and flash app-only firmware**

Run the repository installers, then write only the app partition at `0x20000`
to `/dev/cu.usbmodem21201`. Verify flash against the built app image with
`esptool.py verify_flash`; do not write the full image because it would replace
persisted device configuration.

- [ ] **Step 3: Run a 30-second automated reproduction**

Run:

```bash
PYTHONPATH=. uv run python scripts/product/run_audio_feasibility_hil.py \
  --port /dev/cu.usbmodem21201 \
  --companion dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  --device-url https://192.168.1.195 \
  --duration 30 \
  --output build/hil/cardputer-audio-debug-30s.json
```

Expected operational behavior: the script starts and stops MIC without user
input, PET animation freezes during LIVE and resumes afterward, and the
Cardputer virtual microphone receives the probe stream. The release loss gate
may still fail here; its exact serial counters and state transition become the
evidence for the subsequent systematic MIC ERR repair.

- [ ] **Step 4: Record the evidence and next root-cause hypothesis**

Append a milestone containing:

- received and captured frames at the stop point;
- source overrun and transport drop deltas;
- 24 kHz to 16 kHz transition time;
- last microphone state;
- heap, largest block, allocation failures, and task high-water values;
- whether the failure coincides with the first TLS request at second 8;
- whether the HAL input device shows changing input level.

- [ ] **Step 5: Commit the milestone**

```bash
git add docs/2026-07-26-cardputer-ble-microphone_PROGRESS.md
git commit -m "docs: record automated microphone reproduction"
```

After this plan is complete, use the captured evidence with
`superpowers:systematic-debugging`, write the smallest failing regression test
for the proven MIC ERR cause, implement that repair, and only then rerun the
final 30-minute HIL.

### Task 6: USB-Only Concurrent HID Gate

**Files:**
- Modify: `firmware/main/product/hil_serial_control.hpp`
- Modify: `firmware/main/product/hil_serial_control.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/test/host/test_hil_serial_control.cpp`
- Modify: `scripts/product/run_audio_feasibility_hil.py`
- Modify: `tools/product/tests/test_audio_feasibility_hil.py`

- [ ] **Step 1: Add RED host and Python tests**

Require exact parsing of `HIL HID START`, rejection when HID is not ready or a
burst is already active, exactly 1,000 alternating usage-0 events, and runner
ordering `MIC START -> HID START -> HID COMPLETE -> MIC STOP`.

- [ ] **Step 2: Implement the bounded firmware burst**

Advance one neutral event per 500 ms. Submit each event only through
`KeyboardProbe::enqueue_stable_key_event`; do not call the report sink or metric
observer directly. Emit a completion acknowledgement after the thousandth
event.

- [ ] **Step 3: Integrate the HIL runner**

Start the burst after the resource baseline and accepted microphone start.
Require the accepted and completion acknowledgements. Preserve STOP cleanup on
every failure path.

- [ ] **Step 4: Verify**

Run the focused host/Python tests, full host tests, ESP-IDF build, app-only
flash, and a short concurrent HIL before the final 30-minute gate.
