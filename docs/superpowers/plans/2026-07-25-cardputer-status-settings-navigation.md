# Cardputer Status and Settings Navigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship Cardputer Codex Companion `1.0.30` with five device pages, read-only Codex model/Thinking/Fast/rate-limit telemetry, transactional multi-Profile storage, and a scrollable on-device Settings hierarchy for PIN, Wi-Fi, Profile, brightness, return timeout, and pet FPS.

**Architecture:** Extend the existing serialized, PIN-authenticated Companion snapshot rather than adding a second device transport. Keep telemetry parsing, raw Profile catalog storage, local Settings input, Wi-Fi staging, and PIN migration in focused modules with host-testable interfaces; `product_controller.cpp` only wires those modules to ESP-IDF, M5Unified, BLE, and the current Web server.

**Tech Stack:** Swift 6, Foundation, XCTest, Codex app-server JSON-RPC, C++20 host tests, ESP-IDF 5.5.4, M5Unified/M5GFX, raw ESP partition I/O, NVS, cJSON, static HTML/CSS/JavaScript, Python pytest, esptool, macOS launchd.

## Global Constraints

- Start execution from an isolated `feat/status-settings-navigation` worktree created with `superpowers:using-git-worktrees`; do not reuse the four existing stale worktrees.
- The release version is `1.0.30` in both `firmware/CMakeLists.txt` and `firmware/main/product/product_types.hpp`.
- Page order is exactly `Pets -> Device -> Codex -> Sync -> Settings`.
- Outside Settings, page and scroll navigation remains `Fn+,`, `Fn+/`, `Fn+;`, and `Fn+.`.
- In Settings browse states, bare `;`, `.`, `,`, and `/` mean Up, Down, Back, and Enter.
- In Settings text edit, punctuation is text; no physical key reaches Profile, Macro, BLE HID, or Unicode GATT dispatch.
- Fast is displayed immediately after Model.
- Missing, ambiguous, or older-than-120-second rate-limit data creates no UI row; never display `N/A`, zero, or a guessed value.
- Rate-limit refresh is at most once per 60 seconds and must not add an independent Cardputer request stream.
- Support one immutable SAFE Profile plus at most four custom Profiles.
- Use only storage offsets `0x1c0000..0x1dffff` for Profile Catalog banks; do not change `partitions_product.csv` or either pet slot.
- Profile catalog payload is at most 60 KiB and is copied/validated in bounded chunks; never allocate the complete catalog on internal RAM.
- Wi-Fi candidate credentials are committed only after `IP_EVENT_STA_GOT_IP`; failure restores the prior connection.
- PIN remains exactly eight digits. Previous-PIN grace is five minutes, RAM-only, and valid only for `GET /api/v1/companion/action`.
- Cardputer requests from the Mac Companion remain serialized.
- BLE HID descriptors, BLE security, Unicode GATT, pet bundle format, OTA, Secure Boot, eFuse, and off-LAN access are unchanged.
- Upgrade an already configured device by flashing only the application image at `0x20000`.
- Never print or store a PIN, Wi-Fi password, account identifier, or raw credential in tests, logs, docs, Git, or memory.

---

## File Structure

### New focused modules

- `companion/Sources/ProductContracts/CodexTelemetry.swift` — stable Codable telemetry DTOs shared by the Agent snapshot.
- `companion/Sources/CodexAppServer/CodexTelemetryReader.swift` — app-server session runtime and rate-limit normalization/cache.
- `firmware/main/product/profile_codec.hpp/.cpp` — sparse Profile JSON encode/decode extracted from the Web server.
- `firmware/main/product/profile_catalog.hpp/.cpp` — raw two-bank catalog index, streaming copy, migration, and active Profile lookup.
- `firmware/main/product/device_settings.hpp/.cpp` — versioned display settings and CRC-protected persistence policy.
- `firmware/main/product/settings_menu.hpp/.cpp` — pure local menu/editor state machine and bounded input buffers.
- `firmware/main/product/pin_rotation.hpp/.cpp` — current/previous PIN authorization scope and revision rules.
- `firmware/test/host/test_profile_catalog.cpp`
- `firmware/test/host/test_device_settings.cpp`
- `firmware/test/host/test_settings_menu.cpp`
- `firmware/test/host/test_pin_rotation.cpp`
- `tests/product/test_web_profile_catalog.py` — dependency-free Web source and generated-asset contract checks.

### Existing integration files

- `companion/Sources/ProductContracts/CompanionDTO.swift`
- `companion/Sources/CodexAppServer/JSONRPCProcess.swift`
- `companion/Sources/CodexAppServer/CodexAdapter.swift`
- `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- `companion/Sources/cardputer-companion/Configuration.swift`
- `companion/Sources/cardputer-companion/LANBridge.swift`
- `firmware/main/product/companion_protocol.hpp/.cpp`
- `firmware/main/product/ui_model.hpp/.cpp`
- `firmware/main/product/ui_navigation.hpp/.cpp`
- `firmware/main/product/display.cpp`
- `firmware/main/product/wifi_manager.hpp/.cpp`
- `firmware/main/product/product_web.hpp/.cpp`
- `firmware/main/product/product_controller.cpp`
- `firmware/main/CMakeLists.txt`
- `firmware/test/host/CMakeLists.txt`
- `web/src/index.html`
- `web/src/app.js`
- `web/src/style.css`
- `firmware/main/product/web_assets.hpp` — regenerated, never hand-edited.

---

### Task 1: Mac Codex Telemetry and Unified Snapshot

**Files:**
- Create: `companion/Sources/ProductContracts/CodexTelemetry.swift`
- Create: `companion/Sources/CodexAppServer/CodexTelemetryReader.swift`
- Modify: `companion/Sources/ProductContracts/CompanionDTO.swift`
- Modify: `companion/Sources/CodexAppServer/JSONRPCProcess.swift`
- Modify: `companion/Sources/CodexAppServer/CodexAdapter.swift`
- Modify: `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Modify: `companion/Package.swift`
- Test: `companion/Tests/ProductContractsTests/CompanionDTOTests.swift`
- Test: `companion/Tests/CodexAppServerTests/CodexAdapterTests.swift`
- Create: `companion/Tests/ProductTelemetryExecutableTests/main.swift`

**Interfaces:**
- Produces:

```swift
public enum CodexLimitScope: String, Codable, Sendable {
    case codex
    case spark
}

public enum CodexLimitWindow: String, Codable, Sendable {
    case fiveHours = "5h"
    case weekly
}

public struct CodexLimitUsage: Codable, Equatable, Sendable {
    public let scope: CodexLimitScope
    public let window: CodexLimitWindow
    public let usedPercent: UInt8
}

public struct CodexTelemetry: Equatable, Sendable {
    public let model: String
    public let thinkingLevel: String
    public let fast: Bool
    public let limits: [CodexLimitUsage]
}
```

