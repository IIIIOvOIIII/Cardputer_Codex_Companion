# Cardputer Runtime Memory Tuning Design

## Context

The microphone feasibility smoke test is stable and records no reset, panic, or
allocation failure, but the real ESP32-S3 reports approximately 38 KiB steady
internal heap, a 21 KiB largest block, and 25–31 KiB during HTTPS handshakes.
Those values fail the already approved 64/32/40 KiB resource gates.

The gates originated in the Phase 0 design. Its hardware evidence explicitly
records `capture_complete=false`, so the thresholds were never proven against
the full product runtime. The current microphone branch consumes only about
7 KiB more static DIRAM than the non-microphone product branch. The dominant
locally controllable allocations are the 19,968-byte full pet frame and task
stacks with measured unused capacity.

## Decision

Preserve the existing resource gates and recover memory without changing
product behavior:

1. Decode and draw pet frames one row at a time. The display path owns one
   fixed 96-pixel RGB565 row buffer and immediately pushes each validated row.
   No full 96-by-104 RGB565 frame remains in static RAM.
2. Keep the current full-frame decoder for host fixtures and compatibility.
   Add a row-oriented decoder interface used by `PetStore` and the display.
   Both raw and RLE encodings must produce exactly the same RGB565 pixels and
   reject the same malformed records.
3. Right-size only the scanner, macro, and audio stacks using measured target
   high-water values:
   - scanner: 8,192 to 4,096 bytes;
   - macro: 6,144 to 2,048 bytes;
   - audio: 3,072 to 2,048 bytes.
4. Keep UI and HID stacks unchanged. Their measured remaining stack is too
   close to the acceptance floor to justify reduction.

The expected static DIRAM recovery is 29,184 bytes: 19,968 bytes from the pet
frame and 9,216 bytes from stack right-sizing.

## Runtime and Failure Semantics

The row callback runs synchronously in the existing UI task. A row is pushed
only after that row has been completely decoded and validated. A read error,
invalid RLE count, row width mismatch, or callback failure aborts the frame and
uses the existing placeholder behavior. No heap allocation, task creation,
network call, or audio operation is introduced in the row path.

The wire format, pet bundle validation, active bundle transaction model,
animation cadence, coordinates, colors, and display byte order do not change.
The microphone capture task and HID hot path remain independent of display
work.

## Verification

Implementation follows RED-to-GREEN tests:

- raw and RLE row decoding matches the existing full-frame decoder pixel for
  pixel;
- malformed rows and callback failure abort deterministically;
- source reads remain row-bounded;
- a target size check proves the full-frame static buffer is gone;
- all existing Python, host normal/sanitizer, Swift, ESP-IDF, packaging, and
  secret-exclusion gates pass.

After app-only flashing, serial measurements must still satisfy:

- steady internal heap at least 64 KiB;
- largest internal block at least 32 KiB;
- HTTPS transient internal heap at least 40 KiB;
- zero allocation failures;
- every reported task retains at least 20 percent of configured stack or
  1,024 bytes, whichever is greater.

Only after these checks pass may the ten-minute 24 kHz audio/HID feasibility
gate proceed.

## Alternatives Rejected

- Stack reduction alone recovers only about 9 KiB and cannot meet the original
  heap gates.
- Lowering the thresholds contradicts the approved microphone implementation
  plan and would hide a real lack of headroom.
- Removing Web, pet animation, or other product features changes the accepted
  product scope and is not a bounded feasibility tuning.
