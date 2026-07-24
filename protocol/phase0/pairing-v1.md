# Pairing v1

## Scope

Defines the canonical pairing transcript bytes, HKDF labels, and SAS derivation used by Phase 0 fixtures.

## Constants

- `PAIRING_MAGIC = b"CCP-PAIR"`
- `PROTOCOL_VERSION_BYTES = b"\x00\x01"`

## Canonical transcript

Field order:

1. `MAGIC`
2. `VERSION`
3. `lp16(device_id)`
4. `lp16(companion_instance_id)`
5. `lp16(protocol_version)`
6. `device_identity_sec1` (`04 || X || Y`, 65 bytes)
7. `companion_identity_sec1` (`04 || X || Y`, 65 bytes)
8. `device_ephemeral_sec1` (`04 || X || Y`, 65 bytes)
9. `companion_ephemeral_sec1` (`04 || X || Y`, 65 bytes)
10. `device_nonce` (32 bytes)
11. `companion_nonce` (32 bytes)

Identity and ephemeral public keys are SEC1 uncompressed (`X9.62`) on `secp256r1`, 65 bytes when encoded.

## HKDF derivations

### Pairing root

- `pairing_root = HKDF-SHA256(ikm=ECDH(device_ephemeral_private, companion_ephemeral_public), salt=SHA256(transcript), info="cardputer-codex/pair-root/v1", len=32)`

### GATT auth key

- `gatt_auth = HKDF-SHA256(ikm=ECDH(device_ephemeral_private, companion_ephemeral_public), salt=SHA256(transcript), info="cardputer-codex/gatt-auth/v1", len=32)`

`pairing_root` and `gatt_auth` must be different test vectors.

## SAS

SAS uses rejection sampling over attempts `0..255` using the transcript hash as HKDF salt.

1. For each `attempt` in `0..255`:
   - `attempt_be = uint32_be(attempt)`
   - `candidate = HKDF-SHA256(ikm=ECDH(device_ephemeral_private, companion_ephemeral_public), salt=SHA256(transcript), info=b"cardputer-codex/sas/v1" || attempt_be, len=4)`
   - Interpret `candidate` as unsigned big-endian.
2. Accept only if `< 4_294_000_000`.
3. SAS string is `word % 1_000_000` formatted to 6 digits.
4. If no attempt accepted, return an exhaustion error.

Use exactly 4-byte samples and exactly 256 attempts, no host entropy.

## Post-SAS binding gate

Pairing is accepted only when both SAS confirmations are complete and the
downstream binding policy is also satisfied:

- The same 32-byte fresh WSS challenge arrives within `60` seconds of the second
  SAS acceptance.
- The challenge must arrive over authenticated WSS.
- Bonded GATT transport must already be encrypted.
- The peer identity used by the binding check must match the pinned
  `peer_spki_sha256_hex`.

If any condition is missing or mismatched, bind is rejected. The canonical fixture
encodes this policy in `post_sas_policy`, `post_sas_requirements`, and
`post_sas_binding_state`.
