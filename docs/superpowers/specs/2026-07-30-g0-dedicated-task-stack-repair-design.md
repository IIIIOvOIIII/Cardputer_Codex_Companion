# G0 Dedicated-Task Stack Repair Design

## Problem

Cardputer Codex Companion 1.3.4 executes an enabled G0 chord and the
microphone toggle on the shared `product-macro` task. Hardware serial evidence
shows that one G0 execution reduces the task's high-water free stack from 1,080
bytes to 24 bytes. A second execution reaches the FreeRTOS stack-overflow hook
and reboots the ESP32-S3 with `RTC_SW_CPU_RST`.

The previous release HIL ran one enabled G0 click, so it proved ordering but did
not exercise the second invocation that detects the overflow.

## Chosen Architecture

G0 dual action moves to a dedicated static task and queue:

- `product-g0` has a 3,072-byte static stack and the same priority as
  `product-macro`;
- an eight-entry `G0Invocation` static queue carries the modifier/usage
  snapshot captured at keypress time;
- disabled G0 continues to enqueue the Mic event directly and never wakes the
  dedicated task;
- enabled G0 queues the invocation, and queue failure immediately falls back
  to the Mic event without sending the chord;
- long G0 presses remain ignored.

The dedicated task continues to call `execute_g0_dual_action()` and
`MacroEngine`, preserving the current direct BLE HID press, 30 ms hold, release,
then Mic-toggle order.

## HID Serialization

The existing `MacroEngine` is shared with Profile macros. A new static FreeRTOS
mutex guards complete MacroEngine executions in both `product-macro` and
`product-g0`. This prevents the two tasks from interleaving HID press/release
reports.

For the G0 task:

1. receive a configuration snapshot from the G0 queue;
2. acquire the macro-execution mutex;
3. execute the HID chord and release report;
4. enqueue the Mic toggle;
5. release the mutex and log the bounded result.

If the mutex cannot be acquired within the existing one-second lock timeout,
the G0 worker sends no partial chord and enqueues the Mic toggle as the
privacy-preserving fallback.

## Resource and Failure Boundaries

The new task consumes 3,072 bytes of static internal RAM plus its FreeRTOS
control block and the small eight-entry queue. This is intentionally more
expensive than only increasing the shared stack, matching the selected
isolation design.

Boot fails the keyboard stage if the G0 queue, execution mutex, macro task, or
G0 task cannot be created. Runtime telemetry adds a `g0-dual` task entry with
configured stack and high-water free bytes. Existing queue failure and Mic
fallback logs remain bounded and contain no chord content beyond existing HID
diagnostics.

No Web schema, device settings record, Profile schema, BLE protocol, Mic
protocol, PIN, Wi-Fi, Agent configuration, or partition layout changes.

## Regression and Hardware Verification

Implementation is test-driven:

1. a source/runtime contract test first fails because no dedicated G0 queue,
   task, execution mutex, or telemetry entry exists;
2. HIL-report tests first fail because the runner only supports one click and
   does not gate stack headroom;
3. the HIL runner is extended to execute 20 enabled G0 clicks sequentially,
   waiting for each completion and Mic transition;
4. the report records completion count, Mic-transition count, boot count,
   queue-failure delta, and minimum `g0-dual` free stack;
5. acceptance requires 20/20 completions and Mic transitions, zero reset,
   zero queue failures, and at least 768 bytes of G0 task stack remaining;
6. disabled-mode HIL still proves Mic-only behavior.

The final candidate must also pass normal and ASan/UBSan host tests, the clean
ESP-IDF Factory and Launcher builds, the Launcher `0x190000` app-size gate,
product release checks, and an app-only flash to the attached Launcher device
at `0x170000`, preserving its existing storage and settings.

## Release

All active firmware, macOS Agent, Windows Agent, installer, manifest, Web
Installer, documentation, and checksum surfaces move together to 1.3.5.
Factory reports `1.3.5`; Launcher reports `1.3.5l`.

The public release is created only after hardware stress passes. GitHub Release
assets and Pages Factory firmware must match the pinned SHA-256 digest.

## Acceptance Criteria

- Enabled G0 always sends the configured chord before toggling Mic.
- Disabled G0 remains Mic-only.
- Profile macros and G0 cannot interleave HID reports.
- Queue or execution-lock failure still toggles Mic without a partial chord.
- Twenty sequential enabled G0 HIL clicks cause no panic or reset.
- `product-g0` retains at least 768 bytes of free stack during the stress run.
- Device returns with BLE, Wi-Fi, Agent, and Mic healthy.
- Versioned 1.3.5 release artifacts pass all existing security and packaging
  gates and are published with matching digests.
