# macOS GATT Reboot Recovery Design

## Problem

When the Cardputer reboots while the macOS Agent remains running, the normal
BLE HID connection can recover while the Agent's custom GATT audio session
remains unavailable. Device status then reports BLE, Wi-Fi, and Agent as `OK`
but microphone as `UNAVAILABLE`.

A controlled Agent restart restores `MIC READY` within one second. The current
`ProductGATTConnection` retries only after `didDisconnectPeripheral`. It does
not handle `didFailToConnect`, and it has no deadline for a connection,
service-discovery, characteristic-discovery, or audio-handshake attempt that
never calls back.

## Selected approach

Keep recovery inside the existing Agent process and existing
`CBCentralManager`. Add a small, deterministic recovery policy to the
`ProductGATT` module and route every recoverable CoreBluetooth failure through
one cleanup-and-retry path.

The alternatives are rejected:

- Restarting the whole Agent would recover the symptom but hide the broken
  GATT lifecycle and interrupt unrelated LAN/Codex work.
- Adding only `didFailToConnect` would not recover attempts that hang without a
  callback or fail during discovery, notification setup, or the audio
  handshake.

## Recovery state and timing

The recovery policy tracks these phases:

- `idle`
- `scanning`
- `connecting`
- `discovering`
- `subscribing`
- `ready`
- `stopped`

Connection, discovery, and subscription phases each receive an 8-second
watchdog. A failure, timeout, or unintentional disconnect clears the peripheral,
characteristics, pending write, and audio-ready session state before retrying.

Retry delays are 500, 1000, 2000, and 5000 milliseconds, capped at 5000
milliseconds. A successful audio-ready handshake resets the backoff. An
intentional stop cancels all timers and never schedules a retry. If Bluetooth
is unavailable, recovery waits for `centralManagerDidUpdateState` to report
`.poweredOn`.

Each scheduled callback carries a generation number. Stale callbacks from an
older connection attempt must not alter the current session.

## CoreBluetooth integration

`ProductGATTConnection` will:

- handle `didFailToConnect`;
- arm or replace the watchdog when selecting a peripheral, connecting,
  discovering services/characteristics, and starting notification/handshake
  setup;
- cancel the watchdog after `writeSinkReady` succeeds;
- recover on service/characteristic discovery errors, missing required
  services or characteristics, failed bind/hello/sink-ready writes, and failed
  audio notification setup;
- publish the cleared session before retrying so firmware immediately leaves
  the stale ready state;
- log phase and failure reason without device identifiers, PINs, or audio data.

Unicode and HID behavior must remain unchanged. Recovery must not rotate the
PIN, erase BLE bonds, restart the Agent, or modify the HAL/XPC protocol.

## Testing

Unit tests will cover the pure recovery policy:

- callback failure schedules bounded backoff;
- timeout uses the same path;
- success resets backoff;
- intentional stop never retries;
- stale generations are ignored;
- Bluetooth unavailable waits for `.poweredOn`.

The Swift product test suite will continue covering bind, notification, hello,
and sink-ready ordering.

A macOS hardware-in-loop gate will keep the Agent PID fixed while resetting the
Cardputer five times. Every cycle must return to `MIC READY` within 15 seconds,
with BLE, Wi-Fi, and Agent all `OK`, no Agent restart, and no device panic or
allocation failure. The final cycle will start and stop capture through the
existing USB HIL commands to prove the recovered subscription carries audio.

## Release and boundaries

The fix will ship as unified product version **1.3.3** and Launcher-compatible
firmware version **1.3.3l**. Firmware runtime behavior is unchanged apart from
the displayed/versioned release surfaces; the functional fix is in the macOS
Agent.

The XPC doctor's exclusive producer-lease false negative is documented but out
of scope. It requires a separate protocol/diagnostic design and is not needed
to restore reboot recovery.