- `CompanionSnapshot` gains optional `model`, `thinkingLevel`, `fast`, and a
  bounded `[CodexLimitUsage]` with snake-case keys.
- `CodexTelemetryReader.read(thread:now:)` reads the latest session
  `turn_context`, global service tier, and only fresh classified limits.
- `CodexRPCClient` exposes `start()`, `request(method:params:)`, and approval
  response so a fake can test `CodexAdapter`.

- [x] **Step 1: Write failing Codable and content-equality tests**

Add tests that encode:

```swift
let telemetry = CompanionSnapshot(
    sequence: 7,
    sessionID: "thread-1",
    title: "Cardputer",
    cwd: "/tmp/project",
    state: "active",
    approvals: 0,
    inputs: 0,
    model: "gpt-5.6",
    thinkingLevel: "high",
    fast: true,
    limits: [
        CodexLimitUsage(
            scope: .codex,
            window: .fiveHours,
            usedPercent: 38
        )
    ]
)
```

Assert `thinking_level`, `used_percent`, `scope`, and `window` are exact, and
that changing Fast or a limit percentage makes `hasSameContent` return false.

- [x] **Step 2: Run the ProductContracts RED test**

Run:

```bash
swift run --package-path companion product-telemetry-tests
```

Expected: compile failure because the telemetry types and initializer fields do
not exist. This executable target mirrors the XCTest assertions because the
installed Command Line Tools Swift distribution has no XCTest module.

- [x] **Step 3: Implement the telemetry DTO and snapshot fields**

Implement the exact enums and structs above. Clamp decoded limit arrays to four
entries in a custom `CompanionSnapshot.init(from:)`, and keep all new fields
optional/defaulted so existing call sites and old payloads still decode.

- [x] **Step 4: Run ProductContracts GREEN**

Run the command from Step 2.

Expected: all ProductContracts tests pass.

- [x] **Step 5: Write failing app-server classification and cache tests**

Use a fake `CodexRPCClient` with these response facts:

```swift
let rateLimitsByLimitID: [String: Any] = [
    "codex": [
        "limitId": "codex",
        "limitName": "Codex",
        "primary": ["usedPercent": 38, "windowDurationMins": 300],
        "secondary": ["usedPercent": 61, "windowDurationMins": 10080]
    ],
    "spark": [
        "limitId": "gpt-5.3-codex-spark",
        "limitName": "GPT-5.3-Codex-Spark",
        "primary": ["usedPercent": 17, "windowDurationMins": 300],
        "secondary": ["usedPercent": 22, "windowDurationMins": 10080]
    ]
]
```

Assert:

- the latest JSONL `turn_context` maps model `gpt-5.6` and effort `high`,
  while `config/read` service tier `priority` maps Fast ON;
- a valid `turn_context` remains discoverable behind a 600 KiB turn payload;
- exactly four ordered limit rows are returned;
- a 301-minute window, unnamed Spark bucket, duplicate conflict, or missing
  `usedPercent` is omitted;
- no second rate-limit request occurs at 59 seconds;
- a refresh occurs at 60 seconds;
- cached limits disappear after 120 seconds without a successful refresh.

- [x] **Step 6: Run the CodexAppServer RED test**

Run:

```bash
swift run --package-path companion product-telemetry-tests
```

Expected: compile failure because `CodexTelemetryReader` and `CodexRPCClient`
do not exist.

- [x] **Step 7: Implement the RPC protocol and telemetry reader**

Make `JSONRPCProcess` conform to `CodexRPCClient`. Implement exact identity
normalization by lowercasing and removing non-alphanumeric characters. Accept
Spark only when the normalized ID or name contains
`gpt53codexspark`; accept ordinary Codex only when identity contains `codex`
and not `spark`. Match only `300` and `10080` minute windows.

Read at most the last 8 MiB of the JSONL path and scan backward for the latest
complete `turn_context`. Keep the last successful limit observation and
timestamps in `CodexTelemetryReader`; do not perform a limit RPC inside the
60-second cadence.

- [x] **Step 8: Update CodexAdapter and the main loop**

For the selected thread, read its latest `turn_context`, call `config/read`,
and attach telemetry to the snapshot. Never call `thread/resume` for
telemetry. Change the main loop to compute one Codex snapshot per two-second
iteration after applying any remote action; remove the immediate duplicate
`adapter.snapshot()` call.

On a rate-limit error, retain only a still-fresh cache and continue publishing
base session state. Do not add a Cardputer request.

- [x] **Step 9: Run Swift GREEN and release compilation**

Run:

```bash
swift run --package-path companion product-telemetry-tests
swift build --package-path companion -c release
```

Expected: all Swift tests pass and the release executable links.

- [x] **Step 10: Perform the non-mutating telemetry gate**

Start a temporary app-server process, read the selected JSONL tail, call
`config/read` and `account/rateLimits/read`, and confirm no turn is created and
no thread content/status changes. Record only model-independent booleans and
method results; do not record session IDs or titles.

Expected: context/config/limits are available and all three mutation booleans
are false. Live result: available booleans were true; turn-created,
status-changed, and last-turn-changed were false.

- [ ] **Step 11: Commit Task 1**

```bash
git add companion/Sources companion/Tests
git commit -m "feat: add Codex telemetry snapshot"
```

---

### Task 2: Firmware Telemetry Parser and Five Read-Only Pages

**Files:**
- Modify: `firmware/main/product/companion_protocol.hpp`
- Modify: `firmware/main/product/companion_protocol.cpp`
- Modify: `firmware/main/product/ui_model.hpp`
- Modify: `firmware/main/product/ui_model.cpp`
- Modify: `firmware/main/product/ui_navigation.cpp`
- Modify: `firmware/main/product/display.cpp`
- Test: `firmware/test/host/test_companion_protocol.cpp`
- Test: `firmware/test/host/test_ui_model.cpp`
- Test: `firmware/test/host/test_ui_navigation.cpp`

**Interfaces:**
- Consumes: Task 1 snake-case optional snapshot fields.
- Produces:

```cpp
enum class CodexLimitScope : uint8_t { codex, spark };
enum class CodexLimitWindow : uint8_t { five_hours, weekly };

struct CodexLimitUsage {
  CodexLimitScope scope;
  CodexLimitWindow window;
  uint8_t used_percent;
};

void UiModel::set_codex(std::string_view model,
                        std::string_view thinking_level,
                        std::optional<bool> fast,
                        std::span<const CodexLimitUsage> limits);
```

