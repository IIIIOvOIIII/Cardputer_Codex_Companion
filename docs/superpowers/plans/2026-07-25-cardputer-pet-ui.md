# Cardputer Codex Pet UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship firmware `1.0.28` with an explicit boot version, a locally animated 2.5 FPS copy of the pet currently selected by Codex, and read-only detail pages navigated by the Cardputer's reserved Fn direction chords.

**Architecture:** The Mac Companion discovers and transcodes the selected Codex pet into a deterministic `CCPT` RGB565 bundle, then uploads changed bundles over the existing PIN-authenticated LAN channel. Firmware validates uploads into a transactional two-slot SPIFFS cache, animates one bounded frame buffer without full-screen redraws, maps Codex session state to five pet rows, and captures `Fn+;`, `Fn+,`, `Fn+.`, and `Fn+/` locally before Profile or HID routing.

**Tech Stack:** Swift 6, Foundation, ImageIO, CoreGraphics, CryptoKit, XCTest, C++20 host tests, ESP-IDF 5.5.4, M5Unified/M5GFX, SPIFFS, NVS, mbedTLS SHA-256, Python pytest, esptool.

## Global Constraints

- Release version is exactly `1.0.28` in both `firmware/CMakeLists.txt` and `product_types.hpp`.
- The boot page shows `Cardputer Codex Companion` and `v1.0.28`; the pet page does not permanently consume space for the version.
- Main layout is the approved **C: status bar plus pet** layout on the 240×135 display.
- Pet frame size is exactly 96×104 RGB565, with eight frames per state and a 400 ms frame interval.
- Device states and Codex atlas rows are:
  - `idle = 0`;
  - `working = 7` (`running`);
  - `waiting = 6`;
  - `review = 8`;
  - `failed = 5`.
- Official 8×9 v1 atlases and custom 8×11 v2 atlases are supported; v2 look-direction rows 9–10 are ignored.
- The complete bundle is at most `820 * 1024` bytes. The existing `0x1e0000` SPIFFS partition is not resized.
- The bundle content digest is SHA-256 over the whole bundle with bytes 24–55 zeroed. The upload digest is ordinary SHA-256 over the whole file. Both must pass before activation.
- Do not enable PSRAM to make the feature fit. The renderer owns one fixed `96 * 104 * 2 = 19,968` byte frame buffer.
- Upload endpoints require the existing `X-Cardputer-Pairing` header and never expose payload bytes or PIN values.
- Only one upload transaction exists at a time. Invalid or interrupted uploads never replace the active slot.
- Page order is Pet → Connection → Session → Device. Left/Right wrap; Up/Down clamp to valid scroll bounds.
- `Fn+;`, `Fn+,`, `Fn+.`, and `Fn+/` take priority over Profile mappings and HID. Their press and release are both captured; plain punctuation remains unchanged.
- Pet upload, hashing, storage and decode work never run while holding the input/HID mutex.
- Pet animation redraws only the 96×104 rectangle. Status, page body and pagination redraw only when the UI revision changes.
- Mac offline behavior is cached pet + `waiting` animation + `MAC OFF`; first boot without a valid bundle uses a tiny firmware-resident placeholder.
- Do not package the user's selected pet, Codex configuration, local paths, PIN, Wi-Fi data, Profile or BLE bonds into either full image.
- Final deployment is application-only at `0x20000` so existing NVS, Wi-Fi, Profile and BLE bonds survive.
- Work in an isolated git worktree when implementation starts. Do not make feature changes directly in `main`.

---

### Task 1: Add Pet Selection, Atlas Discovery and Session-State Contracts

**Files:**
- Modify: `companion/Package.swift`
- Modify: `companion/Sources/ProductContracts/CompanionDTO.swift`
- Modify: `companion/Sources/CodexAppServer/CodexAdapter.swift`
- Add: `companion/Sources/ProductPet/PetSelectionReader.swift`
- Add: `companion/Sources/ProductPet/PetAtlas.swift`
- Add: `companion/Tests/ProductPetTests/PetSelectionReaderTests.swift`
- Add: `companion/Tests/ProductPetTests/PetAtlasTests.swift`
- Modify: `companion/Tests/ProductContractsTests/CompanionDTOTests.swift`
- Modify: `companion/Tests/CodexAppServerTests/CodexAdapterTests.swift`

**Interfaces:**

```swift
public enum PetState: String, Codable, CaseIterable, Sendable {
    case idle, working, waiting, review, failed
}

public struct PetSource: Equatable, Sendable {
    public enum AtlasVersion: UInt8, Sendable { case v1 = 1, v2 = 2 }
    public let id: String
    public let atlasURL: URL
    public let atlasVersion: AtlasVersion
}

public struct PetSelectionReader {
    public init(environment: [String: String] = ProcessInfo.processInfo.environment)
    public func selectedSource() throws -> PetSource
}

public enum PetAtlas {
    public static let columns = 8
    public static let cellWidth = 192
    public static let cellHeight = 208
    public static let stateRows: [PetState: Int] = [
        .idle: 0, .working: 7, .waiting: 6, .review: 8, .failed: 5
    ]
    public static func validate(_ image: CGImage, version: PetSource.AtlasVersion) throws
}
```

`CompanionSnapshot` gains `petID`, `petDigest`, and `petState`, encoded as
`pet_id`, `pet_digest`, and `pet_state`. Add:

```swift
public init(
    sequence: UInt64,
    sessionID: String,
    title: String,
    cwd: String,
    state: String,
    approvals: UInt8,
    inputs: UInt8,
    petID: String = "",
    petDigest: String = "",
    petState: PetState = .idle
)

public func withPet(id: String, digest: String) -> CompanionSnapshot
```

`withSequence(_:)` and `withPet(id:digest:)` preserve every unrelated field.
`hasSameContent(as:)` must compare all three pet fields.

- [ ] **Step 1: Create the isolated worktree**

Use `superpowers:using-git-worktrees` and create:

```bash
git worktree add .worktrees/feat-cardputer-pet-ui -b feat/cardputer-pet-ui
cd .worktrees/feat-cardputer-pet-ui
```

