# Cardputer Codex Pet UI Design

## Goal

Replace the Cardputer runtime text screen with the currently selected Codex
desktop pet, animated locally at 2–3 FPS and driven by the active Codex session
state. Preserve the Cardputer's Bluetooth keyboard behavior and expose detailed
runtime information through multiple read-only pages navigated with the
Cardputer's existing Fn direction chords.

The release target is firmware version `1.0.28`.

## Confirmed Product Decisions

- The Cardputer follows the pet selected by Codex on the Mac.
- The pet animation follows Codex session state.
- The selected main-screen layout is **C: status bar plus pet**.
- Firmware version is shown on the boot screen and device-details page, not
  permanently on the pet page.
- Detailed information is split across multiple read-only pages.
- `Fn+;`, `Fn+,`, `Fn+.`, and `Fn+/` are reserved for local navigation as
  Up, Left, Down, and Right.
- Left and Right change pages; Up and Down scroll the current page.
- The last successfully synchronized pet remains available when the Mac is
  offline.

## Existing Constraints

- Display: 240×135 pixels, landscape.
- Flash: 8 MiB.
- Product storage partition: SPIFFS, offset `0x620000`, size `0x1e0000`.
- Existing UI task cadence: 200 ms.
- Existing physical key map:
  - physical key 39: `;`, Fn usage Up;
  - physical key 52: `,`, Fn usage Left;
  - physical key 53: `.`, Fn usage Down;
  - physical key 54: `/`, Fn usage Right.
- Existing Companion transport uses PIN-authenticated HTTPS over the LAN.
- Keyboard scan and HID delivery must remain independent of display, storage,
  and network work.

## Considered Architectures

### Companion transcoding with device cache — selected

The Mac Companion discovers the current Codex pet, converts the relevant
animation rows into a bounded device-native bundle, and uploads it only when
the content digest changes. The Cardputer stores one active pet and animates it
locally.

Benefits:

- supports official and custom Codex pets;
- continues animating without the Mac or Wi-Fi;
- avoids WebP decoding and large image transforms on the ESP32-S3;
- does not require a firmware release when Codex adds or changes pets.

### Bundle every official pet in firmware — rejected

This reduces runtime synchronization to a pet ID, but consumes application/OTA
space, does not support custom pets, and requires firmware updates for pet
catalog changes.

### Stream every frame from the Mac — rejected

This avoids persistent assets on the Cardputer but makes the main screen
dependent on network timing and Companion availability. It would also add
continuous load to the existing HTTPS and heartbeat path.

## Runtime Pages

### Boot page

The first device screen shows:

- `Cardputer Codex Companion`;
- the explicit release version, for example `v1.0.28`;
- the existing startup stages and their state.

After startup completes, the UI selects the pet page.

### Pet page

The pet page uses the approved status-bar layout:

- top 18-pixel status bar:
  - `IDLE`, `WORKING`, `WAITING`, `REVIEW`, or `FAILED`;
  - compact BLE, Wi-Fi, and Mac state;
- central 96×104 pet animation region;
- bottom page-position dots.

The top bar redraws only when state changes. The pet region updates at exactly
400 ms per frame, producing 2.5 FPS.

### Connection page

Read-only fields:

- BLE state;
- Wi-Fi state;
- Mac Companion state;
- IPv4 address;
- last Companion heartbeat age;
- last pet synchronization result and age.

### Session page

Read-only fields:

- active session title;
- session state;
- working directory;
- pending approval count;
- pending user-input count.

Up and Down scroll when content exceeds the visible area.

### Device page

Read-only fields:

- firmware version;
- active Profile name;
- current Web PIN;
- current pet ID;
- shortened pet-bundle digest;
- pet storage usage;
- pet bundle format version.

The page selection is volatile. Every boot returns to the pet page and does
not add an NVS write.

## Local Navigation

The UI controller recognizes these reserved chords:

| Physical chord | Local action |
| --- | --- |
| `Fn+,` | Previous page |
| `Fn+/` | Next page |
| `Fn+;` | Scroll up |
| `Fn+.` | Scroll down |

Rules:

1. Reserved Fn chords have priority over Web Profile actions.
2. When a reserved chord is recognized, firmware sends HID `release all`
   before applying the local action.
3. The direction usage is not forwarded to the Mac.
4. Release events for a captured chord remain captured.
5. The unmodified `;`, `,`, `.`, and `/` keys keep their normal HID behavior.
6. All other existing chords, strings, Profile mappings, BLE pairing input,
   Safe Profile, and Home/G0 behavior remain unchanged.

## Mac Companion Components

### `PetSelectionReader`

The reader:

