# Phase 0 Protocol Fixtures

This folder is the canonical protocol contract for Foundation Task 2. The vectors are deterministic and test-only; they must be regenerated only through `tools/phase0/generate_security_vectors.py`.

## Documents

- `pairing-v1.md`: pairing transcript, HKDF derivation, and SAS window rules.
- `gatt-auth-v1.md`: GATT-auth HMAC canonical encoding and anti-replay window.
- `wss-auth-v1.md`: TLS exporter binding and WSS fixed-width ECDSA signature rules.

## Fixtures

- `fixtures/pairing-v1.json`: pairing transcript and root/bonding keys.
- `fixtures/gatt-auth-v1.json`: GATT-auth canonical input and 16-byte auth tag.
- `fixtures/wss-auth-v1.json`: WSS canonical message, 64-byte fixed-width signature, and SPKI pin.

Pairing fixtures also include a deterministic post-SAS binding policy block used by downstream clients:
`post_sas_policy`, `post_sas_requirements`, and `post_sas_binding_state`.

The policy requires: fresh 32-byte challenge, same-challenge replay within 60 seconds,
authenticated WSS path, and bonded encrypted GATT.

## Encoding Rules

1. Use exact UTF-8 encoding.
2. Use big-endian integers.
3. Use explicit length prefixes (`lp16`) for all variable-length byte fields unless fixed width.
4. Never use host-derived randomness.
5. Never emit ASN.1/DER for WSS signatures in fixtures.
6. SHA-256, HKDF-SHA256, HMAC-SHA256, and ECDSA-P256 are required.

Helper:

- `lp16(value)` -> `uint16be(len(value)) || value`, error if length > `0xFFFF`.
