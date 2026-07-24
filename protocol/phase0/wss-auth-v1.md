# WSS Auth v1

## Scope

Defines TLS exporter tuple binding and fixed-width ECDSA signature bytes for fixture parity.

## TLS exporter

- Exporter label: `EXPORTER-Cardputer-Codex-Companion-v1`
- Exporter context: empty (`use_context = 0` in mbedTLS / zero-length in caller API)
- Exporter length: `32` bytes

`exporter_context` must be serialized as `lp16(context)` and be an empty byte sequence.

## Canonical auth message

```
message =
  lp16(exporter) ||
  lp16(companion_instance_id) ||
  lp16(device_id) ||
  lp16(protocol_version) ||
  lp16(challenge)
```

- `device_id` and `companion_instance_id` are UTF-8 strings.
- `challenge` is fixed at 32 bytes.
- No protocol magic/version prefix is included in this canonical message.

Role is constrained to `"device"` for fixture generation.

## Signature

- Algorithm: ECDSA over SHA-256, P-256 signing key.
- Signature must be fixed-width raw `r || s` (`64` bytes / `128` hex chars).
- Fixture stores:
  - `signature_hex`
  - `signature_format = "raw-rs256-64bytes"`
  - `peer_spki_hex` and `peer_spki_sha256_hex`

Both Swift CryptoKit and mbedTLS fixture consumers must canonicalize using this exact encoding.
