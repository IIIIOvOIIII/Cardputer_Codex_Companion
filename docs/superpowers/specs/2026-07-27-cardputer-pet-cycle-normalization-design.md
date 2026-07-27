# Cardputer Pet Cycle Normalization Design

## Goal

Release Cardputer Codex Companion `1.1.6` so sparse Codex pet atlases retain
their authored animation cycle instead of over-representing the first frames
when the Mac Companion expands fewer than eight visible source cells into the
fixed eight-frame CCPT bundle.

## Confirmed Root Cause

Firmware advances the pet frame index through `0...7`; it is not stuck at
frame 0 or 1.

Rocky v4 uses six visible cells followed by two transparent cells for the
WAITING and WORKING rows. Their rendered pixel sequence is
`A,B,C,D,A,B,--,--`. `PetTranscoder` currently removes the transparent cells
and expands the remaining six as `visibleFrames[index % 6]`, producing
`A,B,C,D,A,B,A,B`. The last half of the resulting cycle therefore
over-represents A and B. The current executable test encodes that behavior as
an expectation.

Other pet atlases must not be assumed to use this layout.

## Strict Period Rule

Period detection operates on the complete rendered RGB565 frame arrays after
crop, scale, alpha composition, and background fill. It does not use source
column numbers, bounding boxes, hashes, approximate image similarity, or pet
IDs.

Given the ordered visible frames `V` with count `n`:

1. Keep the existing rule that removes only fully transparent source cells.
2. Consider candidate periods `p` from 1 through `n - 2`.
   Counts below three have no candidate and use the fallback.
3. A candidate is proven only when every observed frame from `p` through
   `n - 1` is exactly equal, pixel for pixel, to `V[index % p]`.
4. Requiring `p <= n - 2` means at least two observed tail frames must repeat
   the candidate prefix. A single matching tail frame is insufficient.
5. Choose the smallest proven period.
6. If a period is proven, expand the first `p` frames to eight by modulo `p`.
7. Otherwise retain the current conservative fallback: expand all `n` visible
   frames to eight by modulo `n`.

For Rocky's `A,B,C,D,A,B`, the proven period is four and the output is
`A,B,C,D,A,B,C,D`. If any pixel differs in a frame comparison required to
prove that period, or only one tail frame repeats, that candidate is rejected.

## Components and Data Flow

Only the Mac `ProductPet` transcoder changes animation behavior:

```text
Codex atlas
  -> render visible cells to complete RGB565 frames
  -> prove the shortest conservative period
  -> expand the proven period or full visible sequence to 8 frames
  -> encode unchanged CCPT v1 bundle
  -> existing authenticated pet sync
  -> existing firmware 0...7 renderer
```

The CCPT schema, pet partition format, firmware decoder, LCD submission path,
pet state mapping, animation rate, microphone policy, BLE HID, and Wi-Fi
control path remain unchanged. Release-version constants, manifests, plists,
and their consistency tests change only from `1.1.5` to `1.1.6`.

## Tests

The ProductPet executable test must prove:

- `A,B,C,D,A,B` becomes `A,B,C,D,A,B,C,D`;
- six distinct frames retain the fallback `A,B,C,D,E,F,A,B`;
- a sequence with only one repeated tail frame is not normalized;
- a one-pixel difference prevents a false period match;
- fully transparent columns remain excluded;
- every state still contains exactly eight CCPT v1 frames.

The RED run must fail on the current transcoder for the first behavior before
production code changes. The focused ProductPet test then passes, followed by
the full Companion, host, Python, sanitizer, packaging, and ESP-IDF release
gates.

## Version and Deployment

All current release-version surfaces advance together from `1.1.5` to
`1.1.6`. Historical validation records remain unchanged.

Deployment uses the existing signed Mac installer and an application-only
firmware flash at `0x20000`. It must not overwrite NVS, Wi-Fi, PIN, profiles,
pet storage, or other persisted configuration. Restarting the installed
Companion forces a fresh transcode; the corrected bundle has a new content
digest and is uploaded through the existing authenticated sync path.

## Acceptance Criteria

- The current Rocky WAITING/WORKING bundle contains the repeating sequence
  `A,B,C,D,A,B,C,D`, verified from encoded frame payloads.
- Non-periodic and near-periodic fixtures retain their original visible-frame
  order and fallback behavior.
- Device reports version `1.1.6`, BLE and Wi-Fi remain healthy, and Mac
  Companion returns online after installation and app-only flash.
- The device pet digest matches the locally generated corrected bundle.
- Physical observation confirms that the connected WAITING/WORKING animation
  no longer spends the second half repeatedly showing only its first two
  frames.

## Boundaries

- No approximate or perceptual frame matching is allowed.
- No special case for Rocky or any named pet is allowed.
- No new interpolation, synthesized frame, ping-pong animation, or frame hold
  is introduced.
- No CCPT v2 or variable-length firmware cycle is introduced.
- The already non-reproducing microphone reboot-recovery issue is out of
  scope.