- `UiPage` is exactly `{pet, device_status, codex_status, sync_status,
  settings}`.

- [x] **Step 1: Write the failing optional telemetry parser test**

Extend the snapshot fixture with:

```json
"model":"gpt-5.6",
"thinking_level":"high",
"fast":true,
"limits":[
  {"scope":"codex","window":"5h","used_percent":38},
  {"scope":"spark","window":"weekly","used_percent":22}
]
```

Assert exact parsing, a maximum of four rows, percentage clamping/rejection,
and that a malformed `limits` member does not reject the base snapshot.

- [x] **Step 2: Run the parser RED test**

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_companion_protocol -j
./build/product-host/test_companion_protocol
```

Expected: compile failure because telemetry fields do not exist.

- [x] **Step 3: Implement bounded optional parsing**

Retain the existing base field parser. Add bounded helpers that find the
`limits` array, inspect no more than four objects, accept only exact enum
strings, and never allocate from an untrusted claimed count. Clip Model to 32
bytes and Thinking Level to 16 bytes.

- [x] **Step 4: Run parser GREEN**

Run the command from Step 2.

Expected: `test_companion_protocol` exits zero.

- [x] **Step 5: Write failing five-page and row-order tests**

In `test_ui_model.cpp`, navigate through:

```cpp
assert(model.page() == UiPage::pet);
model.navigate(UiNavAction::next_page);
assert(model.page() == UiPage::device_status);
model.navigate(UiNavAction::next_page);
assert(model.page() == UiPage::codex_status);
model.navigate(UiNavAction::next_page);
assert(model.page() == UiPage::sync_status);
model.navigate(UiNavAction::next_page);
assert(model.page() == UiPage::settings);
model.navigate(UiNavAction::next_page);
assert(model.page() == UiPage::pet);
```

Set two limit rows and assert joined Codex content orders:

```text
SESSION:
MODEL:
FAST:
THINKING:
5H:
SPARK WEEKLY:
```

Call `set_codex` without limits and assert no line contains `5H`, `WEEKLY`,
`N/A`, or `NA`.

- [x] **Step 6: Run the UI RED tests**

```bash
cmake --build build/product-host --target test_ui_model test_ui_navigation -j
./build/product-host/test_ui_model
./build/product-host/test_ui_navigation
```

Expected: compile or assertion failure on the old four-page enum/content.

- [x] **Step 7: Implement the read-only page model**

Move former connection/session/device fields into the approved Device, Codex,
and Sync pages. Device has Version, PIN, BLE, Wi-Fi, Agent. Sync has IP,
Heartbeat, Pet Sync, and Profile. Generate Codex rows only from present fields
and limits; Fast follows Model unconditionally when a session runtime is
present.

Keep `UiPageContent` bounded at 12 rows and compute scroll maximum from actual
row count.

- [x] **Step 8: Run UI GREEN and all host tests**

```bash
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
```

Expected: every host test passes.

- [ ] **Step 9: Commit Task 2**

```bash
git add firmware/main/product/companion_protocol.* \
  firmware/main/product/ui_model.* \
  firmware/main/product/ui_navigation.cpp \
  firmware/test/host/test_companion_protocol.cpp \
  firmware/test/host/test_ui_model.cpp \
  firmware/test/host/test_ui_navigation.cpp
git commit -m "feat: add five status pages"
```

---

### Task 3: Streaming Multi-Profile Catalog and Legacy Migration

**Files:**
- Create: `firmware/main/product/profile_codec.hpp`
- Create: `firmware/main/product/profile_codec.cpp`
- Create: `firmware/main/product/profile_catalog.hpp`
- Create: `firmware/main/product/profile_catalog.cpp`
- Modify: `firmware/main/product/product_web.cpp`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/test/host/CMakeLists.txt`
- Test: `firmware/test/host/test_profile.cpp`
- Create: `firmware/test/host/test_profile_catalog.cpp`

**Interfaces:**
- Produces:

```cpp
inline constexpr std::size_t kProfileCatalogBankBytes = 0x10000;
inline constexpr std::size_t kProfileCatalogPayloadMaximum = 60 * 1024;
inline constexpr std::size_t kProfileCatalogMaximumCustomProfiles = 4;

struct ProfileSummary {
  std::array<char, 9> id;
  std::array<char, 21> name;
  uint32_t revision;
  bool builtin;
};

class ProfileCatalogBackend {
 public:
  virtual bool read(std::size_t offset, std::span<uint8_t> output) = 0;
  virtual bool erase(std::size_t offset, std::size_t length) = 0;
  virtual bool write(std::size_t offset, std::span<const uint8_t> input) = 0;
};

class ProfileCatalogStore {
 public:
  ProfileCatalogLoadResult load(std::optional<std::string_view> legacy_json);
  ProfileCatalogResult list(std::span<ProfileSummary> output,
                            std::size_t* count) const;
  ProfileCatalogResult read(std::string_view id, Profile& output) const;
  ProfileCatalogResult create(std::optional<std::string_view> clone_id,
                              std::string_view name,
                              std::array<char, 9>* created_id);
  ProfileCatalogResult publish(std::string_view id, const Profile& profile,
                               uint32_t expected_revision);
  ProfileCatalogResult remove(std::string_view id);
};
```

- `profile_codec` owns the existing sparse JSON encode/decode and enforces the
  16 KiB per-Profile request boundary.
- The catalog keeps only a fixed summary/index in RAM and streams unchanged
  JSON from the active bank to the inactive bank in at most 4 KiB chunks.

- [x] **Step 1: Extract codec tests before moving implementation**

Move sparse round-trip expectations into `test_profile.cpp`:

- 224 passthrough bindings encode as `null`;
- all action kinds round-trip;
- malformed count, oversized text, invalid sequence, and encoded data over
  16 KiB fail;
- a valid encoded Profile preserves name and revision.

- [x] **Step 2: Run codec RED**

```bash
cmake --build build/product-host --target test_profile -j
./build/product-host/test_profile
```

Expected: compile failure because `profile_codec.hpp` does not exist.

- [x] **Step 3: Extract the codec**

Move `action_json`, `profile_json`, `parse_leaf`, and `parse_profile` from
`product_web.cpp` into `profile_codec.cpp` without changing the wire shape.
Expose:

```cpp
ProfileCodecResult encode_profile(const Profile&, std::string& output);
ProfileCodecResult decode_profile(std::string_view, Profile& output);
```

- [x] **Step 4: Run codec GREEN and product Web regression**

