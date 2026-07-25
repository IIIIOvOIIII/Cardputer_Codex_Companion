# Cardputer Status and Settings Navigation Design

## Goal

Extend Cardputer Codex Companion `1.0.29` into a five-page secondary display
with richer Codex telemetry and a bounded on-device settings hierarchy. The
release target is firmware version `1.0.30`.

The pet page remains the home page. The four pages to its right are:

1. Device Status;
2. Codex Status;
3. Sync Status;
4. Settings.

The implementation must preserve independent BLE keyboard operation, LAN-only
control, the current pet cache, and the existing Unicode GATT path.

## Confirmed Product Decisions

- The page order is fixed as `Pets -> Device -> Codex -> Sync -> Settings`.
- `Fn+,` and `Fn+/` change pages outside the Settings menu.
- `Fn+;` and `Fn+.` scroll read-only status pages.
- Inside Settings menus, bare `;`, `.`, `,`, and `/` mean Up, Down, Back, and
  Enter.
- Inside a text editor, punctuation returns to normal text input; `Enter`
  confirms, `Esc` cancels, and `Backspace` deletes.
- The device supports SAFE plus at most four named custom keyboard Profiles.
- Wi-Fi binding uses an SSID scan plus masked password entry, with a manual
  hidden-network option.
- Device-local optional settings are brightness, return-to-pet timeout, and
  pet animation rate.
- Fast appears immediately below Model on the Codex page.
- A missing, unrecognized, or stale rate-limit window is omitted completely.
  The UI never substitutes `N/A`, zero, or a guessed value.
- Codex model, thinking level, Fast, and rate limits are read-only. They are
  not changed from the Cardputer.

## Existing Baseline and Constraints

- Display: 240 by 135 pixels, landscape.
- Current release: `1.0.29`.
- UI task cadence: 200 ms.
- Pet animation default: 400 ms per frame, or 2.5 FPS.
- The storage partition starts at `0x620000`, is `0x1e0000` bytes, and is
  accessed as a raw partition despite its historical `spiffs` subtype.
- Pet slot A occupies storage offsets `0x000000` through `0x0dffff`.
- Pet slot B occupies storage offsets `0x0e0000` through `0x1bffff`.
- Storage offsets `0x1c0000` through `0x1dffff` are currently unused.
- The default NVS partition is `0x6000` bytes and also carries BLE and product
  state; it is not large enough to become a general multi-Profile database.
- The current product Web request limit is 16 KiB.
- The current Mac Companion owns one serialized HTTPS request stream to the
  Cardputer. This must remain serialized to avoid ESP32 HTTPS contention.
- The current app-server exposes read-only `config/read` and
  `account/rateLimits/read`; `thread/list` supplies the local session JSONL
  path, whose latest `turn_context` records the effective model and effort.

## Considered Architectures

### Extend the unified Companion snapshot -- selected

The Mac Companion reads session settings and account limits, maintains
low-frequency cached limit data, and adds the current values to the existing
authenticated snapshot. The device receives one cohesive state object through
the existing serialized network loop.

This keeps the existing heartbeat semantics, avoids adding a second device
poller, and lets old firmware ignore new JSON fields.

### Split session and rate-limit publications -- rejected

Separate routes and cadences would make the DTOs smaller, but add more TLS
requests and more stale clocks. That directly conflicts with the recently
confirmed HTTPS contention boundary.

### Let the Cardputer query Mac or Codex state directly -- rejected

This would require a new Mac listener, a new trust boundary, or access to local
Codex files. It would still not provide a reliable account-limit source and
would make the Cardputer dependent on Mac implementation details.

## Page Model

`UiPage` becomes:

```text
pet
device_status
codex_status
sync_status
settings
```

Every non-pet page has a title bar, a position indicator, and at most five
visible menu or data rows. Read-only and Settings content calculate their
scroll range from the rows that actually exist.

### Pet page

The current status-bar-plus-pet layout remains unchanged except for five page
dots instead of four. Animation uses the configured runtime frame interval.

### Device Status

Rows are fixed and ordered:

1. Version;
2. PIN;
3. BLE;
4. Wi-Fi;
5. Agent.

The eight-digit PIN is deliberately visible on the physical device because it
is the Web login and Companion credential. The Web login input remains masked.

### Codex Status

Rows are generated in this order:

