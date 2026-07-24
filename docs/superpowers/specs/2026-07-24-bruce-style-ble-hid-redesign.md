# Bruce-style BLE HID Redesign

## Context

The current 1.0.15 firmware can boot, advertise, and accept BLE/GATT connections, but the user reports that macOS still receives no keyboard echo after reconnecting. BruceDevices/firmware provides a working Cardputer BLE keyboard reference through `src/modules/badusb_ble/ducky_typer.cpp` and `lib/Bad_Usb_Lib/BleKeyboard.cpp`.

## Reference Findings

Bruce's BLE keyboard model has three behaviors that matter here:

- It uses a standard keyboard input report shape: modifier byte, reserved byte, and six key slots.
- It starts sending only after the HID client is connected and subscribed to input-report notifications.
- It sends press state and then an explicit release state, with notification retry for transient NimBLE buffer pressure.

The current firmware diverges in two ways that can explain "connected but no echo":

- `keyboard_report_map()` declares five key-array slots while `HidReport` sends six key bytes.
- `ble_keyboard_ready()` is gated on encrypted connection state only; it does not prove the Mac subscribed to HID input report notifications.

## Required Behavior

- BLE HID report-map keyboard input must declare six key slots, matching `sizeof(HidReport::keys)`.
- Keyboard ready must require all of:
  - GAP connection established.
  - BLE link encrypted.
  - ESP HID service reported connected.
  - A non-Companion HID notify characteristic has current notify subscription enabled.
- The Companion GATT notify subscription must not make the HID keyboard ready.
- Physical key paths must suppress reports until the ready predicate is true.
- When reports are sent, failures must be logged and retried briefly for transient send errors.
- Existing Web configuration, Profile routing, Companion UTF-8/Chinese path, Wi-Fi, and screen status must remain intact.

## Implementation Boundary

Stay on ESP-IDF/NimBLE and keep `esp_hidd_dev_init`; do not import Arduino `BleKeyboard` into this project. Reuse Bruce's state model and report descriptor semantics rather than its framework dependency.

## Verification

- Host tests prove the report-map slot count and ready-state predicate.
- Full release gate must pass.
- Final private full image must be flashed at `0x0`.
- Serial validation must show firmware 1.0.16 booting without panic, stack overflow, advertising failure, or reboot.
- If macOS GUI/HID pairing cannot be automated, state that boundary explicitly rather than claiming HID echo is verified.