Expected: the worktree starts from the approved design/plan commit and `git status --short` is empty.

- [ ] **Step 2: Register the ProductPet module and write failing tests**

Add `ProductPet` to package products/targets, link `ImageIO`, `CoreGraphics`, and `CryptoKit`, and add `ProductPetTests`. Make `cardputer-companion` depend on `ProductPet`.

Tests must create temporary Codex homes and cover:

```swift
func testReadsPetOnlyFromTuiTable()
func testUsesCODEXHOMEBeforeHomeDotCodex()
func testChoosesHighestNumericOfficialAssetVersion()
func testRejectsCustomSpritesheetOutsidePetDirectory()
func testAcceptsV2CustomManifest()
func testAtlasAccepts1536By1872V1()
func testAtlasAccepts1536By2288V2()
func testAtlasRejectsWrongDimensions()
func testSnapshotPetFieldsParticipateInContentEquality()
func testSessionStatePriorityFailedWaitingReviewWorkingIdle()
```

Use custom manifest fixture:

```json
{
  "id": "local-pet",
  "displayName": "Local Pet",
  "description": "Fixture",
  "spriteVersionNumber": 2,
  "spritesheetPath": "spritesheet.webp"
}
```

- [ ] **Step 3: Run focused tests and verify RED**

```bash
swift test --package-path companion --filter ProductPetTests
swift test --package-path companion --filter ProductContractsTests
swift test --package-path companion --filter CodexAppServerTests
```

Expected: compilation/test failures because `ProductPet`, pet DTO fields, and state mapping do not exist.

- [ ] **Step 4: Implement bounded selection parsing**

Resolve Codex home in this order:

```swift
let codexHome: URL
if let value = environment["CODEX_HOME"], !value.isEmpty {
    codexHome = URL(fileURLWithPath: value, isDirectory: true)
} else if let value = environment["HOME"], !value.isEmpty {
    codexHome = URL(fileURLWithPath: value, isDirectory: true)
        .appending(path: ".codex", directoryHint: .isDirectory)
} else {
    throw PetSelectionError.missingCodexHome
}
```

Parse only quoted assignments such as `pet = "rocky"` or `pet = 'rocky'`
inside the `[tui]` table. Strip
comments only outside quotes. Reject empty IDs, path separators, `..`, and IDs
over 64 UTF-8 bytes.

For official assets, match:

```text
cache/tui-pets/v1/assets/<id>-spritesheet-v([0-9]+).webp
```

and choose the highest numeric version that decodes to a known atlas size.
For custom pets, decode `pet.json`, require the manifest `id` to equal the
selected ID, accept `spriteVersionNumber` 1 or 2, resolve `spritesheetPath`
relative to the pet directory, call `resolvingSymlinksInPath()`, and require
the result to remain below that directory.

- [ ] **Step 5: Implement session-to-pet-state priority**

In `CodexAdapter.normalize`, map in this order:

```swift
private func petState(status: String, flags: [String]) -> PetState {
    let normalized = status.lowercased()
    if ["failed", "error", "cancelled"].contains(normalized) { return .failed }
    if flags.contains("waitingOnApproval") ||
       flags.contains("waitingOnUserInput") { return .waiting }
    if normalized == "review" || flags.contains("review") { return .review }
    if ["active", "running", "inprogress", "in_progress"].contains(normalized) {
        return .working
    }
    return .idle
}
```

Unknown values fail closed to `.idle`.

- [ ] **Step 6: Run tests and verify GREEN**

```bash
swift test --package-path companion --filter ProductPetTests
swift test --package-path companion --filter ProductContractsTests
swift test --package-path companion --filter CodexAppServerTests
```

Expected: all focused suites pass.

- [ ] **Step 7: Commit Task 1**

```bash
git add companion/Package.swift companion/Sources companion/Tests
git commit -m "feat: discover Codex pet and map session state"
```

---

### Task 2: Build a Deterministic CCPT Transcoder

**Files:**
- Add: `companion/Sources/ProductPet/PetBundle.swift`
- Add: `companion/Sources/ProductPet/PetTranscoder.swift`
- Add: `companion/Tests/ProductPetTests/PetTranscoderTests.swift`
- Add: `companion/Tests/ProductPetTests/PetBundleTests.swift`

**Wire format:**

All integers are unsigned little-endian. Do not serialize Swift structs using
their in-memory layout.

```text
CCPT header, 132 bytes
  0   char[4]  magic = "CCPT"
  4   u16      schema = 1
  6   u16      header_length = 132
  8   u32      total_length
  12  u8       pet_id_length
  13  u8[3]    reserved = 0
  16  u16      width = 96
  18  u16      height = 104
  20  u16      interval_ms = 400
  22  u8       state_count = 5
  23  u8       frames_per_state = 8
  24  u8[32]   content_sha256
  56  u32      state_table_offset = 132
  60  u32      frame_table_offset = 172
  64  u32      payload_offset = 812
  68  u8[64]   zero-padded UTF-8 pet ID

State table: five 8-byte records
  u8 state, u8 reserved, u16 frame_count, u32 first_frame_index

Frame table: forty 16-byte records
  u8 encoding, u8[3] reserved, u32 payload_offset,
  u32 stored_length, u32 decoded_length
```

State enum values are `idle=0`, `working=1`, `waiting=2`, `review=3`,
`failed=4`. Encoding values are `raw_rgb565=0`, `rle_rgb565=1`.

RLE payload is row-scoped:

```text
for each of 104 rows:
  u16 run_count
  repeat run_count:
    u16 pixel_count
    u16 rgb565_pixel
```

Each row must decode to exactly 96 pixels. Use RLE only when its complete frame
payload is strictly smaller than the 19,968-byte raw payload.

**Interfaces:**

```swift
public struct PetBundle: Sendable {
    public static let schemaVersion: UInt16 = 1
    public static let maximumBytes = 820 * 1024
    public let petID: String
    public let data: Data
    public let contentDigestHex: String
    public let uploadDigestHex: String
}

public struct PetTranscoder {
    public init(backgroundRGB888: UInt32 = 0x05080d)
    public func transcode(_ source: PetSource) throws -> PetBundle
}
```

