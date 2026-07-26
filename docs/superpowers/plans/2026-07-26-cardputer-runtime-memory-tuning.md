# Cardputer Runtime Memory Tuning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recover at least 29,184 bytes of static internal RAM while preserving pet rendering and the existing 64/32/40 KiB runtime resource gates.

**Architecture:** Add a synchronous fixed-row pet decoder whose callback consumes one fully validated 96-pixel RGB565 row. Keep the existing full-frame API as a compatibility adapter, switch only the Cardputer display to the row interface, and right-size three static task stacks using measured hardware high-water values.

**Tech Stack:** C++20, ESP-IDF 5.5.4, FreeRTOS static tasks, M5Unified/M5GFX, OpenSSL-backed host fixtures, Python 3.11 pytest, esptool.

## Global Constraints

- Keep the 8 MiB ESP32-S3/no-PSRAM target and existing partition table.
- Do not change the pet bundle wire format, coordinates, frame cadence, RGB565 values, byte swapping, transaction model, or fallback placeholder.
- Do not add runtime heap allocation, task creation, network access, or audio work to pet row decoding.
- Preserve steady internal heap `>= 65,536`, largest internal block `>= 32,768`, HTTPS transient internal heap `>= 40,960`, zero allocation failures, HID p95 `<= 20,000 us`, and stack free `>= max(configured / 5, 1,024)`.
- Write only the `0x20000` application partition during hardware verification.
- Use RED-to-GREEN tests and commit each independently reviewable task.

---

### Task 1: Fixed-Row Pet Decoder

**Files:**
- Modify: `firmware/main/product/pet_bundle.hpp`
- Modify: `firmware/main/product/pet_bundle.cpp`
- Modify: `firmware/test/host/test_pet_bundle.cpp`
- Modify: `tools/product/tests/test_companion_packaging.py`

**Interfaces:**
- Consumes: `PetByteSource`, `PetBundleMetadata`, `PetState`, `PetFrameRecord`.
- Produces: `using PetRowConsumer = bool (*)(void*, std::size_t, std::span<const uint16_t, kPetFrameWidth>)` and `decode_pet_frame_rows(..., PetRowConsumer, void*) -> PetBundleError`.
- Preserves: `decode_pet_frame(..., std::span<uint16_t, kPetFramePixels>) -> PetBundleError`.

- [ ] **Step 1: Write the failing row-decoder tests**

Extend `test_pet_bundle.cpp` with a collector and assertions that raw and RLE
rows match the existing decoder, each callback receives row indices `0..103`,
callback rejection returns `PetBundleError::consumer`, malformed RLE never emits
the malformed row, and no source read exceeds 192 bytes:

```cpp
struct RowCollector {
  std::array<uint16_t, kPetFramePixels> pixels{};
  std::size_t rows = 0;
  bool reject_row_2 = false;
};

bool collect_row(
    void* context, std::size_t row,
    std::span<const uint16_t, kPetFrameWidth> pixels) {
  auto& collector = *static_cast<RowCollector*>(context);
  if (collector.reject_row_2 && row == 2) return false;
  std::copy(pixels.begin(), pixels.end(),
            collector.pixels.begin() + row * kPetFrameWidth);
  ++collector.rows;
  return true;
}
```

Update the Python source guard to require
`std::array<uint16_t, kPetFrameWidth> row_pixels` and forbid `std::vector`
within both decode functions.

- [ ] **Step 2: Run the focused tests to verify RED**

Run:

```bash
cmake --build build/audio-host --target test_pet_bundle -j
ctest --test-dir build/audio-host -R '^pet_bundle$' --output-on-failure
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py::test_pet_frame_decode_does_not_allocate_from_runtime_heap
```

Expected: compile failure because `PetRowConsumer` and
`decode_pet_frame_rows` do not exist.

- [ ] **Step 3: Implement the row decoder and compatibility adapter**

Declare the callback and API plus `PetBundleError::consumer` in
`pet_bundle.hpp`. In `pet_bundle.cpp`, decode raw and RLE records into one fixed
row:

```cpp
std::array<uint16_t, kPetFrameWidth> row_pixels{};
```

For raw frames, expose `row_pixels` through `std::as_writable_bytes`, read
exactly 192 bytes, then convert in place with `read16`; do not reserve a second
192-byte row. For RLE, keep only the existing four-byte encoded-run scratch in
addition to `row_pixels`.