1. active session title;
2. Model;
3. Fast;
4. Thinking Level;
5. ordinary Codex 5H limit, when available;
6. ordinary Codex Weekly limit, when available;
7. GPT-5.3-Codex-Spark 5H limit, when available;
8. GPT-5.3-Codex-Spark Weekly limit, when available.

Fast is `ON` when the effective session service tier is `priority` or `fast`.
A null, default, standard, or other non-fast tier displays `OFF`.

Limit rows show integer `usedPercent`. A row exists only when the Companion has
a matching, successful observation no more than 120 seconds old. The device
does not display a placeholder for an omitted row.

### Sync Status

Rows are fixed and ordered:

1. IPv4 address;
2. Companion heartbeat age;
3. pet synchronization result and age;
4. active keyboard Profile name.

### Settings

The top-level menu is:

1. Change PIN;
2. Wi-Fi Binding;
3. Keyboard Profile;
4. Brightness;
5. Return to Pet;
6. Pet Frame Rate;
7. Return to Pet Page.

The selected row remains visible while scrolling. The menu footer documents
`;/.` for selection and `/,` for enter and back.

## Navigation and Input Isolation

The UI owns an explicit interaction state:

```text
page_browse
settings_browse
submenu_browse
text_edit
confirm
applying
result
```

Rules:

1. Page navigation outside Settings requires the existing Fn chords so bare
   punctuation remains usable as a keyboard.
2. Settings and submenu browse capture the four bare direction keys.
3. Text edit captures every physical key locally and never calls Profile,
   Macro, BLE HID, or Unicode GATT dispatch.
4. Entering Settings, entering an editor, changing Profile, or leaving an
   editor sends HID Release All first.
5. Captured key releases remain captured.
6. Applying ignores repeated key presses.
7. Return-to-pet timeout is suspended in text edit, confirm, applying, Wi-Fi
   scan, and result states.

## Mac Companion Telemetry

### Session runtime

The existing `CodexAdapter` continues to select the first active thread, or the
most recently updated thread when none is active. For the selected thread it
reads at most the last 8 MiB of the JSONL path returned by `thread/list` and
uses the latest complete `turn_context` for:

- effective model;
- effective reasoning effort.

Fast comes from `config/read.config.service_tier`. The JSONL read,
`config/read`, and `account/rateLimits/read` path is fully read-only. Live
verification rejected `thread/resume` because it changed an unloaded thread
from `notLoaded` to `idle`, despite creating no turn; the Agent must never call
it for telemetry.

### Rate limits

A dedicated reader calls `account/rateLimits/read` at most once per 60 seconds.
It consumes both the backward-compatible `rateLimits` object and
`rateLimitsByLimitId`.

Classification rules are deterministic:

- a 5H window has `windowDurationMins == 300`;
- a Weekly window has `windowDurationMins == 10080`;
- the Spark bucket must have a normalized `limitId` or `limitName` that
  explicitly identifies `GPT-5.3-Codex-Spark`;
- the ordinary bucket must explicitly identify Codex and must not identify
  Spark;
- a missing duration, ambiguous bucket, duplicate conflicting bucket, missing
  percentage, or failed refresh produces no corresponding row.

The last successful observation may be reused for 120 seconds. After that, its
rows are omitted until a new successful response arrives.

### Snapshot compatibility

The existing snapshot adds optional fields:

```json
{
  "model": "gpt-5.6",
  "thinking_level": "high",
  "fast": true,
  "limits": [
    {
      "scope": "codex",
      "window": "5h",
      "used_percent": 38
    }
  ]
}
```

The existing required fields and sequence behavior do not change. Firmware
parses the optional fields with fixed lengths and a maximum of four limit rows.
Old firmware ignores the additions. A malformed optional field is omitted
without rejecting an otherwise valid session snapshot.

The Companion still performs Cardputer requests serially. Limit refresh happens
inside the Mac process and does not create an independent device request.

## Multi-Profile Catalog

### Capacity

The product supports:

- one implicit, immutable SAFE Profile;
- zero to four custom Profiles;
- a maximum encoded custom-catalog payload of 60 KiB.

Each custom Profile retains the existing four layers, 224 bindings, sparse
passthrough representation, action limits, and 16 KiB Web request limit.

### Raw storage layout

The unused tail of the storage partition becomes:

```text
0x1c0000..0x1cffff  Profile Catalog bank A
0x1d0000..0x1dffff  Profile Catalog bank B
```

