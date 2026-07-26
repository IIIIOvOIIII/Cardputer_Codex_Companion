# Cardputer Codex Audio Protocol v1

Audio v1 carries independent 10 ms IMA-ADPCM microphone blocks over the
existing bonded and encrypted Companion GATT service. It never uses Wi-Fi and
contains no command that can start microphone capture. A local, valid G0 click
is the only capture-start authority.

## Characteristics

Service UUID: `7A100001-2C4D-4F20-9F20-434F44455831`

- Audio Data Notify: `7A100005-2C4D-4F20-9F20-434F44455831`
- Audio Control Write: `7A100006-2C4D-4F20-9F20-434F44455831`
- Audio Status Notify: `7A100007-2C4D-4F20-9F20-434F44455831`

All characteristics require an encrypted, bonded connection. Control writes
also require the current Companion binding.

## Audio Data

All multibyte integers are little-endian.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 1 | Protocol version, always `1` |
| 1 | 1 | Flags: bit 0 start, bit 1 discontinuity, bit 2 degraded rate |
| 2 | 2 | Wrapping sequence number |
| 4 | 1 | Rate: `1` = 24 kHz, `2` = 16 kHz |
| 5 | 1 | Duration, always 10 ms |
| 6 | 2 | ADPCM payload length |
| 8 | variable | Independent IMA-ADPCM block |

The only valid combinations are:

- 24 kHz: 240 samples, 124-byte payload, 132-byte packet.
- 16 kHz: 160 samples, 84-byte payload, 92-byte packet.

Unknown flags, versions, rates, durations, and mismatched lengths are rejected.
Sequence `65535` is followed by `0`.

Each ADPCM payload begins with a little-endian signed 16-bit predictor, a
one-byte step index, and one reserved zero byte. Remaining samples are encoded
with the standard 89-entry IMA step table and 16-entry index-adjustment table,
low nibble first and then high nibble. Predictor and step index are clamped
after every nibble. Every packet starts a new block, so packet loss cannot
corrupt later frames.

## Audio Control

Control messages start with protocol version `1`, followed by one opcode.
`set_preferred_rate` has one additional rate byte; all other messages are
exactly two bytes.

| Opcode | Name | Direction | Argument |
| ---: | --- | --- | --- |
| 1 | `hello` | Companion to device | none |
| 2 | `sink_ready` | Companion to device | none |
| 3 | `sink_not_ready` | Companion to device | none |
| 4 | `set_preferred_rate` | Companion to device | rate code |
| 5 | `reset_statistics` | Companion to device | none |

Opcode 6 and every other unknown opcode are invalid. In particular, there is
no remote-start opcode.

## Audio Status

Audio Status is a read-only device notification surface for microphone state,
active sample rate, saturating source-overrun/transport-drop/discontinuity
counters, and the last non-sensitive error code. Status cannot control capture
and never contains recorded samples.