- [ ] **Step 1: Write deterministic bundle tests**

Create a programmatic 1536×1872 fixture with known opaque and transparent
pixels. Assert:

```swift
func testExtractsAllEightFramesFromFiveContractRows()
func testAspectFitsInto96By104WithoutCropping()
func testCompositesTransparencyAgainst05080D()
func testConvertsRGB888ToLittleEndianRGB565()
func testUsesRowRLEOnlyWhenSmaller()
func testRawFrameIsExactly19968Bytes()
func testHeaderAndTableOffsetsMatchWireContract()
func testContentDigestZerosOnlyBytes24Through55()
func testOutputIsByteForByteDeterministic()
func testRejectsBundleOver820KiB()
```

Use RGB565 conversion:

```swift
let pixel = UInt16(((r & 0xF8) << 8) |
                   ((g & 0xFC) << 3) |
                   (b >> 3))
```

- [ ] **Step 2: Run tests and verify RED**

```bash
swift test --package-path companion \
  --filter ProductPetTests.PetTranscoderTests
swift test --package-path companion \
  --filter ProductPetTests.PetBundleTests
```

Expected: failures because the transcoder and wire encoder do not exist.

- [ ] **Step 3: Implement image extraction and scaling**

Use `CGImageSourceCreateWithURL` and `CGImageSourceCreateImageAtIndex`.
Validate dimensions before cropping. Crop each `192×208` cell using the
state-row table, render into a `96×104` RGBA8 context with interpolation
quality `.none`, preserve aspect ratio and center the frame. Composite the
background before drawing the source.

Never retain the decoded 1536×atlas plus all forty target frames at once.
Encode and append each target frame immediately.

- [ ] **Step 4: Implement wire encoding and both digests**

Build header/tables with zero digest bytes, append payload, patch table
offsets/lengths, compute:

```swift
let contentDigest = SHA256.hash(data: bundleWithZeroDigestField)
```

write that digest at bytes 24–55, then compute:

```swift
let uploadDigest = SHA256.hash(data: completedBundle)
```

Return lowercase 64-character hex strings. Throw before returning if the
complete data exceeds `PetBundle.maximumBytes`.

- [ ] **Step 5: Run tests and verify GREEN**

```bash
swift test --package-path companion --filter ProductPetTests
```

Expected: all ProductPet tests pass and repeated test runs produce identical
bundle bytes/digests.

- [ ] **Step 6: Commit Task 2**

```bash
git add companion/Sources/ProductPet companion/Tests/ProductPetTests
git commit -m "feat: transcode Codex pet into CCPT bundle"
```

---

### Task 3: Parse, Validate and Decode CCPT in Firmware

**Files:**
- Add: `firmware/main/product/pet_bundle.hpp`
- Add: `firmware/main/product/pet_bundle.cpp`
- Add: `firmware/test/host/test_pet_bundle.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/main/CMakeLists.txt`

**Interfaces:**

```cpp
enum class PetState : uint8_t { idle, working, waiting, review, failed };
enum class PetFrameEncoding : uint8_t { raw_rgb565, rle_rgb565 };

class PetByteSource {
 public:
  virtual ~PetByteSource() = default;
  virtual std::size_t size() const = 0;
  virtual bool read(std::size_t offset, std::span<uint8_t> output) const = 0;
};

struct PetBundleMetadata {
  std::string pet_id;
  std::array<uint8_t, 32> content_digest;
  uint16_t schema_version;
  uint16_t width;
  uint16_t height;
  uint16_t interval_ms;
  std::array<std::array<PetFrameRecord, 8>, 5> frames;
};

enum class PetBundleError : uint8_t {
  none, truncated, magic, version, bounds, overflow, dimensions,
  utf8, digest, table, encoding, decoded_length, rle
};

PetBundleError validate_pet_bundle(
    const PetByteSource& source,
    const std::optional<std::array<uint8_t, 32>>& expected_upload_digest,
    PetBundleMetadata* output);

PetBundleError decode_pet_frame(
    const PetByteSource& source,
    const PetBundleMetadata& metadata,
    PetState state,
    uint8_t frame,
    std::span<uint16_t, 96 * 104> output);
```

`pet_bundle.cpp` uses OpenSSL SHA-256 under host builds and mbedTLS SHA-256
under `ESP_PLATFORM`; the call site and validation behavior remain identical.

- [ ] **Step 1: Add failing parser/decoder host tests**

Build a small fixture generator in `test_pet_bundle.cpp` and cover:

```cpp
valid_raw_bundle();
valid_rle_bundle();
rejects_wrong_magic_and_schema();
rejects_total_over_820_kib();
rejects_offset_addition_overflow();
rejects_table_or_payload_out_of_range();
rejects_invalid_or_overlong_utf8_pet_id();
rejects_content_digest_mismatch();
rejects_upload_digest_mismatch();
rejects_unknown_state_or_encoding();
rejects_wrong_decoded_length();
rejects_truncated_rle_run();
rejects_rle_row_short_or_oversized();
```

- [ ] **Step 2: Register and run the test in RED**

Add:

```cmake
add_executable(test_pet_bundle
  test_pet_bundle.cpp
  ../../main/product/pet_bundle.cpp
)
target_include_directories(test_pet_bundle PRIVATE ../../main)
target_link_libraries(test_pet_bundle PRIVATE OpenSSL::Crypto)
add_test(NAME pet_bundle COMMAND test_pet_bundle)
```

Run:

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_pet_bundle -j4
```

Expected: compile failure because the parser does not exist.

- [ ] **Step 3: Implement explicit byte parsing**

Use checked helpers:

```cpp
bool checked_range(std::size_t offset, std::size_t length,
                   std::size_t total) {
  return offset <= total && length <= total - offset;
}
```

Never cast input bytes to packed structs. Read every little-endian field
explicitly. Validate all fixed header values, reserved bytes, tables and frame
ranges before digest comparison or decode.

For the content digest, stream the source while substituting 32 zero bytes for
offsets 24–55. When `expected_upload_digest` is present, also stream the
unmodified bytes and compare the upload digest. Boot validation passes
`std::nullopt` because the content digest already protects a previously
activated slot; upload commit passes the declared whole-file digest so both
digests are required before first activation. Do not allocate the whole bundle.

- [ ] **Step 4: Implement bounded raw/RLE decode**

Raw decode reads exactly 19,968 bytes and converts pairs of little-endian bytes
to `uint16_t`. RLE decode resets the row pixel count for each row, rejects
zero-length runs, and requires every row to finish at exactly 96 pixels and
the payload cursor to finish at exactly `stored_length`.

- [ ] **Step 5: Run host and sanitizer tests**

```bash
cmake --build build/product-host --target test_pet_bundle -j4
build/product-host/test_pet_bundle

