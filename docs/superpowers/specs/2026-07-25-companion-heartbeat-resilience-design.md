# Companion Heartbeat Resilience Design

## Goal

Prevent the Cardputer from reporting `Mac OFFLINE` while the configured
macOS Companion is still running and making authenticated LAN requests.

The user accepts a maximum offline-detection delay of approximately 30 seconds.
This is a firmware-side liveness correction. It does not change BLE HID,
Codex session semantics, Web authentication, pet bundle storage, or the Mac
Companion polling cadence.

## Observed Failure

The macOS LaunchAgent and its `codex app-server` child remained alive. The
Companion log continued to record pet checks, and the Cardputer later returned
to `Mac OK` without restarting the agent. The failure was therefore an
intermittent device-side liveness classification rather than an agent process
exit.

The firmware currently declares the Companion stale after 10 seconds. Normal
action requests use a five-second curl timeout, while pet upload operations may
use a 15-second timeout. Recent logs also contain transient curl 35 connection
resets and curl 28 timeouts. A short sequence of transport failures can
therefore exceed the firmware's stale window even though the agent remains
healthy.

The successful authenticated `GET /api/v1/companion/pet` request does not
currently refresh the Companion heartbeat. This request is issued by the
30-second pet synchronization cadence and is direct evidence that the
configured agent is alive, but the firmware ignores it for liveness.

## Considered Approaches

### Thirty-second window plus authenticated activity — selected

Increase the stale window to 30 seconds and count every successful,
authenticated Companion endpoint as liveness activity. This aligns the
firmware timeout with the existing serialized HTTPS timing without increasing
request volume.

### Dedicated Mac heartbeat request — rejected

Add an independent periodic endpoint and task in the Mac Companion. This can
provide a more explicit signal, but it adds HTTPS traffic and can overlap with
pet uploads on the constrained ESP32 server. The current authenticated requests
already provide sufficient proof of life.

### Timeout-only change — rejected

Increase the stale window without changing endpoint semantics. This reduces
false offline reports, but it still ignores a successful authenticated pet
status request and leaves the liveness model incomplete.

## Firmware Design

`CompanionProtocol` will use a 30,000 ms stale threshold. The existing
snapshot and heartbeat mechanisms remain unchanged.

After authentication succeeds, each endpoint used exclusively by the Mac
Companion refreshes liveness:

- snapshot publication, including an authenticated request whose later
  payload validation fails;
- action polling;
- pet status query;
- pet upload begin;
- pet upload chunk;
- pet upload commit.

An accepted snapshot also refreshes the protocol timestamp through
`CompanionProtocol::apply`; the earlier request heartbeat makes authenticated
request activity explicit even when the snapshot payload is rejected.

Unauthorized requests must never refresh the heartbeat. An authenticated
request may refresh liveness before later payload validation because successful
authentication proves that the configured Companion is alive even when an
individual application payload is invalid.

The Web status endpoint, Profile API, Wi-Fi API, PIN API, and browser traffic
must not refresh Companion liveness.

## Runtime Behavior

The Cardputer remains `Mac OK` while it receives at least one authenticated
Companion request within each 30-second window. Transient curl reset or timeout
bursts shorter than the window no longer cause screen flapping.

When the Mac Companion genuinely stops or loses LAN connectivity, the device
transitions to `Mac OFFLINE` after 30 seconds without an accepted snapshot or
authenticated Companion request. Existing waiting-pet and stale-session UI
fallbacks remain in effect.

The Mac Companion keeps its current two-second action loop, 30-second healthy
pet check, five-second failed pet retry, and serialized HTTPS transport.

## Version and Deployment

The firmware version will advance from `1.0.28` to `1.0.29` in every existing
version source verified by the release tests.

After the full release gate passes, deploy only
`firmware/build/cardputer_codex_companion.bin` at offset `0x20000`. This
preserves the device's NVS-backed PIN, Wi-Fi credentials, Profile mappings,
BLE bonds, and cached pet bundle. The generated private full image remains the
recovery and delivery artifact.

The Mac Companion does not require a behavior change. Its LaunchAgent may be
reloaded after packaging so runtime and repository artifacts are known-good,
but no new traffic or configuration is introduced.

## Verification

Automated tests must prove:

1. a snapshot is not stale at 29,999 ms after its last activity;
2. it becomes stale at 30,000 ms;
3. a heartbeat resets the same 30-second boundary;
4. the authenticated pet status handler refreshes liveness;
5. every existing Companion pet upload/action handler retains heartbeat
   coverage;
6. unauthorized requests remain rejected before heartbeat activity;
7. all existing firmware, Web, Swift, partition, and packaging tests remain
   green.

Hardware acceptance must prove:

1. version `1.0.29` boots without panic or restart;
2. BLE and Wi-Fi remain operational;
3. Mac status remains `OK` during normal action and pet polling for an
   observation period longer than the old 10-second threshold;
4. the current pet ID/digest remains synchronized;
5. the existing PIN, Wi-Fi, Profile, BLE bond, and pet cache survive the
   app-only update;
6. no credential value is written to logs, tests, documentation, or commits.