```bash
cmake --build build/product-host --target test_profile test_product_web -j
./build/product-host/test_profile
./build/product-host/test_product_web
```

Expected: both executables exit zero.

- [x] **Step 5: Write the failing catalog bank tests**

Implement a memory backend covering:

- empty banks plus valid legacy JSON create bank A and rename legacy `SAFE` to
  `IMPORTED`;
- higher valid sequence wins;
- bad magic, count above four, offset overflow, payload above 60 KiB, invalid
  Profile JSON, and CRC mismatch reject a bank;
- interrupted inactive-bank write leaves the prior bank selected;
- publish copies unchanged entries and increments sequence;
- create fifth custom Profile returns `capacity`;
- delete and activate never delete SAFE;
- no backend read/write call exceeds 4096 bytes.

- [x] **Step 6: Run catalog RED**

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_profile_catalog -j
```

Expected: CMake target or compile failure because the catalog does not exist.

- [x] **Step 7: Implement and verify the raw two-bank catalog**

Use bank bases `0x1c0000` and `0x1d0000` relative to the `storage` partition.
Use a fixed `CCPF` header and fixed four-entry table. Compute CRC32 while
streaming with the stored CRC field treated as zero. Erase and write the
inactive 64 KiB bank, read it back through the same validator, and only then
return the new sequence.

For ESP builds, add a `ProfileCatalogBackend` that calls
`esp_partition_read`, `esp_partition_erase_range`, and
`esp_partition_write`. Verify the partition is at least `0x1e0000` bytes.

- [x] **Step 8: Run catalog GREEN, sanitizer, and partition checks**

```bash
cmake --build build/product-host --target test_profile_catalog -j
./build/product-host/test_profile_catalog
cmake -S firmware/test/host -B build/product-host-sanitize \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/product-host-sanitize --target test_profile_catalog -j
ctest --test-dir build/product-host-sanitize -R profile_catalog \
  --output-on-failure
python3 tools/product/verify_partition_layout.py
```

Expected: all tests pass and the existing partition table remains unchanged.

- [ ] **Step 9: Commit Task 3**

```bash
git add firmware/main/product/profile_codec.* \
  firmware/main/product/profile_catalog.* \
  firmware/main/product/product_web.cpp \
  firmware/main/CMakeLists.txt \
  firmware/test/host
git commit -m "feat: add transactional profile catalog"
```

---

### Task 4: Authenticated Multi-Profile Web API and UI

**Files:**
- Modify: `firmware/main/product/product_web.hpp`
- Modify: `firmware/main/product/product_web.cpp`
- Modify: `firmware/test/host/test_product_web.cpp`
- Modify: `web/src/index.html`
- Modify: `web/src/app.js`
- Modify: `web/src/style.css`
- Create: `tests/product/test_web_profile_catalog.py`
- Regenerate: `firmware/main/product/web_assets.hpp`

**Interfaces:**
- Consumes: Task 3 `ProfileCatalogStore` and codec.
- Produces:

```text
GET    /api/v1/profiles
POST   /api/v1/profiles
GET    /api/v1/profile?id=<8-hex-id>
PUT    /api/v1/profile?id=<8-hex-id>
DELETE /api/v1/profile?id=<8-hex-id>
POST   /api/v1/profile/activate
```

- Existing `GET/PUT /api/v1/profile` without `id` remain active-Profile
  aliases.
- Web state holds `profileCatalog`, `activeProfileID`,
  `selectedProfileID`, and one loaded `profile`.

- [x] **Step 1: Write failing route-manifest tests**

Extend `test_product_web.cpp` to require `ProductHttpMethod::delete_`, sixteen
routes, PIN protection on every catalog route, and exact compatibility aliases.

- [x] **Step 2: Run route RED**

```bash
cmake --build build/product-host --target test_product_web -j
./build/product-host/test_product_web
```

Expected: compile or assertion failure because the manifest has twelve routes
and no DELETE method.

- [x] **Step 3: Register the catalog routes and response contracts**

Use these bounded response shapes:

```json
{
  "active_id": "a1b2c3d4",
  "profiles": [
    {"id":"SAFE","name":"SAFE","revision":1,"builtin":true},
    {"id":"a1b2c3d4","name":"CODING","revision":7,"builtin":false}
  ]
}
```

Create accepts `{"name":"CODING","clone_id":"SAFE"}`. Activate accepts
`{"id":"a1b2c3d4"}`. Delete rejects SAFE and the active ID with HTTP 409.
Capacity returns HTTP 409 `profile_capacity`; catalog I/O failure returns HTTP
500 `profile_catalog_failed`; revision mismatch remains HTTP 409.

All handlers hold the existing Profile mutex only around catalog/index access,
never while sending the HTTP response.

- [x] **Step 4: Run route GREEN**

Run the command from Step 2.

Expected: `test_product_web` exits zero.

- [x] **Step 5: Write failing Web source-contract tests**

Create pytest assertions for:

- a Profile selector and buttons for create, clone, rename, delete, activate;
- calls to `/api/v1/profiles` and `/api/v1/profile/activate`;
- selected Profile ID appended with `URLSearchParams`, not raw string
  concatenation;
- delete disabled for SAFE and active Profile;
- key editor publishes only the selected Profile;
- success/failure uses the existing in-page result modal;
- Web login and Wi-Fi password inputs remain masked.

- [x] **Step 6: Run Web RED**

```bash
PYTHONPATH=. uv run pytest -q tests/product/test_web_profile_catalog.py
```

Expected: failures for the missing controls and API calls.

- [x] **Step 7: Implement the Web catalog controls**

Load the catalog after authentication, select the active Profile by default,
and fetch only the selected full Profile. Keep all labels and error messages in
Chinese. Require confirmation before deletion. If active Profile deletion is
requested, show a failure dialog instructing the user to activate SAFE first.

On create/clone/activate/delete, refresh the catalog and full Profile only after
the device confirms success. Preserve the previous visible state on failure.

- [x] **Step 8: Regenerate and verify the embedded asset**

```bash
python3 scripts/build_web_assets.py
PYTHONPATH=. uv run pytest -q tests/product/test_web_profile_catalog.py
python3 scripts/build_web_assets.py --check
```

Expected: pytest passes and the generated header exactly matches Web source.

- [x] **Step 9: Run a local static browser smoke**

Serve `web/src` locally, open the page in Chrome, and verify the login screen,
Profile toolbar, key modal, result modal, Settings tab, and responsive
seven-column layout. Use mocked fetch responses containing SAFE and three
custom Profiles; do not use the real device PIN during this static smoke.

- [ ] **Step 10: Commit Task 4**

```bash
git add firmware/main/product/product_web.* \
  firmware/test/host/test_product_web.cpp \
  web/src firmware/main/product/web_assets.hpp \
  tests/product/test_web_profile_catalog.py