Each bank contains a `CCPF` header, schema version, monotonically increasing
sequence, payload length, Profile count, and CRC32. The payload contains
bounded opaque Profile IDs, names, revisions, and existing sparse Profile JSON.

Publishing writes and verifies the inactive bank before it becomes the newest
valid sequence. On boot the valid bank with the highest sequence wins. A torn
write, invalid length, invalid Profile, or bad CRC falls back to the other bank.

The active Profile ID is a small NVS value. Activating a Profile does not
rewrite the catalog. SAFE is represented by a reserved ID and is never stored
as user JSON.

### Migration

If neither catalog bank is valid, firmware reads the current single NVS
Profile:

- a valid stored Profile is imported as the first custom Profile;
- a stored name equal to `SAFE` is renamed to `IMPORTED` to avoid colliding
  with the immutable SAFE Profile;
- the imported Profile becomes active only after a catalog bank has been
  written and verified;
- invalid or absent legacy data selects SAFE;
- the legacy NVS value is retained for firmware rollback and is not logged.

### Web API

All routes retain the current PIN authentication:

- `GET /api/v1/profiles` lists IDs, names, revisions, built-in state, and the
  active ID.
- `POST /api/v1/profiles` creates a blank or cloned custom Profile.
- `GET /api/v1/profile?id=<opaque-id>` reads one Profile.
- `PUT /api/v1/profile?id=<opaque-id>` publishes one Profile with revision
  conflict checks.
- `DELETE /api/v1/profile?id=<opaque-id>` deletes an inactive custom Profile.
- `POST /api/v1/profile/activate` activates a supplied ID.
- Existing `GET /api/v1/profile` and `PUT /api/v1/profile` without an ID remain
  compatibility aliases for the active Profile.

The Web UI adds Profile selection, create, clone, rename, delete, and activate.
Per-key mapping stays in the existing editor. An active Profile cannot be
deleted; the Web UI must activate SAFE first.

## PIN Rotation

PIN editing accepts exactly eight digits and requires a matching second entry.
The new value becomes active only after a successful NVS commit. A persistent
monotonic PIN revision increments in the same commit, so the Agent can reject a
replayed or older migration response.

To avoid immediately disconnecting the Mac Companion:

1. the previous PIN is retained in RAM only for five minutes;
2. it is accepted only by `GET /api/v1/companion/action`;
3. an action response authenticated by the previous PIN includes optional
   `next_pairing` and `pin_revision` fields;
4. the Mac Companion atomically rewrites its config file with mode `0600`,
   creates a new `LANBridge`, and retries with the new PIN;
5. the first successful request using the new PIN clears the previous PIN
   early;
6. command-line-only configuration updates in memory and emits a warning that
   restart persistence is unavailable;
7. a device reboot before migration completion intentionally loses the old
   PIN and requires manual Mac configuration repair.

The previous and next PIN are never logged. Old-PIN authorization cannot read
or change Web, Wi-Fi, Profile, pet, or snapshot state. Existing Web sessions
using the old PIN become unauthorized immediately; the browser that performed
the rotation replaces its in-memory credential only after the device confirms
the commit.

## Wi-Fi Binding

The Wi-Fi submenu starts an asynchronous scan. It retains at most 12 unique
SSIDs, choosing the strongest observation for duplicates. A final Hidden
Network row allows manual SSID entry.

After SSID selection:

1. the local editor captures and masks the password;
2. firmware keeps the current credentials in RAM;
3. it attempts the candidate for 15 seconds without persisting it;
4. obtaining an IP commits SSID, password, and a runtime-override marker;
5. failure reconnects the prior credentials and does not change NVS;
6. if prior credentials cannot reconnect, the existing provisioning recovery
   path remains available.

The runtime-override marker makes a user binding take precedence over the
private-image baseline on later boots. The private baseline remains intact as
a recovery source.

Scanning, connecting, committing, and rollback run outside the keyboard scan
and display tasks. SSID and password never enter logs.

## Device Display Settings

A versioned, CRC-protected NVS record stores:

- brightness: 25, 50, 75, or 100 percent;
- return-to-pet: disabled, 15, 30, or 60 seconds;
- pet frame interval: 500, 400, or 333 ms for 2.0, 2.5, or approximately
  3.0 FPS.

Changes preview immediately and persist only after confirmation. A failed NVS
write restores the prior runtime value and displays `SAVE FAILED`. Invalid,
unknown-version, or bad-CRC settings load safe defaults without delaying BLE
startup.