Invoke the callback only after a row contains exactly 96 decoded pixels. Add a
private adapter context whose callback copies each row into the caller's
full-frame span, then implement `decode_pet_frame` by calling
`decode_pet_frame_rows`. Return `PetBundleError::bounds` for a null callback
and `PetBundleError::consumer` when a callback declines a row.

- [ ] **Step 4: Run focused normal and sanitizer tests to verify GREEN**

Run:

```bash
cmake --build build/audio-host --target test_pet_bundle -j
ctest --test-dir build/audio-host -R '^pet_bundle$' --output-on-failure
cmake --build build/audio-host-sanitized --target test_pet_bundle -j
ctest --test-dir build/audio-host-sanitized -R '^pet_bundle$' --output-on-failure
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py::test_pet_frame_decode_does_not_allocate_from_runtime_heap
```

Expected: all focused tests pass with no sanitizer report.

- [ ] **Step 5: Commit**

```bash
git add firmware/main/product/pet_bundle.hpp \
  firmware/main/product/pet_bundle.cpp \
  firmware/test/host/test_pet_bundle.cpp \
  tools/product/tests/test_companion_packaging.py
git commit -m "refactor: decode pet frames by row"
```

### Task 2: Row-Oriented Cardputer Display

**Files:**
- Modify: `firmware/main/product/pet_store.hpp`
- Modify: `firmware/main/product/pet_store.cpp`
- Modify: `firmware/main/product/display.cpp`
- Modify: `tools/product/tests/test_firmware_memory.py`

**Interfaces:**
- Consumes: Task 1 `PetRowConsumer` and `decode_pet_frame_rows`.
- Produces: `PetStore::decode_rows(PetState, uint8_t, PetRowConsumer, void*) -> bool`.
- Removes: the 19,968-byte `g_pet_frame` full-frame static array from target runtime.

- [ ] **Step 1: Write the failing static-memory source test**

Add a test to `test_firmware_memory.py`:

```python
def test_pet_display_uses_only_a_single_rgb565_row():
    display = (
        REPO_ROOT / "firmware/main/product/display.cpp"
    ).read_text(encoding="utf-8")
    assert "g_pet_frame" not in display
    assert "pushImage(kPetX, kPetY + static_cast<int32_t>(row)," in display
    assert "kPetFrameWidth, 1, pixels.data()" in display
```

- [ ] **Step 2: Run the test to verify RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_firmware_memory.py::test_pet_display_uses_only_a_single_rgb565_row
```

Expected: fail because `g_pet_frame` still exists.

- [ ] **Step 3: Add the store row facade and display callback**

Implement `PetStore::decode_rows` with the same mutex, slot, partition source,
and metadata lifetime as `decode`. In `display.cpp`, remove `g_pet_frame` and
add a non-capturing callback:

```cpp
bool draw_pet_row(
    void*, std::size_t row,
    std::span<const uint16_t, kPetFrameWidth> pixels) {
  M5.Display.pushImage(
      kPetX, kPetY + static_cast<int32_t>(row),
      kPetFrameWidth, 1, pixels.data());
  return true;
}
```

Keep one `startWrite`/`endWrite` transaction and one swap-bytes save/restore
around `PetStore::decode_rows`. On any failure, return `false`; the existing UI
caller renders the placeholder.

- [ ] **Step 4: Run focused tests and target build**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_firmware_memory.py \
  tools/product/tests/test_companion_packaging.py
cmake --build build/audio-host --target test_pet_bundle -j
ctest --test-dir build/audio-host -R '^pet_bundle$' --output-on-failure
scripts/phase0/idf.sh -C firmware build
```

Expected: all tests and the ESP32-S3 build pass. `xtensa-esp32s3-elf-nm`
contains no 19,968-byte `g_pet_frame` symbol.

- [ ] **Step 5: Commit**

```bash
git add firmware/main/product/pet_store.hpp \
  firmware/main/product/pet_store.cpp \
  firmware/main/product/display.cpp \
  tools/product/tests/test_firmware_memory.py
git commit -m "refactor: render pet frames row by row"
```

### Task 3: Right-Size Measured Task Stacks

**Files:**
- Modify: `firmware/main/product/keyboard_matrix.hpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/test/host/test_keyboard_matrix.cpp`
- Modify: `tools/product/tests/test_firmware_memory.py`