git commit -m "feat: add Web profile management"
```

---

### Task 5: Settings Menu, Local Text Editor, and Display Settings

**Files:**
- Create: `firmware/main/product/device_settings.hpp`
- Create: `firmware/main/product/device_settings.cpp`
- Create: `firmware/main/product/settings_menu.hpp`
- Create: `firmware/main/product/settings_menu.cpp`
- Modify: `firmware/main/product/ui_model.hpp`
- Modify: `firmware/main/product/ui_model.cpp`
- Modify: `firmware/main/product/ui_navigation.hpp`
- Modify: `firmware/main/product/ui_navigation.cpp`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/test/host/CMakeLists.txt`
- Create: `firmware/test/host/test_device_settings.cpp`
- Create: `firmware/test/host/test_settings_menu.cpp`
- Modify: `firmware/test/host/test_ui_model.cpp`
- Modify: `firmware/test/host/test_ui_navigation.cpp`

**Interfaces:**
- Produces:

```cpp
enum class Brightness : uint8_t { percent_25, percent_50,
                                  percent_75, percent_100 };
enum class ReturnToPet : uint8_t { disabled, seconds_15,
                                   seconds_30, seconds_60 };
enum class PetFrameRate : uint8_t { fps_2, fps_2_5, fps_3 };

struct DeviceSettings {
  uint8_t schema_version = 1;
  Brightness brightness = Brightness::percent_75;
  ReturnToPet return_to_pet = ReturnToPet::seconds_30;
  PetFrameRate pet_frame_rate = PetFrameRate::fps_2_5;
};

enum class SettingsCommandKind : uint8_t {
  none, release_hid, scan_wifi, rotate_pin, stage_wifi,
  activate_profile, apply_display_settings, return_to_pet
};

struct SettingsInputResult {
  bool captured;
  SettingsCommandKind command;
};
```

- `SettingsMenu::on_key(physical_key, pressed, shift, now_ms)` owns menu
  selection, bounded PIN/password/hidden-SSID buffers, confirm state, and
  command emission.
- `UiNavigation` accepts current page/interaction context so bare punctuation
  is captured only by Settings browse states.

- [x] **Step 1: Write failing display-settings validation tests**

Test:

- defaults are 75%, 30 seconds, and 2.5 FPS;
- enum to runtime values maps to brightness `191`, timeout `30000`, and frame
  interval `400`;
- schema mismatch, invalid enum, or CRC mismatch returns defaults;
- encode/decode round-trips a valid record;
- a failed backend commit leaves the previous runtime settings selected.

- [x] **Step 2: Run display-settings RED**

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_device_settings -j
```

Expected: target or compile failure because the module does not exist.

- [x] **Step 3: Implement the versioned settings record**

Use a fixed POD wire record with explicit fields and CRC32 rather than
persisting compiler padding. Keep NVS behind a `DeviceSettingsBackend`
interface so host tests do not link ESP-IDF.

- [x] **Step 4: Run display-settings GREEN**

```bash
cmake --build build/product-host --target test_device_settings -j
./build/product-host/test_device_settings
```

Expected: executable exits zero.

- [x] **Step 5: Write failing Settings interaction tests**

Cover this exact sequence:

```cpp
SettingsMenu menu;
menu.enter();
assert(menu.on_key(39, true, false, 100).captured);  // ; = up
assert(menu.on_key(53, true, false, 110).captured);  // . = down
assert(menu.on_key(54, true, false, 120).captured);  // / = enter
```

Also assert:

- entering Settings emits `release_hid`;
- a captured press captures its release;
- PIN editor accepts digits only and requires two identical eight-digit
  entries;
- password editor converts US HID usages with Shift, including comma, period,
  semicolon, and slash, instead of navigating;
- Backspace edits, Enter confirms, Esc cancels;
- buffers stop at 8 PIN digits, 32 SSID bytes, and 64 password bytes;
- applying ignores repeated input;
- return timeout is suspended in edit/confirm/apply/result states;
- leaving Settings restores ordinary bare punctuation passthrough.

- [x] **Step 6: Run Settings RED**

```bash
cmake --build build/product-host --target test_settings_menu \
  test_ui_navigation -j
```

Expected: target or compile failure because Settings interaction context does
not exist.

- [x] **Step 7: Implement the pure Settings state machine**

Use the existing physical key map to derive HID usage. Implement a bounded
US-layout usage-to-ASCII decoder for letters, numbers, and printable Cardputer
punctuation. Do not call ESP, BLE, NVS, display, or Web functions from
`settings_menu.cpp`; emit commands for the runtime to execute.

- [x] **Step 8: Add scrollable Settings content to UiModel**

Expose top-level and submenu rows through `UiPageContent`. Keep five visible
rows and ensure selected index remains inside the viewport. Use masked PIN and
password editor values; never expose password through a model getter.

- [x] **Step 9: Run Settings and full host GREEN**

```bash
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
```

Expected: all host tests pass.

- [ ] **Step 10: Commit Task 5**

```bash
git add firmware/main/product/device_settings.* \
  firmware/main/product/settings_menu.* \
  firmware/main/product/ui_model.* \
  firmware/main/product/ui_navigation.* \
  firmware/main/CMakeLists.txt firmware/test/host
git commit -m "feat: add on-device settings menu"
```

---

### Task 6: Transactional Wi-Fi Scan, Candidate Connection, and Rollback

**Files:**
- Modify: `firmware/main/product/wifi_manager.hpp`
- Modify: `firmware/main/product/wifi_manager.cpp`
- Modify: `firmware/test/host/test_wifi_manager.cpp`
- Modify: `firmware/main/product/product_controller.cpp`

**Interfaces:**
- Consumes: Task 5 `scan_wifi` and `stage_wifi` commands.
- Produces:

```cpp
struct WifiScanEntry {
  std::array<char, 33> ssid;
  int8_t rssi;
  bool secured;
};

enum class WifiApplyResult : uint8_t {
  connected_and_persisted,
  candidate_failed_rolled_back,
  rollback_failed,
  storage_failed
};

WifiCommand WifiStateMachine::stage(WifiCredentials candidate,
                                    uint64_t now_ms);