## Error Handling

- Missing Codex session data displays the existing offline or no-session
  state; rate-limit rows remain absent.
- One malformed optional telemetry field does not invalidate the base
  snapshot.
- A stale Companion changes Agent state but does not erase the last pet,
  Profile catalog, or device settings.
- A catalog write failure leaves the active bank and active Profile unchanged.
- A Profile activation failure sends HID Release All and falls back to SAFE.
- A Wi-Fi scan failure returns to the Wi-Fi submenu with a retry row.
- A Wi-Fi candidate failure restores the prior network and displays the
  failure locally.
- A PIN mismatch, invalid length, or persistence failure leaves the existing
  PIN unchanged.
- Settings result screens contain bounded, non-secret error codes.

## Security and Privacy

- All management and Companion routes remain LAN-only and PIN-authenticated.
- No PIN, Wi-Fi password, session content, account identifier, or raw
  rate-limit payload is written to logs or committed artifacts.
- PIN comparison remains constant-time.
- The Web login field remains masked.
- Profile IDs are opaque fixed-length identifiers, not user-controlled paths.
- Catalog lengths, counts, offsets, revisions, and CRC are validated before
  allocation or activation.
- This feature does not expand the existing self-signed HTTPS trust model.
  Certificate pinning is outside this release.

## Testing

### Firmware host tests

- five-page wraparound and scroll bounds;
- Fast immediately follows Model;
- missing and stale limit rows are absent;
- Settings direction capture and text-editor HID isolation;
- HID Release All before page-state transitions and Profile activation;
- catalog bank selection, torn-write fallback, CRC rejection, capacity limits,
  legacy migration, and SAFE fallback;
- PIN validation, five-minute previous-PIN scope, and early revocation;
- Wi-Fi scan deduplication, candidate success commit, failure rollback, and
  runtime precedence;
- settings validation, CRC fallback, and enum persistence.

### Mac Companion tests

- session runtime parsing for model, reasoning effort, and service tier;
- Fast mapping;
- ordinary Codex and Spark bucket classification by explicit identity and
  exact window duration;
- ambiguous, missing, failed, and older-than-120-second limit omission;
- optional snapshot encoding and content comparison;
- PIN migration, atomic config replacement, mode `0600`, and command-line
  memory-only behavior.

### Web tests

- Profile list, create, clone, rename, delete, activate, and compatibility
  aliases;
- active-Profile delete rejection;
- revision conflict and catalog-capacity feedback;
- publication success and failure result dialogs;
- existing chord, ASCII string, Unicode string, sequence, device, and Codex
  actions round-trip in every custom Profile.

### Release and hardware checks

- Python suite;
- normal host C++ suite;
- ASan/UBSan host suite;
- Swift tests, release build, and doctor;
- ESP-IDF 5.5.4 build and partition checks;
- generic/private image assembly and source secret scan;
- app-only flash at `0x20000` to preserve NVS, Wi-Fi, bonds, pet cache, and
  legacy Profile;
- navigation without flicker, reset, stuck keys, or punctuation regression;
- live comparison of session, model, thinking level, and Fast;
- proof that unavailable limits produce no row;
- wrong-password Wi-Fi rollback;
- PIN rotation while the LaunchAgent remains online;
- three custom Profiles edited from Web, switched on device, and retained
  across reboot;
- BLE passthrough, HID chord, ASCII string, Unicode GATT, and pet-sync
  regression checks;
- at least 30 minutes of serialized Agent, status, and pet-sync soak.

## Delivery

- Bump both firmware version sources to `1.0.30`.
- Build the generic and private full images from the final main commit.
- Deploy only the application image at `0x20000` during development and final
  device update.
- Rebuild and reload the Mac LaunchAgent from the same source commit.
- Preserve current user data and publish final artifact paths and SHA-256
  digests.
- Commit all work. Push only if a Git remote exists.

## Out of Scope

- Changing Model, Thinking Level, or Fast from the Cardputer.
- Editing individual key mappings on the Cardputer.
- More than four custom Profiles.
- Multiple saved Wi-Fi networks or roaming.
- Off-LAN access, cloud relay, or public remote control.
- Changes to BLE HID descriptors, the Unicode GATT protocol, or the pet bundle
  format.
- OTA, Secure Boot, eFuse, or partition-table migration.
- Guessing labels or values for unknown Codex rate-limit buckets.
- HTTPS certificate pinning.
