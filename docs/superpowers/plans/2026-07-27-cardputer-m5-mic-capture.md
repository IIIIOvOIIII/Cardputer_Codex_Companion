# Cardputer M5.Mic Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the silent direct ESP-IDF PDM capture backend with the official M5Unified `M5.Mic` Cardputer path and prove non-zero microphone input on macOS.

**Architecture:** Keep `IAudioCapture`, ADPCM, BLE transport, and the macOS HAL contract unchanged. Replace only the ESP hardware backend with a fixed-allocation double-buffered adapter around `M5.Mic`, using the Cardputer right PDM slot and M5Unified signal conditioning.

**Tech Stack:** C++20, M5Unified, ESP-IDF 5.5.4, FreeRTOS, Python HIL, Core Audio/FFmpeg metrics.

## Global Constraints

- Flash only the application partition at `0x20000`; never overwrite persisted configuration partitions.
- Keep PIN, Wi-Fi, keyboard profiles, pet data, BLE bonds, Audio v1 packets, G0 semantics, and 24→16 kHz fallback unchanged.
- Allocate no audio buffers after capture starts.
- Never save or print PCM, ADPCM, or captured audio content; validation may emit only aggregate peak/RMS/zero-sample metrics.

---

### Task 1: Specify the M5.Mic capture contract

**Files:**
- Modify: `firmware/main/product/audio_capture.hpp`
- Modify: `firmware/test/host/test_audio_capture.cpp`

**Interfaces:**
- Consumes: existing `AudioCaptureBackend` and `PdmAudioCapture`.
- Produces: `ProductMicHardwareConfig product_mic_hardware_config(uint32_t rate_hz)` and double-buffer queue-state helpers used by the ESP backend.

- [ ] **Step 1: Write the failing host tests**

Add assertions that 16/24 kHz select GPIO46 data, GPIO43 clock, right input,
oversampling 1, magnification 16, and that a two-buffer queue returns buffers
in order when the recorder depth falls from two to one or zero.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
cmake --build firmware/test/host/build --target test_audio_capture
ctest --test-dir firmware/test/host/build -R '^audio_capture$' --output-on-failure
```

Expected: compile failure because the hardware contract and queue helper do not exist.

- [ ] **Step 3: Add the minimal pure C++ contract**

Declare a POD hardware configuration with exact pin, channel, oversampling, and
magnification values, plus a fixed two-slot completion tracker that never
allocates.

- [ ] **Step 4: Run the focused test and verify GREEN**

Run the same build and CTest commands. Expected: `audio_capture` passes.

- [ ] **Step 5: Commit**

```bash
git add firmware/main/product/audio_capture.hpp firmware/test/host/test_audio_capture.cpp
git commit -m "test: specify M5 microphone capture contract"
```

### Task 2: Replace direct PDM with M5.Mic

**Files:**
- Modify: `firmware/main/product/audio_capture.cpp`
- Test: `firmware/test/host/test_audio_capture.cpp`

**Interfaces:**
- Consumes: `product_mic_hardware_config()` and the two-slot completion tracker from Task 1.
- Produces: `M5UnifiedCaptureBackend`, returned by the existing `make_product_audio_capture()`.

- [ ] **Step 1: Remove the direct ESP-IDF PDM initialization**

Delete the `driver/i2s_pdm.h` dependency and the `i2s_new_channel`,
`i2s_channel_init_pdm_rx_mode`, `i2s_channel_read`, and channel callback path.

- [ ] **Step 2: Implement fixed double-buffered M5.Mic capture**

Configure `M5.Mic` with the exact Task 1 contract, call `begin()`, queue both
fixed `int16_t[456]` buffers with `record(..., rate_hz, false)`, and make
`read()` wait at most `audio_capture_read_timeout_ms(rate_hz)` for the oldest
queued buffer before copying and requeueing it.

- [ ] **Step 3: Preserve lifecycle and error mapping**

Keep speaker shutdown before capture, make `disable()` idempotently call
`M5.Mic.end()`, reset all queue state on stop/rate change, map record/begin
failures to `backend_error`, and map the bounded wait to `timeout`.

- [ ] **Step 4: Run host and sanitizer tests**

Run:

```bash
cmake --build firmware/test/host/build
ctest --test-dir firmware/test/host/build --output-on-failure
cmake --build firmware/test/host/build-sanitize
ctest --test-dir firmware/test/host/build-sanitize --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 5: Build the ESP-IDF target**

Run:

```bash
scripts/phase0/idf.sh -C firmware build
```

Expected: ESP32-S3 build passes without direct `i2s_pdm` usage in product code.

- [ ] **Step 6: Commit**

```bash
git add firmware/main/product/audio_capture.cpp
git commit -m "fix: capture Cardputer audio through M5 Mic"
```

### Task 3: Deploy and prove real input level

**Files:**
- Modify: `scripts/product/run_audio_feasibility_hil.py`
- Modify: `tools/product/tests/test_audio_feasibility_hil.py`
- Modify: `docs/2026-07-26-cardputer-ble-microphone_PROGRESS.md`
- Modify: `docs/validation/cardputer-ble-microphone-release.md`

**Interfaces:**
- Consumes: the existing USB `HIL MIC START/STOP`, audio probe, virtual Core Audio device, and app-only flash flow.
- Produces: content-free signal metrics and final release evidence.

- [ ] **Step 1: Add a failing metrics-schema test**

Require aggregate `signal_peak`, `signal_rms`, and `nonzero_sample_percent`
fields while retaining the existing recursive ban on audio content keys.

- [ ] **Step 2: Run the Python test and verify RED**

Run:

```bash
.venv/bin/pytest tools/product/tests/test_audio_feasibility_hil.py -q
```

Expected: failure because signal metrics are absent.

- [ ] **Step 3: Collect aggregate signal metrics only**

Read the virtual input into an in-memory bounded analysis window, compute peak,
RMS, and nonzero percentage, discard each window immediately, and write only
those aggregates to the HIL report.

- [ ] **Step 4: Run Python and release gates**

Run:

```bash
.venv/bin/pytest tools/product/tests/test_audio_feasibility_hil.py -q
scripts/verify_product_release.sh
```

Expected: focused tests and the full release gate pass.

- [ ] **Step 5: Flash the app partition and verify it**

Run the repository app-only flashing command against
`/dev/cu.usbmodem21201` at offset `0x20000`, then independently verify the
flashed digest. Expected: digest match and preserved device settings.

- [ ] **Step 6: Run serial-triggered signal HIL**

Use the same held-open serial session to wait for BLE audio sink readiness,
send `HIL MIC START`, measure silence and near-field speech/clap windows through
`Cardputer Codex Microphone`, send `HIL MIC STOP` on every exit path, and verify
non-zero signal plus a clear active/silence delta.

- [ ] **Step 7: Update evidence and commit**

Record firmware hash, signal aggregates, frame continuity, heap/stack/HID/TLS
results, and any remaining physical acoustic limitation.

```bash
git add scripts/product/run_audio_feasibility_hil.py tools/product/tests/test_audio_feasibility_hil.py docs/2026-07-26-cardputer-ble-microphone_PROGRESS.md docs/validation/cardputer-ble-microphone-release.md
git commit -m "test: gate Cardputer microphone signal level"
```