cmake -S firmware/test/host -B build/product-host-sanitize \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/product-host-sanitize --target test_pet_bundle -j4
build/product-host-sanitize/test_pet_bundle
```

Expected: both executions exit 0 with no sanitizer diagnostics.

- [ ] **Step 6: Register firmware sources and commit**

Add `product/pet_bundle.cpp` to firmware `SRCS`; existing `mbedtls` dependency
is sufficient.

```bash
git add firmware/main firmware/test/host
git commit -m "feat: validate and decode CCPT bundles"
```

---

### Task 4: Add Transactional Two-Slot Pet Storage

**Files:**
- Add: `firmware/main/product/pet_store.hpp`
- Add: `firmware/main/product/pet_store.cpp`
- Add: `firmware/test/host/test_pet_store.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `tools/product/tests/test_partition_layout.py`

**Storage contract:**

```text
SPIFFS label: storage
mount point: /pet
slot A: /pet/slot-a.ccpt
slot B: /pet/slot-b.ccpt
temporary upload: /pet/upload.tmp
NVS namespace: product
NVS keys: pet_slot, pet_digest
```

**Pure policy interfaces:**

```cpp
enum class PetSlot : uint8_t { a, b, none };
constexpr PetSlot inactive_pet_slot(PetSlot active);
PetSlot select_boot_pet_slot(
    PetSlot nvs_selected,
    bool slot_a_valid,
    bool slot_b_valid);
constexpr bool pet_commit_can_activate(
    bool upload_valid,
    bool rename_succeeded,
    bool nvs_commit_succeeded);
```

**Runtime interfaces:**

```cpp
struct PetUploadBegin {
  std::string pet_id;
  uint16_t format_version;
  std::size_t length;
  std::array<uint8_t, 32> upload_digest;
};

struct PetUploadStatus {
  std::string transaction_id;
  std::size_t received;
  std::size_t expected;
};

class PetStore {
 public:
  esp_err_t start();
  esp_err_t begin(const PetUploadBegin&, PetUploadStatus*);
  esp_err_t append(std::string_view transaction_id, std::size_t offset,
                   std::span<const uint8_t> chunk,
                   std::span<const uint8_t, 32> chunk_digest);
  esp_err_t commit(std::string_view transaction_id);
  PetStoreStatus status() const;
  bool decode(PetState state, uint8_t frame,
              std::span<uint16_t, 96 * 104> output);
};
```

`start()` creates a dedicated static `product-pet-upload` task at
`tskIDLE_PRIORITY` plus a bounded command queue. `begin`, `append`, and
`commit` enqueue owned command data and wait for a bounded completion
notification; their filesystem writes/hashes run only in that task. `decode`
is a read-only UI call guarded by the store mutex and does not enter the upload
queue. The command queue owns at most one 8 KiB chunk buffer.

- [ ] **Step 1: Write failing slot-policy and partition tests**

Host tests must prove:

```cpp
assert(inactive_pet_slot(PetSlot::a) == PetSlot::b);
assert(select_boot_pet_slot(PetSlot::a, true, true) == PetSlot::a);
assert(select_boot_pet_slot(PetSlot::a, false, true) == PetSlot::b);
assert(select_boot_pet_slot(PetSlot::none, true, false) == PetSlot::a);
assert(select_boot_pet_slot(PetSlot::none, false, false) == PetSlot::none);
assert(!pet_commit_can_activate(true, true, false));
```

Python partition test must assert the `storage` partition is SPIFFS, starts at
`0x620000`, is `0x1e0000`, and can contain two 820 KiB logical slot maxima
plus at least 256 KiB headroom.

- [ ] **Step 2: Run tests and verify RED**

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_pet_store -j4
python3 -m pytest tools/product/tests/test_partition_layout.py \
  -k pet_storage -q
```

Expected: missing policy symbols/test and missing SPIFFS capacity assertion.

- [ ] **Step 3: Implement SPIFFS boot and fallback**

Mount with `esp_vfs_spiffs_register` for partition label `storage`. It is
acceptable to format only this pet-cache partition when SPIFFS cannot mount;
NVS/Wi-Fi/Profile/BLE bonds are outside it.

At boot:

1. read `pet_slot`;
2. independently validate both existing slots using `validate_pet_bundle`;
3. choose the NVS slot when valid, otherwise the other valid slot;
4. update NVS only when fallback changes the selected slot;
5. publish no active bundle when both fail, leaving the renderer placeholder.

- [ ] **Step 4: Implement transaction rules**

`begin` rejects length 0, length over 820 KiB, schema other than 1, invalid pet
ID/digest, or an already-active transaction. After all request validation, it
removes only the current inactive slot to guarantee room for active bundle +
temporary upload, generates a 16-hex-digit transaction ID from `esp_random()`,
truncates/creates `/pet/upload.tmp`, and stores declared fields in RAM. The
active slot is never removed.

`append` accepts 4096–8192-byte chunks except the final shorter chunk. It:

1. checks transaction ID;
2. hashes the chunk;
3. requires `offset == received`;
4. permits only the immediately previous chunk as an idempotent duplicate when
   offset, length and digest all match;
5. rejects gaps, overlaps and writes beyond declared length.

`commit` flushes/closes the temporary file, validates length, upload digest and
CCPT metadata, requires the CCPT pet ID/schema to equal the begin declaration,
removes only the inactive slot, renames the temp file to that slot, commits
`pet_slot` and `pet_digest` to NVS, then swaps the in-memory active metadata
under the store mutex. Failures before NVS commit keep the previous active slot
selected.

- [ ] **Step 5: Keep upload work below keyboard and UI priority**

Use static FreeRTOS task/queue storage. The upload task priority must remain
below the current UI (`tskIDLE_PRIORITY + 1`), macro
(`tskIDLE_PRIORITY + 2`), keyboard scan and BLE HID tasks. Do not allocate an
8 KiB chunk on an HTTP or task stack; use one heap-owned buffer released by the
upload worker after completion.

- [ ] **Step 6: Register SPIFFS and run tests**

Add `product/pet_store.cpp` to firmware `SRCS` and `spiffs` to
`PRIV_REQUIRES`.

```bash
cmake --build build/product-host --target test_pet_store -j4
build/product-host/test_pet_store
python3 -m pytest tools/product/tests/test_partition_layout.py -q
```

Expected: host policy and all partition tests pass.

- [ ] **Step 7: Commit Task 4**

```bash
git add firmware/main firmware/test/host tools/product/tests/test_partition_layout.py
git commit -m "feat: cache pet bundles transactionally in SPIFFS"
```

---

### Task 5: Expose Authenticated Pet Upload APIs and Sync from Companion

**Files:**
- Modify: `firmware/main/product/product_web.hpp`
- Modify: `firmware/main/product/product_web.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/test/host/test_product_web.cpp`
- Add: `companion/Sources/ProductPet/PetSyncCoordinator.swift`
- Add: `companion/Tests/ProductPetTests/PetSyncCoordinatorTests.swift`
- Modify: `companion/Sources/cardputer-companion/LANBridge.swift`
- Modify: `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Modify: `tools/product/tests/test_companion_packaging.py`

