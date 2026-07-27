# Product Agent Protocol v1

This contract defines the local HTTPS interface shared by Cardputer firmware,
the macOS Companion, and the Windows Machine Agent.

## Transport and authentication

- Transport is HTTPS on the Cardputer's LAN address.
- All Companion endpoints require the `X-Cardputer-Pairing` header.
- The header value is the current eight-digit device PIN.
- The PIN must not be placed in a URL, command-line argument, log, diagnostic
  result, or public release manifest.
- A successful authenticated Companion request counts as Agent activity.
- JSON bodies use UTF-8 and `application/json`; pet chunks use
  `application/octet-stream`.

The device uses a self-signed certificate. A new Agent may accept the
certificate only during an explicit PIN-authenticated first-pair flow and must
persist the leaf certificate fingerprint. Later connections must match the
stored fingerprint.

## Companion status

`POST /api/v1/companion/status` accepts a snapshot described by
`fixtures/status.json` and returns `{"accepted":true}`.

`sequence` is monotonic per Agent process. A snapshot contains the active
session and pet state. `model`, `thinking_level`, `fast`, and `limits` are
optional. When telemetry is unavailable, the key is omitted; `null`, `"NA"`,
and `"N/A"` are not wire values. At most four limit rows are sent.

## Actions and heartbeat

`GET /api/v1/companion/action` is both the action poll and a heartbeat. It
returns the envelope in `fixtures/actions.json`.

`needs_snapshot=true` requests an immediate status post. `next_pairing` and
`pin_revision` appear together only during the bounded PIN-migration window.

## Pet synchronization

The endpoints and request/response examples are in
`fixtures/pet-sync.json`.

- A chunk is at most 8192 bytes.
- Every chunk includes transaction ID, byte offset, and SHA-256 headers.
- An interrupted upload resumes only when transaction ID, expected length,
  and received offset match.
- Commit succeeds only after the firmware validates the complete CCPT v1
  bundle and digest.

## Compatibility

Additive optional response fields are allowed. Removing a field, changing a
wire name, changing authentication, increasing a firmware bound, or changing
CCPT encoding requires an explicit protocol revision and fixtures.