1. resolves `${CODEX_HOME:-$HOME/.codex}`;
2. parses `[tui].pet` from `config.toml`;
3. supports official pet IDs and local custom pet IDs;
4. reports the selected pet ID and a resolved source descriptor.

Official pet discovery searches:

```text
${CODEX_HOME}/cache/tui-pets/v1/assets/<id>-spritesheet-v*.webp
```

It chooses the highest numeric asset version whose dimensions match a known
Codex atlas.

Custom pet discovery reads:

```text
${CODEX_HOME}/pets/<id>/pet.json
${CODEX_HOME}/pets/<id>/<spritesheetPath>
```

The custom manifest must satisfy the Codex pet contract. Paths escaping the pet
directory are rejected.

### `PetTranscoder`

Supported source atlases:

- 8×9, 1536×1872, cell 192×208;
- 8×11 v2, 1536×2288, cell 192×208.

The transcoder extracts all eight frames from these rows:

| Device state | Codex row |
| --- | --- |
| `idle` | idle |
| `working` | running |
| `waiting` | waiting |
| `review` | review |
| `failed` | failed |

Each source frame is:

1. decoded on macOS;
2. aspect-fit into 96×104 without upscaling beyond the source;
3. composited against the Cardputer pet-page background;
4. converted to RGB565;
5. encoded with deterministic row-based run-length encoding when that is
   smaller than raw RGB565, otherwise stored as raw RGB565.

No WebP or alpha decoding is required in firmware.

### `PetSyncCoordinator`

The coordinator computes a synchronization digest from:

- selected pet ID;
- source-file SHA-256;
- source atlas version/dimensions;
- transcoder format version;
- output dimensions and background color.

It polls selection/source metadata with the existing two-second Companion loop
but transcodes only when this digest changes. It asks the Cardputer for its
active digest before uploading and skips identical content.

## Device Pet Bundle

The versioned binary format uses magic `CCPT`.

Header fields:

- magic;
- schema version;
- header length;
- total length;
- pet ID length and UTF-8 bytes;
- frame width `96`;
- frame height `104`;
- frame interval `400 ms`;
- state count `5`;
- frames per state `8`;
- content SHA-256, computed over the complete bundle with this digest field
  zeroed;
- state table offset;
- frame table offset;
- payload offset.

Each state-table entry maps one `PetState` enum to eight frame-table entries.
Each frame-table entry contains an encoding enum (`raw_rgb565` or
`rle_rgb565`), payload offset, stored length, and decoded RGB565 length.

Validation rules:

- package total length must not exceed 820 KiB;
- all integer additions and offset ranges are overflow checked;
- every table and frame range must remain inside the package;
- decoded frame length must equal `96 × 104 × 2`;
- RLE runs must end exactly at the expected decoded length;
- pet ID must be valid UTF-8 and at most 64 bytes;
- the header content digest and upload-level whole-file SHA-256 must both match
  before activation.

The raw image payload is 798,720 bytes before tables and headers. The
raw-or-RLE selection keeps the complete bundle under the 820 KiB limit. Two
820 KiB slots plus their small manifests remain within the existing
`0x1e0000` storage partition.

## Transactional Upload

All endpoints require the existing `X-Cardputer-Pairing` PIN authentication.
They are intended for the Mac Companion, not the browser session.

### Begin

`POST /api/v1/companion/pet/begin`

JSON request:

```json
{
  "pet_id": "rocky",
  "format_version": 1,
  "length": 123456,
  "sha256": "..."
}
```

The device validates the declared bounds, creates a new upload transaction,
and opens a temporary SPIFFS file. Only one transaction may exist at a time.

### Chunk

`PUT /api/v1/companion/pet/chunk`

Headers:

- transaction ID;
- byte offset;
- chunk SHA-256.

Body:

- 4–8 KiB binary chunk.

The offset must equal the current committed temporary-file length. Duplicate
retries of the immediately preceding chunk are accepted only when offset,
length, and digest match; gaps and conflicting rewrites are rejected.

### Commit

`POST /api/v1/companion/pet/commit`

The device:

1. closes and flushes the temporary file;
2. verifies total length and upload-level whole-file SHA-256;
3. validates the complete `CCPT` structure;
4. atomically renames the temporary file to the inactive slot;
5. commits the active-slot ID and content digest to the existing product NVS
   namespace;
6. publishes the new bundle to the renderer.

Any error removes only temporary data. The previously active pet is preserved.
At boot, firmware validates the NVS-selected slot and falls back to the other
valid slot if power was lost between the SPIFFS rename and NVS commit.

### Status

`GET /api/v1/companion/pet`

Returns active pet ID, digest, bundle version, storage usage, transaction state,
and last synchronization result. It never returns pet payload bytes or PIN.