**Interfaces:**
- Consumes: hardware high-water values scanner 5,888; macro 5,300; audio 2,140
  bytes free at configurations 8,192; 6,144; 3,072.
- Produces: scanner 4,096; macro 2,048; audio 2,048 configured bytes.

- [ ] **Step 1: Write failing exact-budget tests**

Change the host assertion to:

```cpp
assert(kKeyboardScannerTaskStackBytes == 4096);
```

Add a Python source test requiring:

```python
assert "std::array<StackType_t, 2048> g_macro_task_stack{};" in controller
assert "std::array<StackType_t, 2048> g_audio_task_stack{};" in controller
assert "std::array<StackType_t, 4096> g_ui_task_stack{};" in controller
```

- [ ] **Step 2: Run tests to verify RED**

Run:

```bash
cmake --build build/audio-host --target test_keyboard_matrix -j
ctest --test-dir build/audio-host -R '^keyboard_matrix$' --output-on-failure
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_firmware_memory.py
```

Expected: scanner and product stack budget assertions fail.

- [ ] **Step 3: Apply only the approved stack sizes**

Set `kKeyboardScannerTaskStackBytes = 4096`, macro stack to 2,048, and audio
stack to 2,048. Leave HID and UI stack sizes unchanged.

- [ ] **Step 4: Verify host and target static recovery**

Run:

```bash
cmake --build build/audio-host --target test_keyboard_matrix -j
ctest --test-dir build/audio-host -R '^keyboard_matrix$' --output-on-failure
PYTHONPATH=. uv run pytest -q tools/product/tests/test_firmware_memory.py
scripts/phase0/idf.sh -C firmware build
```

Expected: tests/build pass and target `diram_remain` increases by at least
29,184 bytes relative to 113,529 bytes, reaching at least 142,713 bytes.

- [ ] **Step 5: Commit**

```bash
git add firmware/main/product/keyboard_matrix.hpp \
  firmware/main/product/product_controller.cpp \
  firmware/test/host/test_keyboard_matrix.cpp \
  tools/product/tests/test_firmware_memory.py
git commit -m "fix: recover Cardputer runtime memory"
```

### Task 4: Full Verification and Hardware Resource Gate

**Files:**
- Modify: `docs/2026-07-26-cardputer-ble-microphone_PROGRESS.md`

**Interfaces:**
- Consumes: Tasks 1–3 firmware and the unique `/dev/cu.usbmodem21201`.
- Produces: clean release evidence and a go/no-go result for continuing the
  ten-minute 24 kHz microphone gate.

- [ ] **Step 1: Run the complete pre-flash release gate**

Run:

```bash
swift run --package-path companion product-audio-tests
swift run --package-path companion product-gatt-tests
swift run --package-path companion product-pet-tests
swift run --package-path companion product-telemetry-tests
scripts/verify_product_release.sh
```

Expected: Swift tests, Python suite, normal/sanitizer host tests, ESP-IDF build,
partition/DIRAM checks, packaging, codesign, version, doctor, and hashes pass.

- [ ] **Step 2: Flash and independently verify only the app partition**

Run:

```bash
python -m esptool --chip esp32s3 \
  --port /dev/cu.usbmodem21201 --baud 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x20000 firmware/build/cardputer_codex_companion.bin
python -m esptool --chip esp32s3 \
  --port /dev/cu.usbmodem21201 --baud 460800 \
  --before default_reset --after hard_reset \
  verify_flash 0x20000 firmware/build/cardputer_codex_companion.bin
```

Expected: write hash verification and independent digest match.

- [ ] **Step 3: Capture a 30-second real-device resource smoke**

Collect and validate serial JSON while the normal Mac agent performs HTTPS
polls. Reject the build unless every sample has zero allocation failures, all
task stack gates pass, steady samples meet 65,536/32,768 bytes, and handshake
samples meet 40,960 bytes. Also reject any panic, watchdog, or reboot banner.

- [ ] **Step 4: Record result and commit evidence**

Update the progress document with static DIRAM delta, runtime extrema, stack
minima, image hash, flash verification, and the decision to proceed or stop:

```bash
git add docs/2026-07-26-cardputer-ble-microphone_PROGRESS.md
git commit -m "docs: verify Cardputer runtime memory"
```

Expected: only a passing resource smoke permits resuming Task 9's ten-minute
24 kHz audio/HID gate.
