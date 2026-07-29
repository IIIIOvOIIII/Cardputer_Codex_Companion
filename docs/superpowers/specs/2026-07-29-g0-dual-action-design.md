# G0 Dual-Action Design

## Objective

Allow the Cardputer G0 microphone button to optionally emit one configured
keyboard chord before it toggles microphone capture. The chord is a
device-global setting, is disabled by default, and is activated only after an
authenticated user enables and saves it from the Web Settings page.

## User-visible behavior

- A short G0 press retains the existing microphone toggle behavior.
- When the optional chord is disabled, G0 emits no keyboard report.
- When enabled, a short press emits the stored chord and its release report,
  then submits the microphone toggle event.
- A long G0 press remains ignored and triggers neither action.
- The chord setting applies to every keyboard Profile and does not change when
  the active Profile changes.
- If BLE HID is unavailable, the HID queue is full, or chord execution cannot
  complete, the firmware records the failure and still submits the microphone
  toggle. The optional chord must never disable the privacy control.

## Configuration and persistence

Extend `DeviceSettings` with:

```cpp
bool g0_chord_enabled = false;
uint8_t g0_chord_modifiers = 0;
uint8_t g0_chord_usage = 0;
```

The existing 12-byte `display_cfg` NVS record has two unused payload bytes
covered by its CRC. Encode the usage in byte 6 and encode the enabled flag plus
the four supported modifier bits in byte 7:

- byte 6: one non-modifier HID usage;
- byte 7 bit 7: enabled;
- byte 7 bits 0-3: Ctrl, Shift, Alt, and Cmd;
- all other byte 7 bits must be zero.

Existing version-1 records have zero in both bytes, so they decode as disabled
without migration and retain their brightness, return timeout, and pet frame
rate. An enabled setting requires a non-zero supported keyboard usage and a
modifier mask no greater than `0x0f`. Supported base-key usages are the HID
Keyboard/Keypad range `0x04` through `0x65`; modifier usages are represented
only by the mask. A disabled setting may retain a captured chord so the user
can turn the feature back on without recapturing it.

## Firmware execution

Extend `MacroInvocation` with an invocation kind for the G0 dual action.
For a valid short press:

1. If the feature is disabled, submit the existing microphone click event
   directly.
2. If enabled, enqueue one G0 invocation on `g_macro_queue`.
3. The macro task snapshots the persisted setting and uses the existing
   `MacroEngine` and dedicated `keyboard-hid` sender path to emit the chord,
   wait the established press duration, and emit release.
4. After chord execution returns, submit the microphone click event.
5. If the macro queue cannot accept the invocation, submit the microphone
   click immediately and increment/log the G0 chord failure.

`ProductMacroSink::send_hid` remains best-effort when BLE HID is unavailable.
The macro task therefore always proceeds to the microphone click, regardless
of HID readiness.

The serial HIL surface gains a G0 click command that follows the same runtime
dispatcher as the physical button. This permits repeatable HID-plus-microphone
testing without requiring repeated physical interaction.

## Web API

Add authenticated routes:

- `GET /api/v1/settings/g0-chord`
- `PUT /api/v1/settings/g0-chord`

GET response:

```json
{
  "enabled": false,
  "modifiers": 0,
  "usages": []
}
```

PUT accepts the same object. `usages` must contain zero or one integer HID
usage; when `enabled` is true it must contain exactly one valid non-zero
keyboard usage from `0x04` through `0x65`. The modifier mask must be an integer
from 0 through 15.
Malformed input returns `400 invalid_request`; persistence failure returns
`500 settings_save_failed`. The in-memory setting changes only after the NVS
commit succeeds.

The Web server accesses settings through registered getter and apply callbacks
owned by the product controller. It does not own a second persistence copy.

## Web Settings UI

Add a `G0 双动作` card containing:

- an `启用 G0 组合键` checkbox, off by default;
- a read-only chord capture field using the same modifier/usage conversion as
  the existing key editor;
- a short explanation that G0 sends the chord first and then toggles Mic;
- a save button.

The page loads the device value after authentication. Saving uses the existing
page-level result dialog for both success and failure. Disabling the checkbox
does not erase the captured chord in the form or on the device.

Generated `firmware/main/product/web_assets.hpp` must be regenerated from the
Web sources and verified byte-for-byte by the existing asset gate.

## Error handling and observability

- Invalid Web values are rejected before NVS access.
- NVS failure leaves the previous runtime setting active.
- Macro queue overflow never suppresses the microphone click.
- BLE HID unavailability suppresses only the chord.
- Runtime logs distinguish disabled, queued, queue-fallback, and completed G0
  dual actions without logging PINs, Wi-Fi credentials, or other secrets.
- Existing HID queue overflow and latency metrics remain authoritative for
  report delivery.

## Test strategy

Use test-driven development:

1. `DeviceSettings` codec/store tests for the disabled default, legacy
   compatibility, configured round trip, invalid masks/usages, and failed
   persistence rollback.
2. Pure dispatch tests for disabled, enabled, long-press, queue-full fallback,
   and exact `chord -> release -> microphone` ordering.
3. Product Web tests for route authentication, JSON shape, validation, and
   save failure.
4. Web asset and browser-side tests for load, chord capture, checkbox behavior,
   save payload, and result dialog.
5. Normal and sanitizer host suites.
6. Factory and Launcher builds, partition validation, DIRAM gate, and
   credential audit.
7. Attached-device HIL proof showing the configured HID report, release,
   microphone state transition, zero queue failures, and no reset.

## Version and release

This feature is released as:

- Factory firmware and Machine Agents: `1.3.4`;
- Launcher-compatible firmware: `1.3.4l`;
- GitHub Release tag: `v1.3.4`.

All active version surfaces, installers, checksums, README release pointers,
and `release/product-release.json` move together. Publication occurs only
after the complete release gate and credential-history audit pass. The
matching GitHub Release is published before the fail-closed Pages workflow is
run, followed by HTTP and digest verification of the live Web Installer.

## Out of scope

- Per-Profile G0 chords.
- Multiple base keys or arbitrary input sequences on G0.
- Web start/stop microphone controls.
- Changes to long-press behavior.
- Changes to the existing 56-key Profile schema.