WifiCommand WifiStateMachine::on_connected();
WifiCommand WifiStateMachine::on_candidate_timeout(uint64_t now_ms);
```

- ESP adapter exposes asynchronous scan and staged connection callbacks; no
  callback executes on the 10 ms keyboard scanner task.

- [x] **Step 1: Replace the current precedence test with the approved behavior**

Assert:

- runtime credentials with an override marker beat private credentials;
- without the marker, private credentials remain the bootstrap source;
- staging keeps a copy of prior credentials;
- candidate `GOT_IP` returns `persist_candidate`;
- timeout returns `reconnect_previous`;
- rollback success restores online state;
- candidate failure never mutates the credential source.

- [x] **Step 2: Run Wi-Fi RED**

```bash
cmake --build build/product-host --target test_wifi_manager -j
./build/product-host/test_wifi_manager
```

Expected: assertions fail because current code chooses private first and writes
candidate credentials before connection.

- [x] **Step 3: Implement the pure staging state machine**

Add explicit `candidate_connecting` and `rollback_connecting` states. Preserve
the 15-second timeout. Make persistence a command emitted only after
`on_connected()` for the candidate.

- [x] **Step 4: Run pure Wi-Fi GREEN**

Run the command from Step 2.

Expected: executable exits zero.

- [x] **Step 5: Implement asynchronous scan**

Use `esp_wifi_scan_start(..., false)` and consume
`WIFI_EVENT_SCAN_DONE`. Read at most the platform result count, deduplicate by
the exact SSID bytes, retain the strongest RSSI, sort descending, and publish
at most 12 entries to the Settings backend. Append Hidden Network in the UI,
not the scan result array.

- [x] **Step 6: Delay persistence until candidate success**

Remove the immediate NVS write from `product_wifi_save`. Add a staging entry
point that disconnects and attempts the candidate in RAM. On candidate
`GOT_IP`, write SSID, password, and `override=1` in one NVS commit. If commit
fails or connection times out, restore the prior configuration and reconnect.

Never overwrite the `wifi_cfg` private partition.

- [x] **Step 7: Run host tests and ESP target compile**

```bash
cmake --build build/product-host -j
ctest --test-dir build/product-host -R wifi_manager --output-on-failure
(
  cd firmware
  ../scripts/phase0/idf.sh build
)
```

Expected: host test and ESP-IDF build pass.

- [ ] **Step 8: Commit Task 6**

```bash
git add firmware/main/product/wifi_manager.* \
  firmware/main/product/product_controller.cpp \
  firmware/test/host/test_wifi_manager.cpp
git commit -m "feat: add transactional Wi-Fi binding"
```

### Task 7: Rotate the PIN safely and migrate the Mac Agent credential

**Files:**

- Create: `firmware/main/product/pin_rotation.hpp`
- Create: `firmware/main/product/pin_rotation.cpp`
- Create: `firmware/test/host/test_pin_rotation.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/main/product/product_web.hpp`
- Modify: `firmware/main/product/product_web.cpp`
- Modify: `companion/Sources/ProductContracts/CompanionDTO.swift`
- Create: `companion/Sources/ProductContracts/PairingMigration.swift`
- Modify: `companion/Sources/cardputer-companion/Configuration.swift`
- Modify: `companion/Sources/cardputer-companion/LANBridge.swift`
- Modify: `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Modify: `companion/Tests/ProductContractsTests/CompanionDTOTests.swift`
- Create: `companion/Tests/ProductContractsTests/PairingMigrationTests.swift`

Implement these boundaries:

```cpp
enum class PinAuthorization {
  denied,
  current,
  previous_companion_action,
};

class PinRotationState {
 public:
  void restore(uint32_t revision);
  bool rotate(std::string_view old_pin,
              std::string_view new_pin,
              uint64_t now_ms);
  PinAuthorization authorize(std::string_view candidate,
                             bool is_companion_action,
                             uint64_t now_ms);
  uint32_t revision() const;
  std::optional<std::string_view> next_pairing() const;
};
```

```swift
public struct PairingMigration: Equatable, Sendable {
    public let nextPairing: String
    public let pinRevision: UInt32
}

public enum PairingConfigWriter {
    public static func persist(
        _ migration: PairingMigration,
        to configURL: URL
    ) throws
}
```

The previous PIN is held in RAM for at most 300 seconds and is accepted only
for `GET /api/v1/companion/action`. The response may include `next_pairing`
and `pin_revision`; no other route accepts the previous PIN. A successful
request authenticated with the new PIN revokes the previous PIN immediately.
Neither PIN is logged.

- [ ] **Step 1: Write firmware RED tests for the grace window**

Cover:

- only an exactly eight-digit new PIN may be rotated;
- the current PIN remains valid for every authenticated route;
- the previous PIN is denied for all routes except Companion action;
- Companion action accepts the previous PIN through `now + 299999 ms`;
- it rejects the previous PIN at `now + 300000 ms`;
- successful current-PIN authorization revokes the previous PIN early;
- the revision increments once and survives `restore()`;
- `next_pairing()` exists only while migration is pending.

- [ ] **Step 2: Run firmware PIN RED**

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_pin_rotation -j
./build/product-host/test_pin_rotation
```

Expected: the target or assertions fail because `PinRotationState` does not
exist.

- [ ] **Step 3: Implement PIN rotation and Web confirmation**

Use constant-time equality at the HTTP authorization boundary. In the
Settings backend, require two matching eight-digit entries before calling
`rotate()`. Commit the new PIN and monotonic revision to NVS in one
transaction, then arm the RAM-only grace credential.

In the Web Settings PIN form, require `新 PIN` and `确认新 PIN`; retain the
existing authenticated page/modal behavior and show the existing in-page
success or failure notice. Never return either PIN from a general settings or
status endpoint.

- [ ] **Step 4: Run firmware PIN GREEN**

Run the command from Step 2.

Expected: executable exits zero.

- [ ] **Step 5: Write Swift RED tests**

Add fixtures for an action envelope with and without:

```json
{
  "next_pairing": "87654321",
  "pin_revision": 8
}
```

Test that `PairingConfigWriter`:

- ignores a revision no newer than the locally recorded revision;
- rewrites the existing JSON without losing unrelated keys;
- uses a sibling temporary file and atomic replacement;
- leaves the resulting file at mode `0600`;
- rejects a pairing value that is not exactly eight ASCII digits.

- [ ] **Step 6: Run Swift RED**

```bash
cd companion
swift test --filter ProductContractsTests
```

Expected: compile or assertions fail because the migration DTO and writer do
not exist.

- [ ] **Step 7: Implement Agent migration**

Decode the optional migration fields in `RemoteActionEnvelope`. Preserve the
source config URL and local `pin_revision` in `Configuration`. Make
`LANBridge` pairing state lock-protected and add:

```swift
func updatePairing(_ pairing: String, revision: UInt32)
```

After a successful Companion action response, persist a newer migration
atomically before updating the live bridge. If persistence fails, retain the
old pairing and retry on the next poll. Once the new pairing succeeds, normal
firmware authorization revokes the old grace credential.

Command-line-only pairing configuration has no durable target: update it in
memory for the process lifetime and emit a PIN-free warning. A device reboot
during this non-durable migration requires manual config repair, as documented
in the approved boundary.

- [ ] **Step 8: Run Swift GREEN and the combined auth checks**

```bash
cd companion
swift test --filter ProductContractsTests
swift test --filter CodexAppServerTests
cd ..
cmake --build build/product-host -j
ctest --test-dir build/product-host -R 'pin_rotation|product_web|web_guard' \
  --output-on-failure
