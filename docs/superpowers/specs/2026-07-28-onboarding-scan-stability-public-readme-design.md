# Cardputer Codex Companion 1.2.1 Scan Stability and Public README Design

## Goal

Stop blank-device onboarding from rebooting when `SCANNING NETWORKS` finishes,
publish a verified `1.2.1` firmware and matching Agent packages, and prepare
the repository for its first GitHub push with English-first bilingual
documentation and an Apache-2.0 license.

## Confirmed Decisions

- Wi-Fi scan completion uses deferred processing on the existing
  `wifi-state` task.
- The ESP default event-loop callback performs no scan-result allocation,
  sorting, input locking, onboarding mutation, or UI rendering.
- Scan storage is fixed and bounded: inspect at most 48 AP records, deduplicate
  by SSID, and publish the strongest 12.
- No additional worker task is introduced.
- All live product/package version surfaces advance from `1.2.0` to `1.2.1`.
- `README.md` is English and begins with a link to `README.zh-CN.md`.
- The public author is `IIIIOvOIIII`.
- The repository is licensed under Apache License 2.0.
- The GitHub remote is
  `git@github.com:IIIIOvOIIII/Cardputer_Codex_Companion.git`.
- GitHub SSH authentication uses the private key corresponding to
  `~/.ssh/id_co_openclaw.pub`, namely `~/.ssh/id_co_openclaw`.

## Evidence and Root Cause

The attached blank Cardputer running `1.2.0` reproduces the failure on every
boot. About five seconds after startup, immediately when the Wi-Fi scan
finishes, serial output reports:

```text
assert failed: xQueueGenericSend queue.c:937
Backtrace:
  xQueueGiveMutexRecursive
  esp_event_loop_run ... esp_event.c:727
  esp_event_loop_run_task
```

Address resolution against the flashed ELF places the failure at the default
event loop's `xSemaphoreGiveRecursive(loop->mutex)` after registered handlers
return. The event task is configured with a 2304-byte stack. The registered
`WIFI_EVENT_SCAN_DONE` handler currently calls `publish_scan_results()`
directly; that path allocates two vectors, reads and sorts up to 48 APs,
copies strings, acquires the input and UI mutexes, mutates onboarding state,
builds display content, and renders a page. The handler corrupts event-loop
task state before it returns, so the loop fails while releasing its own
recursive mutex.

This is not a power, watchdog, BLE, or Wi-Fi credential failure. Increasing
the event stack alone was rejected because it keeps application work inside
the system event task and leaves the same failure mode available as the UI
grows.

## Firmware Design

### Event boundary

`WIFI_EVENT_SCAN_DONE` records only a pending scan-completion signal and
returns. The existing `wifi-state` task consumes that signal, obtains the AP
records, publishes the bounded results, and invokes the registered product
callback. The maximum added delivery latency is the existing 250 ms task
period.

Only one ESP scan can be active at a time. Starting a scan clears stale
pending state before calling `esp_wifi_scan_start()`. A completion is consumed
once. Failed or empty scans still invoke the callback with an empty span so
the onboarding controller can present a stable retry screen.

### Bounded result selection

Temporary `std::vector` allocations are removed. A static array stores at most
48 raw records. A second fixed array stores at most 12 unique public results.
For repeated SSIDs, the strongest record wins. When more than 12 unique SSIDs
exist, a stronger candidate replaces the weakest retained result. The final
published slice is sorted by descending RSSI with SSID as the deterministic
tie-breaker.

No SSID or password is emitted into logs.

### Tests and device gate

A regression test must fail against `1.2.0` because scan completion is handled
directly in the system event callback. After the change it must prove that:

- the event callback only marks deferred work;
- result publication occurs from the `wifi-state` task;
- no `std::vector` remains in the scan-result path;
- duplicate and over-capacity result selection remains deterministic.

The firmware is then built and flashed at application offset `0x20000` on the
currently blank test device. The hardware gate passes only if serial shows a
completed scan and the device remains up for at least 60 seconds without a
panic, assertion, or reboot. A generic full image is packaged separately for
new-device flashing.

## Documentation and Licensing

`README.md` is rewritten in English with:

- a first-line Chinese-language link;
- project overview and feature summary;
- supported hardware/software and platform boundaries;
- public full-image flashing and preserved-state upgrade instructions;
- first-run Wi-Fi, BLE, and Agent onboarding;
- macOS and Windows Agent install/status/uninstall instructions;
- build, test, security, and release-artifact guidance;
- author and Apache-2.0 license sections.

`README.zh-CN.md` preserves equivalent Chinese instructions. Commands and
artifact names must agree with `1.2.1`. `LICENSE` contains the unmodified
Apache License 2.0 text, and the README copyright notice identifies
`IIIIOvOIIII`.

## Release and Git Boundary

Before pushing:

1. all focused and full release gates pass;
2. generic firmware is verified to contain no provisioned Wi-Fi data;
3. the all-ref/reflog/unreachable-object credential audit reports zero issues;
4. the attached-device scan stability gate passes;
5. only task-related source and documentation changes are committed.

The remote is added only after local verification. Push uses
`IdentitiesOnly=yes` with `~/.ssh/id_co_openclaw`; the `.pub` file is an
identity declaration, not a file an SSH client can use for signing.

## Out of Scope

- Redesigning the onboarding screens or navigation.
- Changing Wi-Fi credential persistence.
- Adding a new scan worker task.
- Installing the locally purged macOS Agent or audio driver.
- Completing the pending Windows runtime HIL.
- Publishing a GitHub Release or uploading release assets; this task pushes
  the verified repository branch only.
