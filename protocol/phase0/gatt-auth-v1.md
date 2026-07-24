# GATT Auth v1

## Scope

Defines canonical GATT auth input bytes and auth tag derivation for fixture parity.

## Constants

- `COUNTER_WINDOW = 32`
- `HMAC_LABEL = b"cardputer-codex/gatt-auth/v1"`

## Canonical frame

```
frame =
  0x01 ||
  flags(1) ||
  connection_id(16 bytes, companion generated) ||
  operation_id(16 bytes) ||
  counter(uint64be) ||
  fragment_index(uint16be) ||
  fragment_count(uint16be) ||
  full_message_len(uint32be) ||
  full_message_sha256(32 bytes) ||
  fragment_len(uint16be) ||
  fragment(variable, <=412 bytes)
```

- `connection_id` must be exactly 16 bytes, companion-generated per BLE connection.
- `operation_id` must be exactly 16 bytes.
- `counter` is 64-bit big-endian.
- `full_message_utf8` is variable-length UTF-8 up to `1024` bytes.
- `full_message_sha256` is SHA-256 over `full_message_utf8`.
- `fragment` is variable-length up to `412` bytes.

## Auth tag

- `tag = HMAC-SHA256(key=gatt_auth_key, msg=b"cardputer-codex/gatt-auth/v1" || frame)`
- Keep only first `16` bytes in fixture as `tag_hex` (`32` hex chars).
- Fixture should include `gatt_auth_hex` and `gatt_hmac_label` for explicit replay.

## Counter replay policy

- Maintain `counter_window = 32` per connection/operation pair.
- Reject when `candidate_counter <= highest_counter - counter_window`.
- Reject duplicate counters already in accepted set.
- Reject counters above `highest_counter + counter_window`.
- Allow `candidate_counter == 0` only when `highest_counter == -1`.