**HTTP contract:**

```text
POST /api/v1/companion/pet/begin
PUT  /api/v1/companion/pet/chunk
POST /api/v1/companion/pet/commit
GET  /api/v1/companion/pet
```

Begin request:

```json
{
  "pet_id":"rocky",
  "format_version":1,
  "length":799532,
  "sha256":"<64 lowercase upload digest>"
}
```

Begin response:

```json
{"transaction_id":"0123456789abcdef","received":0}
```

The begin request uses `bundle.uploadDigestHex` in its `sha256` field. Device
status uses `bundle.contentDigestHex` in `digest`; the two fields must not be
interchanged.

Chunk headers:

```text
X-Pet-Transaction: 0123456789abcdef
X-Pet-Offset: 8192
X-Pet-Chunk-SHA256: <64 lowercase hex>
Content-Type: application/octet-stream
```

Commit request:

```json
{"transaction_id":"0123456789abcdef"}
```

Status response:

```json
{
  "pet_id":"rocky",
  "digest":"<64 lowercase hex>",
  "format_version":1,
  "storage_used":799532,
  "transaction":{"active":false,"received":0,"expected":0},
  "last_result":"ok"
}
```

**Swift interfaces:**

```swift
public protocol PetDeviceClient {
    func petStatus() async throws -> DevicePetStatus
    func beginPetUpload(_ bundle: PetBundle) async throws -> PetUploadReceipt
    func putPetChunk(transactionID: String, offset: Int, data: Data) async throws
    func commitPetUpload(transactionID: String) async throws -> DevicePetStatus
}

public actor PetSyncCoordinator {
    public init(reader: PetSelectionReader, transcoder: PetTranscoder)
    public func synchronize(client: PetDeviceClient) async -> PetSyncResult
}
```

- [ ] **Step 1: Extend route and authentication tests in RED**

Change `kProductWebRoutes` to 12 routes and assert the four new paths/methods
are present and all have `requires_pairing == true`. Add packaging assertions
for `petStatus`, `beginPetUpload`, `putPetChunk`, `commitPetUpload`, and ensure
no PIN appears in `Process.arguments`.

```bash
cmake --build build/product-host --target test_product_web -j4
build/product-host/test_product_web
python3 -m pytest tools/product/tests/test_companion_packaging.py -q
```

Expected: failures because routes/client methods do not exist.

- [ ] **Step 2: Implement firmware handlers**

Raise `max_uri_handlers` from the route array size automatically. Keep request
bodies bounded: begin/commit JSON ≤ 1024 bytes; chunk ≤ 8192 bytes.

Handlers translate `PetStore` errors to stable JSON/status codes:

```text
400 invalid_request, invalid_offset, invalid_digest, invalid_bundle
401 pairing_required
409 upload_in_progress, transaction_mismatch
413 bundle_too_large, chunk_too_large
507 storage_full
500 pet_store_failed
```

Register the store in `EspProductStartup::config()` after NVS initialization,
before Web starts. The Web handler must only append/commit storage; it must not
decode or redraw.

- [ ] **Step 3: Write coordinator tests with a fake client**

Cover:

```swift
func testSkipsTranscodeAndUploadWhenInputKeyAndDeviceDigestMatch()
func testUploadsIn8192ByteChunksAndCommits()
func testRetriesImmediatelyPreviousChunkAfterTransportFailure()
func testResumesFromDeviceReportedReceivedOffset()
func testRetainsLastSuccessWhenSourceIsMissing()
func testBacksOffAfterTranscodeFailureUntilInputChanges()
```

The coordinator cache key is SHA-256 of pet ID, source-file SHA-256, atlas
version/dimensions, transcoder schema, `96×104`, `400`, and background
`0x05080d`. The device comparison uses `bundle.contentDigestHex`, not the cache
key.

- [ ] **Step 4: Run coordinator tests and verify RED**

```bash
swift test --package-path companion \
  --filter ProductPetTests.PetSyncCoordinatorTests
```

Expected: compilation failure because the coordinator/client protocol is absent.

- [ ] **Step 5: Implement secure binary curl transport**

Refactor `LANBridge.runCurl` so request bodies travel on stdin. Write curl
configuration, including the PIN header, to a `mkstemp` file with mode `0600`,
pass only that non-secret path in process arguments, and remove the file in
`defer`. For binary chunk requests configure:

```text
request = "PUT"
data-binary = "@-"
```

Never place the PIN, body, digest, or pet bytes in arguments, stdout, stderr or
logs. Preserve the existing five-second connect timeout; allow 15 seconds for
chunk and commit requests.

