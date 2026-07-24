# Web Profile Sparse Persistence Design

## Goal

Make Web-configured string and other non-passthrough key mappings publish
reliably on the real Cardputer, persist across reloads and reboots, and make the
modal's save behavior match the user's expectation.

## Confirmed Root Cause

The current Profile wire format serializes all 224 bindings as JSON objects,
including passthrough bindings. A default Profile is about 5,193 bytes.

This causes two independent failures:

- `nvs_set_str()` cannot persist the Profile because ESP-IDF limits an NVS
  string to 4,000 bytes when a full page is available. The current
  `persist_profile()` ignores that error.
- GET and PUT handlers construct large cJSON trees while BLE, Wi-Fi, TLS and the
  product runtime are active. Real-device serial evidence shows
  `httpd_uri: uri handler execution failed`; clients receive a disconnected
  response or `Failed to fetch`.

The Web modal also currently updates only the browser's local Profile. A second
page-level publish action is required, which makes a successful-looking modal
action easy to mistake for a device save.

## Selected Architecture

Use a backward-compatible sparse representation inside the existing Profile
JSON envelope:

```json
{
  "name": "SAFE",
  "revision": 1,
  "bindings": [
    null,
    {"kind": "text_utf8", "text": "请检查这段代码"},
    null
  ]
}
```

- The `bindings` array remains exactly 224 entries.
- `null` means the existing safe passthrough action.
- Non-passthrough entries retain the existing action objects and enum values.
- The parser accepts both `null` and the legacy
  `{"kind":"passthrough"}` representation.
- The serializer always emits `null` for passthrough bindings.
- The current Web `cleanAction()` behavior already treats `null` as
  passthrough, so no migration is required in stored browser state.

A default Profile falls from about 5,193 bytes to about 1,161 bytes, below the
NVS string limit and with substantially fewer cJSON allocations.

## Persistence and Atomicity

Profile persistence becomes an explicit success condition:

1. Parse and validate the candidate Profile.
2. Verify its revision matches the active Profile.
3. Increment the candidate revision.
4. Encode the sparse Profile.
5. Persist it with `nvs_set_str()` and `nvs_commit()`.
6. Replace the active in-memory Profile only after persistence succeeds.
7. Return the new sparse Profile to the client.

If NVS open, set or commit fails, the handler returns a JSON 500 response and
keeps the previous in-memory Profile. It must not report success for an
unpersisted configuration.

Existing legacy Profile JSON stored in NVS remains readable. The next
successful publish rewrites it in sparse form.

## Web Behavior

The modal submit action becomes the authoritative save path:

- Rename `应用到按键` to `保存并发布`.
- Update the selected binding in the local Profile.
- Call the existing `PUT /api/v1/profile`.
- Keep the modal open while publication is in progress.
- Close the modal and redraw only after the device returns the updated Profile.
- On failure, retain the editor contents and show the error in the modal.

The page-level `发布配置` button remains available for Profile name changes and
future batch edits. It uses the same publish function and visible error
handling.

## Error Handling

- Invalid or oversized requests continue to return 400.
- Revision conflicts continue to return 409.
- Persistence failures return 500 with `profile_persist_failed`.
- The Web UI shows a visible inline message instead of relying on an alert for
  modal publication errors.
- Pairing PIN, Wi-Fi password and Profile contents are not added to logs.

## Testing

Test-first coverage will include:

- sparse serialization emits `null` for passthrough bindings;
- the parser accepts both sparse nulls and legacy passthrough objects;
- the default sparse Profile is below the 4,000-byte NVS limit;
- persistence failure does not replace the active Profile;
- the Web modal exposes `保存并发布` and calls the publish path;
- failed modal publication leaves the modal open with an inline error;
- existing chord, sequence, UTF-8 string, device and Codex actions round-trip.

Real-device verification will use the existing authenticated Web UI and serial:

1. publish a Chinese UTF-8 string to a test key;
2. reload the Profile and confirm the mapping remains;
3. reboot the Cardputer and confirm the mapping remains;
4. press the mapped key in a Mac text target and confirm Unicode injection;
5. run repeated Profile GET/PUT requests while the Mac agent is active;
6. confirm serial has no URI handler failure, reset, panic or stack overflow.

## Delivery

- Bump the firmware patch version.
- Run targeted RED/GREEN tests, the full product release gate, and real-device
  Web/serial verification.
- Flash the application partition at `0x20000` to preserve Wi-Fi, PIN, bonds and
  existing NVS state.
- Commit the implementation. Push only if a Git remote exists.

## Out of Scope

- A new per-key PATCH endpoint.
- A new Profile schema version or migration tool.
- Changing the BLE HID or Unicode GATT protocols.
- Changing Mac agent polling unless real-device post-fix verification still
  demonstrates contention.
