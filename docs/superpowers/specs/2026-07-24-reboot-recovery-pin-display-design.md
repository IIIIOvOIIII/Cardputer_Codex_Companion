# Cardputer Reboot Recovery, Stable PIN, and Larger Display Design

## Goal

Fix three real-device issues in the product firmware:

1. After the Cardputer reboots, macOS can still show the BLE keyboard as connected while the device remains `BLE:OFFLINE`; the firmware must actively recover instead of waiting forever.
2. The Web PIN must remain stable across reboots unless the user explicitly rotates it in Web Settings.
3. The Cardputer display text must be larger and more readable.

## Design

### BLE reboot recovery

The BLE keyboard readiness model remains strict: HID input is allowed only when GAP is connected, encryption succeeded, authentication is present, HIDD is connected, and the HID input report is subscribed. The recovery change does not weaken pairing or fall back to Just Works.

The firmware will add a watchdog policy around this existing model:

- If there is no GAP connection and advertising is inactive, restart HID advertising.
- If GAP/HIDD appears connected but the keyboard is not ready for a bounded interval, terminate the stale link so macOS can reconnect to the freshly rebooted device.
- While waiting for recovery, product UI status should show `STARTING` rather than permanently `OFFLINE`; `OK` remains reserved for full HID-ready state.

### Stable PIN

The firmware currently generates a random PIN before attempting to read NVS. The corrected lifecycle is:

- On boot, read `product/web_pin` from NVS.
- If the stored value is a valid 8-digit PIN, use it.
- If no valid PIN exists, generate one valid 8-digit PIN and immediately persist it to NVS.
- Web Settings `POST /api/v1/pin` remains the only PIN rotation path and must update NVS before changing the active in-memory PIN.

### Larger display text

The display renderer will use a larger body text size for boot and runtime pages. Runtime text will be shortened to fit the 240x135 Cardputer display:

- status line: `B:<state> W:<state> M:<state>`
- mode/profile line
- IP line
- PIN line
- active session title
- state plus approval/input counts

The less important cwd line is removed from the device screen to preserve readability.

## Verification

- Add/adjust host tests for BLE advertising/stale-link policy.
- Add/adjust host tests for stable PIN load action.
- Add/adjust UI model tests for shortened display text.
- Add a product packaging regression that the display renderer uses larger body text.
- Run `scripts/verify_product_release.sh`.
- Flash `dist/private/cardputer_codex_companion-private-full.bin` to `/dev/cu.usbmodem21201`.
- Verify after reboot:
  - `/api/v1/status` reports the new version.
  - The Web PIN remains usable after reboot unless changed by Web Settings.
  - Mac agent comes back online.
  - Display text is visibly larger.