- [ ] **Step 6: Integrate synchronization into the two-second loop**

Construct one `PetSyncCoordinator` after the bridge. Each loop:

1. poll/perform the remote action;
2. synchronize selected pet;
3. obtain the Codex snapshot;
4. attach the last successfully synchronized pet ID/content digest;
5. POST only when snapshot content changed.

Pet sync errors write only a stable error code such as
`pet sync warning: source_not_found`; never print local pet paths.

- [ ] **Step 7: Verify Swift, host and Python suites**

```bash
swift test --package-path companion
cmake --build build/product-host --target test_product_web -j4
build/product-host/test_product_web
python3 -m pytest tools/product/tests/test_companion_packaging.py -q
```

Expected: all commands pass.

- [ ] **Step 8: Commit Task 5**

```bash
git add companion firmware/main firmware/test/host tools/product/tests
git commit -m "feat: synchronize pet bundles over authenticated LAN"
```

---

### Task 6: Extend Snapshot State and Build Read-Only Page Navigation

**Files:**
- Modify: `firmware/main/product/companion_protocol.hpp`
- Modify: `firmware/main/product/companion_protocol.cpp`
- Modify: `firmware/test/host/test_companion_protocol.cpp`
- Add: `firmware/main/product/ui_navigation.hpp`
- Add: `firmware/main/product/ui_navigation.cpp`
- Add: `firmware/test/host/test_ui_navigation.cpp`
- Modify: `firmware/main/product/ui_model.hpp`
- Modify: `firmware/main/product/ui_model.cpp`
- Modify: `firmware/test/host/test_ui_model.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`

**Interfaces:**

```cpp
enum class UiPage : uint8_t { pet, connection, session, device };
enum class UiNavAction : uint8_t {
  none, previous_page, next_page, scroll_up, scroll_down
};

struct UiNavigationResult {
  bool captured;
  UiNavAction action;
};

class UiNavigation {
 public:
  UiNavigationResult on_key(uint8_t physical_key, bool pressed,
                            bool fn_pressed);
};
```

Reserved physical keys are constants, not HID usages:

```cpp
constexpr uint8_t kNavUpPhysicalKey = 39;    // ;
constexpr uint8_t kNavLeftPhysicalKey = 52;  // ,
constexpr uint8_t kNavDownPhysicalKey = 53;  // .
constexpr uint8_t kNavRightPhysicalKey = 54; // /
```

`UiModel` gains page, scroll, pet/session/device details, heartbeat/pet-sync
ages, and:

```cpp
void navigate(UiNavAction action);
void set_pet(std::string_view id, std::string_view digest,
             PetState state, std::string_view sync_result);
void set_pet_storage(uint32_t used_bytes, uint16_t format_version);
void set_heartbeat_age(uint32_t seconds);
void set_pet_sync_age(uint32_t seconds);
UiPage page() const;
uint8_t scroll_offset() const;
struct UiPageContent {
  std::array<std::string, 12> lines;
  uint8_t count;
};

UiPageContent page_content() const;
```

- [ ] **Step 1: Write failing protocol tests**

Extend snapshot fixture with:

```json
"pet_id":"rocky",
"pet_digest":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
"pet_state":"waiting"
```

Assert strict pet-state parsing, ID/digest clipping/validation, unknown state
to idle, and stale fallback through:

```cpp
PetState effective_pet_state(bool companion_stale,
                             PetState snapshot_state);
```

- [ ] **Step 2: Write failing navigation/UI tests**

Cover:

```cpp
pet_page_is_boot_default();
left_and_right_wrap_all_four_pages();
up_and_down_clamp_to_page_line_bounds();
fn_semicolon_captures_press_and_release();
fn_comma_period_slash_map_to_left_down_right();
plain_punctuation_is_not_captured();
captured_chord_ignores_profile_mapping();
page_lines_include_connection_session_and_device_fields();
device_page_contains_1_0_28_and_maskable_pin_source();
```

The Device page intentionally displays the current PIN because the approved
device detail contract requires it; it must remain read-only.

- [ ] **Step 3: Run tests and verify RED**

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host \
  --target test_companion_protocol test_ui_navigation test_ui_model -j4
```

Expected: missing types/methods and snapshot fields.

- [ ] **Step 4: Implement protocol and page model**

Validate `pet_digest` as exactly 64 lowercase hex characters; invalid values
become empty and do not replace active store metadata. Clamp page scroll to:

```cpp
  max_scroll = content.count > visible_lines
                 ? content.count - visible_lines
                 : 0;
```

Every boot constructs `UiModel` on `UiPage::pet`; navigation is not persisted.

- [ ] **Step 5: Capture local navigation before Profile/HID routing**

In `keyboard_event`, after pairing-digit handling and before layer/Profile
lookup:

1. call `UiNavigation::on_key`;
2. when captured, call `KeyboardProbe::on_mode_changed()` to send release-all;
3. apply the action to `UiModel` only on the captured press;
4. return for both press and release.

Do not take `g_ui_mutex` while sending HID or while holding it across storage
work. Keep the current pairing digit path higher priority than navigation.

- [ ] **Step 6: Verify host and sanitizer suites**

```bash
cmake --build build/product-host -j4
ctest --test-dir build/product-host --output-on-failure

cmake --build build/product-host-sanitize -j4
ctest --test-dir build/product-host-sanitize --output-on-failure
```

Expected: all host tests pass, including original punctuation/Profile cases,
with no sanitizer errors.

- [ ] **Step 7: Commit Task 6**

```bash
git add firmware/main/product firmware/test/host
git commit -m "feat: add pet state pages and local Fn navigation"
```

---

### Task 7: Render the Approved Pet UI Without Flicker

**Files:**
- Modify: `firmware/main/product/display.hpp`
- Modify: `firmware/main/product/display.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `tools/product/tests/test_companion_packaging.py`
- Modify: `tools/product/tests/test_firmware_memory.py`

**Display contract:**

```text
status bar: x=0, y=0, w=240, h=18
pet frame:  x=72, y=20, w=96, h=104
page dots:  centered, y=131
frame cadence: exactly 400 ms
```

