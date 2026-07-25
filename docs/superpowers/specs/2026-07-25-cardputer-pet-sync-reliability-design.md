# Cardputer Pet Synchronization Reliability Design

## Goal

Ensure a Codex pet selection change on the Mac is reflected on the Cardputer
within 30 seconds under normal LAN operation, without reintroducing concurrent
HTTPS pressure on the ESP32-S3.

This is a reliability correction for the existing 1.0.28 pet synchronization
protocol. It does not change the CCPT format, device storage layout, Web API,
animation rendering, or Bluetooth keyboard behavior.

## Observed Failure

The Mac selected pet was `seedy`, while the Cardputer initially continued to
show the previous cached pet. The device eventually received `seedy`, proving
that source discovery, transcoding, upload, commit, and cache activation still
worked.

The Companion error log repeatedly recorded HTTPS connection resets and
timeouts. In the current main loop, the 30-second pet synchronization check is
inside the same `do` block and after action polling, action execution, snapshot
generation, and snapshot publication. A failure in any preceding operation
skips the pet synchronization check for that loop. Because the pet deadline is
not serviced independently, repeated unrelated request failures can postpone a
pet change indefinitely.

## Considered Approaches

### Independent serialized cadence — selected

Keep all Cardputer HTTPS operations on the existing serial loop, but service
the pet deadline in its own error boundary before action and snapshot work.
Successful pet checks run every 30 seconds. Failed pet checks retry after 5
seconds.

This preserves low device concurrency while preventing action or snapshot
failures from suppressing pet synchronization.

### Dedicated concurrent pet task — rejected

An independent task would make timing simple, but it could overlap action and
snapshot requests. The ESP32 HTTPS server has already shown connection resets
under overlapping request pressure, so this would trade latency for reduced
reliability.

### Filesystem event watcher — rejected

Watching `config.toml` could trigger immediately, but the confirmed product
target is synchronization within 30 seconds. A watcher adds lifecycle and
coalescing complexity without addressing the actual error-boundary defect.

## Runtime Design

The Companion loop retains its two-second cadence and executes these stages in
order:

1. If the pet synchronization deadline is due, call
   `PetSyncCoordinator.synchronize`.
2. Record the result without throwing it into the action path:
   - success: next deadline is 30 seconds later;
   - failure: next deadline is 5 seconds later.
3. Poll and execute a device action.
4. Generate and publish the current Codex snapshot when its content changed.
5. Sleep until the next two-second cycle.

Pet synchronization and action/snapshot work use separate error boundaries.
Failure in either stage is logged and does not skip the other stage.

All HTTPS operations remain serialized. The design does not create a second
network task or issue overlapping requests.

## Scheduling Component

A small monotonic-clock cadence component owns:

- the next synchronization deadline;
- the 30-second healthy interval;
- the 5-second failure retry interval;
- first-run immediate synchronization.

It exposes only deadline inspection and success/failure recording. It does not
perform I/O and can therefore be tested deterministically.

The schedule uses `ContinuousClock`, so wall-clock adjustments cannot delay or
accelerate synchronization.

## Logging

Each attempted pet synchronization records:

- selected or last successful pet ID when available;
- success or stable error code;
- whether the next attempt uses the healthy or retry interval.

Logs must not contain the PIN, pairing header, complete configuration, or
private device URL credentials.

## Verification

Automated checks must prove:

1. first-run synchronization is immediately due;
2. success schedules the next attempt 30 seconds later;
3. failure schedules a retry 5 seconds later;
4. action/snapshot failure cannot bypass an already-due pet attempt;
5. pet failure does not prevent action polling in the same loop;
6. the Companion still issues Cardputer requests serially;
7. all existing pet selection, transcoding, CCPT, firmware, and packaging tests
   remain green.

Hardware-in-the-loop acceptance:

1. start with the Cardputer and Companion online;
2. record the active device pet ID and digest;
3. select a different valid Codex pet;
4. verify the device pet ID and digest change within 30 seconds;
5. verify BLE, Wi-Fi, and Mac status remain `OK`;
6. verify the Companion log contains no credential data.

## Release Boundary

Only the macOS Companion scheduling and its tests require changes. A new
firmware image is not necessary unless the full release gate reveals an
unexpected cross-component contract change. The updated Companion app will be
installed through the existing LaunchAgent workflow.