```

Expected: all selected tests pass.

- [ ] **Step 9: Commit Task 7**

```bash
git add firmware/main/product/pin_rotation.* \
  firmware/main/product/product_web.* \
  firmware/test/host/test_pin_rotation.cpp \
  firmware/test/host/CMakeLists.txt \
  companion/Sources/ProductContracts/CompanionDTO.swift \
  companion/Sources/ProductContracts/PairingMigration.swift \
  companion/Sources/cardputer-companion/Configuration.swift \
  companion/Sources/cardputer-companion/LANBridge.swift \
  companion/Sources/cardputer-companion/CardputerCompanionMain.swift \
  companion/Tests/ProductContractsTests
git commit -m "feat: migrate rotated PIN to Mac Agent"
```

### Task 8: Integrate Settings, profiles, and display behavior at runtime

**Files:**

- Modify: `firmware/main/product/product_controller.hpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/main/product/input_router.hpp`
- Modify: `firmware/main/product/input_router.cpp`
- Modify: `firmware/main/product/ui_navigation.hpp`
- Modify: `firmware/main/product/ui_navigation.cpp`
- Modify: `firmware/main/product/ui_model.hpp`
- Modify: `firmware/main/product/ui_model.cpp`
- Modify: `firmware/main/product/display.hpp`
- Modify: `firmware/main/product/display.cpp`
- Modify: `firmware/main/product/profile.hpp`
- Modify: `firmware/main/product/profile.cpp`
- Modify: `firmware/main/product/product_types.hpp`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/test/host/test_product_controller.cpp`
- Modify: `firmware/test/host/test_input_router.cpp`
- Modify: `firmware/test/host/test_ui_navigation.cpp`
- Modify: `firmware/test/host/test_ui_model.cpp`
- Modify: `firmware/test/host/test_profile.cpp`

The integration ordering is fixed:

1. BLE pairing input, when the stack requests it;
2. Settings text editor, when active;
3. Settings browse navigation;
4. global Fn navigation;
5. active-profile macro or HID handling.

Every transition into or out of Settings browse/editor state sends HID
release-all before the new routing policy becomes active.

- [ ] **Step 1: Write controller and routing RED tests**

Assert:

- startup loads Profile Catalog before Web and activates the NVS Profile ID;
- a missing/corrupt active ID selects immutable SAFE;
- activating a profile changes subsequent macro lookup without reboot;
- `DeviceAction::next_profile` and `previous_profile` wrap only through
  available profiles, including SAFE;
- bare `; . , /` are consumed only in Settings browse state;
- punctuation reaches the editor as literal text in Settings edit state;
- the same keys remain ordinary HID keys outside Settings;
- entering/leaving the editor emits release-all;
- Settings selection/scroll position is retained while entering a child menu.

- [ ] **Step 2: Run integration RED**

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host \
  --target test_product_controller test_input_router test_ui_navigation \
           test_ui_model test_profile -j
ctest --test-dir build/product-host \
  -R 'product_controller|input_router|ui_navigation|ui_model|profile' \
  --output-on-failure
```

Expected: new assertions fail because runtime components are not connected.

- [ ] **Step 3: Integrate Profile Catalog and actions**

Initialize the catalog after storage and before Web route registration. Resolve
the active ID once, publish it in the unified status view, and have every
macro lookup read the active in-memory profile. Activation through Web,
Settings, or `next_profile`/`previous_profile` uses the same controller method
and persists only the Profile ID.

- [ ] **Step 4: Integrate the Settings state machine**

Queue blocking settings work such as Wi-Fi scan/connection on the existing
product service task; never execute it on the 10 ms keyboard scan path.
Implement these rows and child flows:

1. Keyboard Profile
2. Change PIN
3. Bind Wi-Fi
4. Brightness: 25 / 50 / 75 / 100 percent
5. Return to Pet: Off / 15 / 30 / 60 seconds
6. Pet FPS: 2 / 2.5 / 3

Persist the last three settings as one versioned, CRC-protected NVS object.
Apply brightness immediately. Apply pet interval as 500, 400, or 333 ms.
Auto-return is based on the last local UI input and is suspended while an
editor, PIN flow, Wi-Fi scan, or candidate connection is active.

- [ ] **Step 5: Render the five-page display without pet-frame blanking**

Keep the page order:

```text
Pets -> Device -> Codex -> Sync -> Settings
```

Render:

- a stable header with page title and connection indicators;
- page-specific body rows using the larger approved font;
- five footer dots with the current page highlighted;
- scroll hints only when content exists outside the viewport;
- the pet frame directly over the prior pet frame, with no intermediate
  background fill.

Only redraw dirty regions. The Pets canvas color comes from the bundle
background metadata, so no mismatched rectangle appears between frames.

- [ ] **Step 6: Run host GREEN and ESP compile**

```bash
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
(
  cd firmware
  ../scripts/phase0/idf.sh build
)
```

Expected: every host test passes and the ESP-IDF target links.

- [ ] **Step 7: Commit Task 8**

```bash
git add firmware/main/CMakeLists.txt \
  firmware/main/product/product_controller.* \
  firmware/main/product/input_router.* \
  firmware/main/product/ui_navigation.* \
  firmware/main/product/ui_model.* \
  firmware/main/product/display.* \
  firmware/main/product/profile.* \
  firmware/main/product/product_types.hpp \
  firmware/test/host/test_product_controller.cpp \
  firmware/test/host/test_input_router.cpp \
  firmware/test/host/test_ui_navigation.cpp \
  firmware/test/host/test_ui_model.cpp \
  firmware/test/host/test_profile.cpp
git commit -m "feat: integrate status and settings pages"
```

### Task 9: Release 1.0.30, deploy to the attached Cardputer, and prove it

**Files:**

- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify: `firmware/test/host/test_product_types.cpp`
- Modify: `README.md`
- Modify: `docs/USER_GUIDE.md`
- Modify: `docs/IMPLEMENTATION_STATUS.md`
- Modify: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`
- Modify: `/Users/nicholasliao/clawd/memory/2026-07-25.md`