**Interfaces:**

```cpp
void display_render_boot(const UiModel& model);
void display_render_page(const UiModel& model, const PetStoreStatus& pet);
bool display_render_pet_frame(PetStore& store, PetState state,
                              uint8_t frame_index);
void display_render_placeholder(PetState state);
```

- [ ] **Step 1: Add failing source/memory contract tests**

Assert:

```python
def test_pet_renderer_uses_bounded_frame_buffer_and_partial_push():
    display = Path("firmware/main/product/display.cpp").read_text()
    assert "std::array<uint16_t, 96 * 104>" in display
    assert "pushImage(kPetX, kPetY, 96, 104" in display
    assert "kPetFrameIntervalMs = 400" in display

def test_pet_frame_tick_does_not_clear_full_screen():
    display = Path("firmware/main/product/display.cpp").read_text()
    frame_body = display.split("display_render_pet_frame", 1)[1]
    assert "fillScreen" not in frame_body.split("}", 1)[0]
```

Extend firmware memory tests to require at least 96 KiB target DIRAM headroom
after the static 19,968-byte frame buffer is linked.

- [ ] **Step 2: Run tests and verify RED**

```bash
python3 -m pytest \
  tools/product/tests/test_companion_packaging.py \
  tools/product/tests/test_firmware_memory.py -q
```

Expected: renderer contract test fails.

- [ ] **Step 3: Implement boot and full-page rendering**

Boot header is rendered as two explicit lines:

```text
Cardputer Codex Companion
v1.0.28
```

Then render existing startup stages. Runtime page full redraws happen only when
`UiModel.revision()` or selected page changes. The Pet page draws the 18-pixel
state/connectivity bar and page dots; Connection/Session/Device pages draw
their read-only lines with current body text size 2 and clipped horizontal
content.

- [ ] **Step 4: Implement independent 400 ms frame ticks**

In `ui_task`, track:

```cpp
uint64_t next_frame_ms = 0;
uint8_t frame_index = 0;
```

Every 200 ms task iteration:

- full-render on UI revision changes;
- if current page is Pet and `now_ms >= next_frame_ms`, advance modulo 8 and
  decode/push only the pet rectangle;
- when state changes, reset `frame_index = 0`;
- if decode fails/no bundle, draw the firmware placeholder in the same
  rectangle;
- set `next_frame_ms += 400`, correcting to `now_ms + 400` after long delays.

Copy the required UI state before releasing `g_ui_mutex`; perform file reads,
RLE decode and `pushImage` after the mutex is released.

- [ ] **Step 5: Verify focused tests and target build**

```bash
python3 -m pytest \
  tools/product/tests/test_companion_packaging.py \
  tools/product/tests/test_firmware_memory.py -q
rm -f firmware/sdkconfig firmware/sdkconfig.old
(
  cd firmware
  ../scripts/phase0/idf.sh set-target esp32s3
  ../scripts/phase0/idf.sh build
)
```

Expected: target build passes without enabling PSRAM. Capture the size report
and confirm `diram_remain >= 98304`.

- [ ] **Step 6: Commit Task 7**

```bash
git add firmware/main tools/product/tests
git commit -m "feat: render cached pet animation and detail pages"
```

---

### Task 8: Release Firmware 1.0.28 and Preserve Private-State Boundaries

**Files:**
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify: `firmware/test/host/test_product_types.cpp`
- Modify: `tools/product/tests/test_private_packaging.py`
- Modify: `tools/product/tests/test_companion_packaging.py`
- Modify: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`

- [ ] **Step 1: Set the failing version assertions**

```cpp
static_assert(kProductVersion == std::string_view{"1.0.28"});
static_assert(
    kProductBootTitle ==
    std::string_view{"CARDPUTER CODEX COMPANION"});
```

The display prints `kProductVersion` separately as `v1.0.28`.

- [ ] **Step 2: Run the version test and verify RED**

```bash
cmake --build build/product-host --target test_product_types -j4
```

Expected: compile failure because the current source is 1.0.27 and the old
boot title includes the version.

- [ ] **Step 3: Update both version sources**

Set:

```cmake
set(PROJECT_VER "1.0.28")
```

and:

```cpp
inline constexpr std::string_view kProductVersion = "1.0.28";
inline constexpr std::string_view kProductBootTitle =
    "CARDPUTER CODEX COMPANION";
```

Run:

```bash
cmake --build build/product-host --target test_product_types -j4
build/product-host/test_product_types
```

Expected: version test passes.

- [ ] **Step 4: Add release-boundary tests**

Assert neither packaging script nor tracked/dist full-image inputs include:

```text
.codex/config.toml
.codex/cache/tui-pets
.codex/pets
slot-a.ccpt
slot-b.ccpt
upload.tmp
pet.json
spritesheet.webp
```

Assert the Companion bundle contains the transcoder code but no concrete
selected pet. Keep the existing generated/private artifact tracking checks.

- [ ] **Step 5: Run the complete release gate**

```bash
scripts/verify_product_release.sh
```

Expected:

- all Python tests pass;
- all normal and ASan/UBSan host tests pass;
- Web asset freshness passes;
- ESP-IDF target build and partition verification pass;
- DIRAM headroom is at least 96 KiB;
- Swift release build, version and doctor pass;
- generic/private full images and Companion app are built;
- private material exclusion checks pass.

- [ ] **Step 6: Record release hashes and commit**

```bash
shasum -a 256 \
  firmware/build/cardputer_codex_companion.bin \
  dist/cardputer_codex_companion-full.bin \
  dist/private/cardputer_codex_companion-private-full.bin \
  dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion

git add firmware companion tools scripts docs
git commit -m "feat: release Cardputer pet UI 1.0.28"
```

Do not commit `build/`, `dist/`, `firmware/sdkconfig`, generated pet bundles or
any user configuration.

---

### Task 9: Install, App-Only Flash and Perform Real-Hardware Acceptance

**Files:**
- Modify: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`
- Modify outside the repo after deployment:
  `/Users/nicholasliao/clawd/memory/2026-07-25.md`

**Preconditions:**

