# USB HIL Microphone Control Design

## Objective

Allow automated hardware-in-loop tests to start and stop Cardputer microphone
capture without repeated physical G0 presses, while preserving the product
privacy rule that microphone capture cannot be started over Wi-Fi or BLE.

## Scope

The firmware will accept two exact, newline-terminated commands through the
USB serial console:

```text
HIL MIC START
HIL MIC STOP
```

No equivalent Web, HTTPS, BLE, Companion GATT, or Codex Agent command will be
added.

## Firmware Architecture

### Parser

A small allocation-free parser will consume bounded serial input and produce
one of three results:

- `start`
- `stop`
- `none`

Only complete, exact command lines are accepted. Oversized, malformed, or
partial lines are discarded without changing microphone state.

### Runtime integration

The existing UI task will poll the USB serial input without blocking. It will
translate accepted commands into the existing microphone event queue:

- `START` queues the same `g0_click` event as a physical G0 click only when the
  microphone state is `READY`.
- `STOP` queues the same `g0_click` event when the microphone is starting,
  live, or stopping; it is otherwise an idempotent no-op.

The interface does not call the capture backend directly. Existing gates remain
authoritative: encrypted BLE connection, current Companion binding, Audio Data
and Audio Status subscriptions, protocol negotiation, and `sink_ready`.

The firmware writes an acknowledgement to the same serial console. It contains
only the command result and microphone state, never audio data.

### Animation isolation

While the microphone state is `STARTING`, `LIVE24`, `LIVE16`, or `STOPPING`,
the PET page keeps the last rendered pet frame and stops advancing animation
frames. Status-bar and microphone-state updates remain enabled. Animation
resumes from the next frame after capture returns to a non-active state.

This prevents LCD frame decoding and transfer from competing with PDM capture
and BLE notifications.

## HIL Runner

The audio feasibility runner will own the serial descriptor in read/write,
non-blocking mode.

1. Start the Companion audio probe.
2. Wait until the serial evidence shows that the BLE audio sink is ready.
3. Send `HIL MIC START`.
4. Keep the existing first-audio-frame start gate; an acknowledgement alone is
   not proof that capture works.
5. Run the requested measurement interval.
6. Send `HIL MIC STOP` in a `finally` path on success, validation failure,
   timeout, interrupt, or child-process failure.

The runner continues to store metrics only. It must not persist PCM, ADPCM, BLE
payloads, or decoded samples.

## Failure Handling

- Serial write failure aborts the HIL run with a clear error.
- A rejected or no-op `START` does not start the timer.
- If no first audio frame arrives before the existing bounded start timeout,
  the run fails and still sends `STOP`.
- `STOP` is best-effort during cleanup and cannot mask the original failure.
- A malformed serial command cannot toggle the microphone.

## Testing

### Host tests

- Exact commands parse correctly.
- Partial, oversized, and malformed input cannot generate an action.
- `START` and `STOP` mapping is idempotent for every microphone state.
- PET animation is disabled only for active microphone states.

### Python tests

- The runner writes `START` only after the probe is launched.
- The runner always attempts `STOP`, including timeout and interruption paths.
- A serial acknowledgement does not replace the first-frame start gate.
- Reports remain metrics-only.

### Hardware validation

- Automated serial `START` reaches `MIC LIVE` without a physical G0 press.
- Automated serial `STOP` returns to `MIC READY`.
- PET animation remains frozen during capture and resumes after stop.
- The final 30-minute HIL meets audio loss, gap, reconnect, heap, stack, TLS,
  HID, and privacy gates.

## Security and Privacy Boundary

USB physical access is required. The feature introduces no network-reachable
microphone start path, does not bypass BLE encryption or Companion ownership,
does not auto-start after reboot or reconnect, and does not change the existing
physical G0 behavior.

## Concurrent HID Gate Extension

The same physical USB-only parser also accepts the exact command
`HIL HID START`. When the bonded HID link is fully ready and no prior burst is
active, firmware schedules exactly 1,000 neutral HID source events at one event
per 500 ms.

Each event uses HID keyboard usage `0x00` (`NoEvent`) and alternates press and
release state. It therefore traverses the real fixed-capacity
`KeyboardProbe` sender queue and `esp_hidd_dev_input_set` path without typing
characters or shortcuts on the Mac. The existing HID generated, queued,
overflow, latency, and sender-task stack metrics remain the release evidence;
the gate does not directly increment or synthesize metric counters.

Firmware emits `HIL HID START ACCEPTED`, `HIL HID START REJECTED`, and
`HIL HID COMPLETE 1000` acknowledgements. The HIL runner starts the burst only
after its pre-run resource baseline and microphone-start acknowledgement.
The existing report gate requires at least 1,000 generated and queued events,
so short diagnostic runs may finish before the completion line while the final
30-minute run cannot pass without the full burst. The command is not
exposed through BLE, Wi-Fi, Web, Companion, or Codex Agent surfaces.