## Session-to-Pet State Mapping

`CompanionSnapshot` adds:

- `pet_id`;
- `pet_digest`;
- `pet_state`.

State priority:

1. failed/error state → `failed`;
2. waiting on approval or user input → `waiting`;
3. review state → `review`;
4. active/in-progress execution → `working`;
5. otherwise → `idle`.

Unknown values fail closed to `idle`. When the Companion becomes stale, the
firmware retains the cached pet, changes the top bar to `MAC OFF`, and plays
the `waiting` row.

## Rendering and Concurrency

- The animation timer advances every 400 ms independently of `UiModel`
  revisions.
- A single frame is decoded into a bounded approximately 20 KiB RGB565 buffer.
- Only the 96×104 pet rectangle is pushed to the display on frame ticks.
- Header, page body, and pagination redraw only when their model revision
  changes.
- Storage reads and RLE decoding run outside the keyboard/HID lock.
- Pet upload runs in a lower-priority task than keyboard scan, HID sender, and
  UI rendering.
- Network handlers never decode images or redraw the display.
- The active bundle handle changes only after commit validation succeeds.

These boundaries preserve keyboard latency and prevent full-screen animation
flicker.

## Fallback and Error Handling

- Missing or invalid selected-pet source: retain the last active bundle and
  expose `source_not_found` or `source_invalid`.
- Transcode failure: retain the last bundle and retry only after source digest
  changes or the bounded backoff expires.
- Interrupted upload: retain the last bundle and resume/restart using the
  transaction status.
- Invalid offset, chunk digest, package digest, or bundle structure: reject the
  transaction and preserve the active bundle.
- Insufficient SPIFFS space: remove temporary content, preserve the active
  bundle, and report `storage_full`.
- First boot without a valid cache: render a tiny firmware-resident default
  idle placeholder until synchronization succeeds.
- Unknown session state: use `idle`.
- Mac timeout: use cached pet plus `waiting`.

No failure in this feature may stop BLE HID, Wi-Fi recovery, Web configuration,
or the Companion snapshot loop.

## Test Strategy

### Swift tests

- TOML `[tui].pet` parsing and missing-value fallback;
- official asset version selection;
- custom `pet.json` parsing and path traversal rejection;
- 8×9 and 8×11 atlas validation and row extraction;
- deterministic resize, RGB565 conversion, and RLE output;
- digest changes for each relevant input and remains stable otherwise;
- skip upload when device digest matches;
- chunk retry, interrupted transaction, and commit recovery;
- session fields map to all five `pet_state` values.

### Firmware host tests

- `CCPT` header, version, size, offset, overflow, UTF-8, and SHA validation;
- raw-frame and RLE happy paths, truncated run, oversized run, and exact
  output-length checks;
- active/inactive slot selection and failure-preserving commit policy;
- pet state parsing and offline fallback;
- page order, page wrap, scroll bounds, and boot default;
- all four Fn navigation chords are captured;
- captured press and release are not emitted over HID;
- unmodified punctuation continues through HID;
- non-navigation Profile mappings remain unchanged.

### Packaging and release tests

- SPIFFS is enabled and mounted with the existing partition label;
- pet endpoints require PIN authentication and enforce body/chunk limits;
- renderer uses a 400 ms frame interval and bounded frame buffer;
- generated version sources agree on `1.0.28`;
- generic and private full images include the updated application while
  excluding private pet content and local Codex paths.

### Hardware acceptance

1. Boot and confirm explicit `v1.0.28`.
2. Confirm the selected Codex pet appears in layout C.
3. Observe 2.5 FPS playback for ten minutes without full-screen flicker,
   reboot, panic, heap failure, or stack overflow.
4. Exercise idle, working, waiting, review, and failed states.
5. Change `[tui].pet`, then confirm automatic one-time synchronization and
   persistent playback after Mac or Wi-Fi disconnect.
6. Traverse all four pages with Fn direction chords and verify scroll bounds.
7. Confirm no navigation chord reaches a Mac text field.
8. Confirm unmodified `;`, `,`, `.`, and `/` still type normally.
9. Confirm BLE, Wi-Fi, Mac Companion, Web PIN, Profile, and existing mappings
   remain intact across reboot.

## Release Sequence

1. Add tests before implementation for every new parser, protocol, renderer,
   and navigation rule.
2. Build and install the updated Mac Companion first.
3. Build firmware `1.0.28` and pass the complete product release gate.
4. Flash the application image at `0x20000` to preserve NVS, Wi-Fi, Profile,
   and BLE bonds.
5. Allow the updated Companion to perform the first pet synchronization.
6. Complete hardware acceptance and generate a new private full image.