- Cardputer serial device resolves to `/dev/cu.usbmodem21201`, or replace only
  the port value after discovering the current `/dev/cu.usbmodem*`.
- Device remains reachable at `https://192.168.1.195`, or use its current
  DHCP address from serial/status.
- Existing Companion config remains at
  `~/Library/Application Support/CardputerCodexCompanion/config.json`.

- [ ] **Step 1: Install and reload the Companion**

```bash
scripts/build_companion.sh
python3 scripts/install_companion_launch_agent.py --load
/bin/launchctl print "gui/$(id -u)/com.lynx.cardputer-companion"
```

Expected: LaunchAgent state is running and points to the worktree-built
`dist/CardputerCompanion.app`.

- [ ] **Step 2: Flash only the application partition**

```bash
.tools/espressif/python_env/idf5.5_py3.14_env/bin/python -m esptool \
  --chip esp32s3 \
  --port /dev/cu.usbmodem21201 \
  --baud 460800 \
  --before default_reset \
  --after hard_reset \
  write_flash \
  --flash_mode dio \
  --flash_freq 80m \
  --flash_size 8MB \
  0x20000 firmware/build/cardputer_codex_companion.bin
```

Expected: esptool reports `Hash of data verified`. Never flash the private full
image at `0x0` for this upgrade because that would replace persistent state.

- [ ] **Step 3: Verify boot, version and service health**

Capture at least 45 seconds of serial output and verify:

```text
App version: 1.0.28
product runtime started
HTTPS server listening
```

Reject the release if logs contain `Guru Meditation`, `abort()`, stack
overflow, watchdog reset, SPIFFS assert, heap failure or repeated boot banners.

Authenticate with the current PIN without printing it:

```bash
curl -sk https://192.168.1.195/api/v1/status
```

Expected: version `1.0.28`, Wi-Fi OK, BLE OK/starting during reconnect, and
Companion becomes OK within the existing stale/retry window.

- [ ] **Step 4: Verify pet upload and persistence**

Confirm Companion logs show one successful pet sync without exposing a local
path. Authenticated `GET /api/v1/companion/pet` must report:

- selected pet ID (`rocky` on the current Mac unless the user changed it);
- 64-character digest;
- format version 1;
- transaction inactive;
- `last_result = ok`.

Restart Cardputer without restarting the Mac Companion. The same digest must be
active immediately after boot and the cached pet must animate before the next
Companion poll.

- [ ] **Step 5: Verify screen behavior visually**

Observe for at least 60 seconds:

- boot page visibly shows `v1.0.28`;
- Pet page uses top status bar + centered pet + page dots;
- animation advances 2–3 times per second;
- no full-screen flashing occurs;
- active Codex work selects `WORKING`;
- pending input/approval selects `WAITING`;
- stopping the Companion retains the pet, shows `MAC OFF`, and keeps the
  waiting loop.

- [ ] **Step 6: Verify navigation and HID isolation**

With a Mac text field focused:

1. type plain `; , . /` and confirm all four characters reach the Mac;
2. press `Fn+/` three times and confirm pages advance
   Pet → Connection → Session → Device;
3. press `Fn+,` and confirm previous-page navigation;
4. on Session/Device pages use `Fn+;` and `Fn+.` to scroll;
5. confirm no arrow, punctuation, stuck modifier or configured macro is emitted
   to the Mac for any captured navigation chord;
6. confirm an unrelated mapped combination/string key still works.

- [ ] **Step 7: Run a reboot/soak regression**

Run at least three Cardputer restarts and a 10-minute idle observation. Verify:

- BLE reconnects without re-pairing;
- Wi-Fi/PIN/Profile remain unchanged;
- Companion returns online;
- cached pet remains valid;
- no display flashing, upload loop, heap decay, panic or reset occurs.

- [ ] **Step 8: Merge, verify main, document and close**

Use `superpowers:finishing-a-development-branch` to fast-forward the reviewed
branch into `main`. On `main`, rerun:

```bash
scripts/verify_product_release.sh
```

Rebuild/reinstall the Companion and repeat the app-only flash from `main` so
the checked-out source, installed agent, device image and delivered artifacts
are identical.

Update the project progress document and
`/Users/nicholasliao/clawd/memory/2026-07-25.md` with:

- implementation summary;
- final test counts;
- target memory headroom;
- application/private image SHA-256;
- serial port and app-only flash result;
- pet digest persistence result;
- BLE/HID/navigation/soak evidence;
- root cause of any issue encountered.

Then commit documentation:

```bash
git add docs
git commit -m "docs: record Cardputer pet UI verification"
```

The repository currently has no remote. Confirm with `git remote -v`; do not
claim a push. Deliver:

```text
dist/private/cardputer_codex_companion-private-full.bin
```

and separately state that the installed upgrade used:

```text
firmware/build/cardputer_codex_companion.bin at 0x20000
```

---

## Final Acceptance Checklist

- [ ] Version `1.0.28` appears on boot and Device page.
- [ ] The selected Codex pet is discovered from `[tui].pet`.
- [ ] Official v1 and custom v2 atlas contracts are validated.
- [ ] Forty deterministic 96×104 frames are encoded with raw-or-row-RLE.
- [ ] Both CCPT content SHA-256 and upload SHA-256 are verified.
- [ ] Two-slot SPIFFS commit survives interruption and reboot fallback.
- [ ] Identical device digest skips re-upload.
- [ ] Pet state follows failed > waiting > review > working > idle priority.
- [ ] Pet animates locally at 400 ms per frame with a ~20 KiB buffer.
- [ ] Only the pet rectangle redraws on animation ticks.
- [ ] Pet/Connection/Session/Device pages navigate and scroll correctly.
- [ ] Fn navigation press/release never reaches HID or Profile actions.
- [ ] Plain `; , . /` and unrelated Profile mappings remain functional.
- [ ] Cached pet remains animated when Mac/Wi-Fi is unavailable.
- [ ] First boot without cache displays a safe resident placeholder.
- [ ] PIN, Wi-Fi, Profile and BLE bonds survive app-only flashing/restarts.
- [ ] Full release gate, real-device reboot and 10-minute soak pass.
- [ ] No user pet data or local Codex paths enter full images or git.