- [ ] **Step 1: Write and run the version RED test**

Change the expected public version in `test_product_types.cpp` to `1.0.30`.

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_product_types -j
./build/product-host/test_product_types
```

Expected: assertion fails while firmware still reports `1.0.29`.

- [ ] **Step 2: Set version 1.0.30 and run the narrow release tests**

Set the ESP project version and displayed/API version to the same constant.

```bash
cmake --build build/product-host \
  --target test_product_types test_companion_protocol test_product_controller \
           test_product_web test_wifi_manager test_pin_rotation -j
ctest --test-dir build/product-host \
  -R 'product_types|companion_protocol|product_controller|product_web|wifi_manager|pin_rotation' \
  --output-on-failure
(
  cd companion
  swift test
)
```

Expected: selected host tests and the full Swift suite pass.

- [ ] **Step 3: Run the repository release gate**

```bash
./scripts/verify_product_release.sh
```

Expected: Python, host, ASAN/UBSAN, embedded Web asset, ESP-IDF 5.5.4,
partition/memory, Swift release/doctor, package, app build, secret scan, and
hash stages all pass.

- [ ] **Step 4: Record artifacts and checksums**

Confirm both generic and private full images identify release `1.0.30`. Write
SHA-256 entries for the app-only image, generic full image, private full image,
and Mac Agent bundle to the ignored runtime file
`dist/1.0.30-SHA256SUMS`, then record the same artifact names and hashes in the
progress document as committed release evidence. Do not include private
configuration values in documentation or command output.

- [ ] **Step 5: Verify the serial target and perform an app-only flash**

Enumerate `/dev/cu.usbmodem*`, verify the expected ESP32-S3 USB VID/PID and
read the chip identity before writing. Abort if more than one candidate remains
ambiguous.

Use the release-produced app image and preserve NVS, Profile Catalog, pet
slots, private Wi-Fi, and otadata:

```bash
.tools/espressif/python_env/idf5.5_py3.14_env/bin/python \
  -m esptool --chip esp32s3 --port /dev/cu.usbmodem21201 \
  --before default_reset --after hard_reset \
  write_flash 0x20000 firmware/build/cardputer_codex_companion.bin
.tools/espressif/python_env/idf5.5_py3.14_env/bin/python \
  -m esptool --chip esp32s3 --port /dev/cu.usbmodem21201 \
  --before default_reset --after hard_reset \
  verify_flash 0x20000 firmware/build/cardputer_codex_companion.bin
```

At execution time, substitute the verified unique port if macOS assigned a
different device node. Do not use the full image for this upgrade.

- [ ] **Step 6: Install and verify the rebuilt Mac Agent**

Use the repository's existing Agent installer/build path, then verify:

```bash
launchctl print "gui/$(id -u)/com.lynx.cardputer-companion"
```

Expected: the job is running, the Agent heartbeat advances on Device and Sync
pages, and no PIN or Wi-Fi credential appears in stdout/stderr.

- [ ] **Step 7: Run the physical acceptance matrix**

Record pass/fail evidence in the progress document for:

- boot displays `1.0.30` and preserves the selected pet, active profile, PIN,
  Wi-Fi override, brightness, return timeout, and FPS;
- Fn+`,` and Fn+`/` traverse all five pages in a loop;
- Fn+`;` and Fn+`.` scroll long read-only pages;
- Device shows Version, masked PIN, BLE, Wi-Fi, and Agent;
- Codex places Fast immediately under Model;
- absent base or Spark 5H/Weekly buckets produce no row;
- a real returned bucket shows correct used percentage/window;
- Sync shows IP, advancing heartbeat, latest Pet Sync, and active profile;
- Settings browse consumes bare punctuation, while outside Settings those keys
  still type through BLE HID;
- PIN double-entry rejects mismatch and successful rotation migrates the
  running Agent within five minutes;
- Wi-Fi scan shows at most 12 unique strongest SSIDs plus Hidden Network;
- a deliberately invalid candidate rolls back to the prior Wi-Fi and prior IP
  becomes reachable again;
- create three temporary profiles, switch by Web and device, reboot, and prove
  persistence; delete only those test-created profiles afterward;
- pet animation holds 2, 2.5, and 3 FPS without an empty frame or mismatched
  background.

- [ ] **Step 8: Soak for 30 minutes**

Poll the authenticated status and observe the display every 30 seconds. Pass
only if there is:

- no reboot or watchdog reset;
- no display flashing;
- no BLE disconnect loop;
- no Agent heartbeat gap longer than 45 seconds;
- no heap trend exceeding 5 percent from the post-boot baseline;
- no profile/PIN/Wi-Fi state regression.

- [ ] **Step 9: Update docs, progress, and operational memory**

Document the navigation grammar, page fields, hidden-limit rule, settings
values, Profile Catalog capacity/migration, PIN grace boundary, Wi-Fi rollback,
and app-only upgrade path. Add a timestamped milestone to the project progress
document with release-gate and HIL evidence.

Before the user-facing closeout, append a secret-free operation summary, final
status, root cause/lesson, and deployed artifact paths to
`/Users/nicholasliao/clawd/memory/2026-07-25.md`.

- [ ] **Step 10: Commit release evidence**

```bash
git add firmware/CMakeLists.txt \
  firmware/main/product/product_types.hpp \
  firmware/test/host/test_product_types.cpp \
  README.md docs/USER_GUIDE.md docs/IMPLEMENTATION_STATUS.md \
  docs/2026-07-24-cardputer-codex-companion_PROGRESS.md
git commit -m "chore: release Cardputer Companion 1.0.30"
```

If a remote is configured by execution time, push the completed branch and
verify the remote commit. If no remote exists, report the exact local commit
and do not invent a push destination.

## Final acceptance checklist

- [ ] Pets is the home page and all five pages use the approved order.
- [ ] Device, Codex, and Sync fields come from one authenticated snapshot.
- [ ] Fast is directly below Model.
- [ ] Unavailable base or Spark rate-limit rows are absent, never `N/A`.
- [ ] Settings is scrollable and implements the approved hybrid key grammar.
- [ ] Four custom profiles plus immutable SAFE fit in the existing partition.
- [ ] Wi-Fi credentials persist only after `GOT_IP`, with rollback on failure.
- [ ] PIN is exactly eight digits, double-entered, durable, and migrates the
  Agent through the bounded old-PIN action route.
- [ ] User data survives the app-only upgrade and one reboot.
- [ ] Release gate, physical matrix, and 30-minute soak all pass.
