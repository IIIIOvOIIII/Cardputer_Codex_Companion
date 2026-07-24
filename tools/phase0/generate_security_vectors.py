from __future__ import annotations

import argparse
import hashlib
import hmac
import json
from pathlib import Path
from typing import Any, Callable, Dict, Mapping

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat


PAIRING_MAGIC = b"CCP-PAIR"
PROTOCOL_VERSION_BYTES = b"\x00\x01"
PAIRING_VERSION = "1"

GATT_COUNTER_WINDOW = 32

HKDF_LABEL_PAIRING_ROOT = b"cardputer-codex/pair-root/v1"
HKDF_LABEL_GATT_AUTH = b"cardputer-codex/gatt-auth/v1"
HKDF_LABEL_SAS = b"cardputer-codex/sas/v1"

WSS_EXPORTER_LABEL = "EXPORTER-Cardputer-Codex-Companion-v1"
WSS_SIGNED_EXPORTER_LENGTH = 32
WSS_CHALLENGE_LENGTH = 32
WSS_ROLE_DEVICE = "device"

SAS_REJECTION_LIMIT = 4_294_000_000
SAS_ATTEMPT_MIN = 0
SAS_ATTEMPT_MAX = 255

CURVE = ec.SECP256R1()


class FixtureValidationError(ValueError):
    """Raised for invalid fixed-width/vector inputs."""


def _to_bytes(value_hex: str) -> bytes:
    return bytes.fromhex(value_hex)


def lp16(value: bytes) -> bytes:
    if len(value) > 0xFFFF:
        raise ValueError("length exceeds uint16")
    return len(value).to_bytes(2, "big") + value


def _require_exact_bytes(value_hex: str, length: int, name: str) -> bytes:
    value = _to_bytes(value_hex)
    if len(value) != length:
        raise FixtureValidationError(f"{name} must be exactly {length} bytes")
    return value


def _private_key_from_scalar(hex_scalar: str) -> ec.EllipticCurvePrivateKey:
    scalar = int(hex_scalar, 16)
    return ec.derive_private_key(scalar, CURVE)


def _public_sec1_hex(private_key: ec.EllipticCurvePrivateKey) -> str:
    return private_key.public_key().public_bytes(
        encoding=Encoding.X962,
        format=PublicFormat.UncompressedPoint,
    ).hex()


def _public_spki_hex(private_key: ec.EllipticCurvePrivateKey) -> str:
    return private_key.public_key().public_bytes(
        encoding=Encoding.DER,
        format=PublicFormat.SubjectPublicKeyInfo,
    ).hex()


def _public_digest_hex(private_key: ec.EllipticCurvePrivateKey) -> str:
    return hashlib.sha256(bytes.fromhex(_public_spki_hex(private_key))).hexdigest()


def _hkdf_sha256(
    *,
    shared_secret: bytes,
    salt: bytes,
    info: bytes,
    length: int,
) -> bytes:
    return HKDF(
        algorithm=hashes.SHA256(),
        length=length,
        salt=salt,
        info=info,
    ).derive(shared_secret)


def _default_sas_sample(shared_secret: bytes, salt: bytes, info: bytes) -> bytes:
    return _hkdf_sha256(
        shared_secret=shared_secret,
        salt=salt,
        info=info,
        length=4,
    )


def pairing_transcript(
    *,
    device_id: str,
    companion_instance_id: str,
    protocol_version: str,
    device_identity_sec1: str,
    companion_identity_sec1: str,
    device_ephemeral_sec1: str,
    companion_ephemeral_sec1: str,
    device_nonce_hex: str,
    companion_nonce_hex: str,
) -> bytes:
    device_nonce = _require_exact_bytes(device_nonce_hex, 32, "pairing device nonce")
    companion_nonce = _require_exact_bytes(
        companion_nonce_hex,
        32,
        "pairing companion nonce",
    )

    return b"".join(
        (
            PAIRING_MAGIC,
            PROTOCOL_VERSION_BYTES,
            lp16(device_id.encode("utf-8")),
            lp16(companion_instance_id.encode("utf-8")),
            lp16(protocol_version.encode("utf-8")),
            bytes.fromhex(device_identity_sec1),
            bytes.fromhex(companion_identity_sec1),
            bytes.fromhex(device_ephemeral_sec1),
            bytes.fromhex(companion_ephemeral_sec1),
            device_nonce,
            companion_nonce,
        )
    )


def _derive_pairing_secret_keys(
    *,
    device_ephemeral_private_key: ec.EllipticCurvePrivateKey,
    companion_ephemeral_public_key: ec.EllipticCurvePublicKey,
    transcript_sha256: bytes,
) -> tuple[str, str]:
    shared_secret = device_ephemeral_private_key.exchange(
        ec.ECDH(),
        companion_ephemeral_public_key,
    )

    pairing_root = _hkdf_sha256(
        shared_secret=shared_secret,
        salt=transcript_sha256,
        info=HKDF_LABEL_PAIRING_ROOT,
        length=32,
    )

    gatt_auth_key = _hkdf_sha256(
        shared_secret=shared_secret,
        salt=transcript_sha256,
        info=HKDF_LABEL_GATT_AUTH,
        length=32,
    )

    return pairing_root.hex(), gatt_auth_key.hex()


def derive_sas(
    *,
    shared_secret: bytes,
    salt: bytes,
    attempt_min: int = SAS_ATTEMPT_MIN,
    attempt_max: int = SAS_ATTEMPT_MAX,
    sample: Callable[[bytes, bytes, bytes], bytes] = _default_sas_sample,
) -> tuple[str, int]:
    for attempt in range(attempt_min, attempt_max + 1):
        attempt_bytes = attempt.to_bytes(4, "big", signed=False)
        attempt_material = HKDF_LABEL_SAS + attempt_bytes
        raw = sample(shared_secret, salt, attempt_material)

        if len(raw) != 4:
            raise ValueError("SAS sample must be exactly 4 bytes")

        word = int.from_bytes(raw, "big")
        if word < SAS_REJECTION_LIMIT:
            return f"{word % 1_000_000:06d}", attempt

    raise ValueError(
        "SAS attempts exhausted (0..255), no candidate below 4_294_000_000 was found"
    )


def gatt_frame(
    *,
    connection_id_hex: str,
    operation_id_hex: str,
    counter: int,
    fragment_index: int,
    fragment_count: int,
    full_message_utf8_hex: str,
    fragment_hex: str,
    flags: int = 0,
) -> bytes:
    if len(connection_id_hex) != 32:
        raise FixtureValidationError("connection_id must be exactly 16 bytes")
    if len(operation_id_hex) != 32:
        raise FixtureValidationError("operation_id must be exactly 16 bytes")

    connection_id = bytes.fromhex(connection_id_hex)
    operation_id = bytes.fromhex(operation_id_hex)
    full_message = bytes.fromhex(full_message_utf8_hex)
    fragment = bytes.fromhex(fragment_hex)

    if counter < 0 or counter > 0xFFFF_FFFF_FFFF_FFFF:
        raise FixtureValidationError("counter must fit uint64")
    if fragment_index < 0:
        raise FixtureValidationError("fragment_index must be non-negative")
    if fragment_count < 1 or fragment_count > 64:
        raise FixtureValidationError("fragment_count must be 1..64")
    if fragment_index >= fragment_count:
        raise FixtureValidationError("fragment_index must be < fragment_count")

    if len(full_message) > 1024:
        raise FixtureValidationError("full UTF-8 message must be <= 1024 bytes")
    if len(fragment) > 412:
        raise FixtureValidationError("fragment must be <= 412 bytes")
    frame = b"".join(
        (
            b"\x01",
            flags.to_bytes(1, "big"),
            connection_id,
            operation_id,
            counter.to_bytes(8, "big", signed=False),
            fragment_index.to_bytes(2, "big", signed=False),
            fragment_count.to_bytes(2, "big", signed=False),
            len(full_message).to_bytes(4, "big", signed=False),
            hashlib.sha256(full_message).digest(),
            len(fragment).to_bytes(2, "big", signed=False),
            fragment,
        )
    )

    frame_length = len(frame)
    total_length = frame_length + 16
    if frame_length < 84:
        raise FixtureValidationError("GATT frame body must be at least 84 bytes")
    if total_length < 100:
        raise FixtureValidationError("GATT framed packet must be at least 100 bytes")
    if total_length > 512:
        raise FixtureValidationError("GATT framed packet must be at most 512 bytes")

    return frame


def gatt_tag(
    *,
    gatt_auth_key_hex: str,
    canonical_frame: bytes,
) -> str:
    key = bytes.fromhex(gatt_auth_key_hex)
    message = b"cardputer-codex/gatt-auth/v1" + canonical_frame
    return hmac.new(key, message, hashlib.sha256).digest()[:16].hex()


def is_counter_acceptable(
    candidate_counter: int,
    *,
    highest_counter: int,
    accepted_counters: set[int],
    connection_id: str,
    expected_connection_id: str,
    window: int = GATT_COUNTER_WINDOW,
) -> bool:
    if connection_id != expected_connection_id:
        return False

    if candidate_counter < 0 or candidate_counter > 0xFFFF_FFFF_FFFF_FFFF:
        return False

    if highest_counter < 0:
        return candidate_counter == 0

    if candidate_counter <= highest_counter - window:
        return False

    if candidate_counter <= highest_counter and candidate_counter in accepted_counters:
        return False

    if candidate_counter > highest_counter + window:
        return False

    if candidate_counter == highest_counter:
        return candidate_counter not in accepted_counters

    return True


def wss_canonical_message(
    *,
    companion_instance_id: str,
    device_id: str,
    protocol_version: str,
    tls_exporter: bytes,
    challenge: bytes,
) -> bytes:
    return b"".join(
        (
            lp16(tls_exporter),
            lp16(companion_instance_id.encode("utf-8")),
            lp16(device_id.encode("utf-8")),
            lp16(protocol_version.encode("utf-8")),
            lp16(challenge),
        )
    )


def _raw_ecdsa_signature_from_private(
    private_key: ec.EllipticCurvePrivateKey,
    message: bytes,
) -> str:
    der_signature = private_key.sign(
        message,
        ec.ECDSA(
            hashes.SHA256(),
            deterministic_signing=True,
        ),
    )
    r, s = utils.decode_dss_signature(der_signature)
    return f"{r.to_bytes(32, 'big').hex()}{s.to_bytes(32, 'big').hex()}"


def _raw_ecdsa_signature_to_der(raw_hex: str) -> bytes:
    raw = bytes.fromhex(raw_hex)
    if len(raw) != 64:
        raise ValueError("WSS signature must be raw r||s exactly 64 bytes")
    r = int.from_bytes(raw[:32], "big")
    s = int.from_bytes(raw[32:], "big")
    return utils.encode_dss_signature(r, s)


def verify_wss_signature(
    *,
    public_key: ec.EllipticCurvePublicKey,
    message: bytes,
    signature_hex: str,
) -> bool:
    try:
        public_key.verify(
            _raw_ecdsa_signature_to_der(signature_hex),
            message,
            ec.ECDSA(hashes.SHA256()),
        )
    except Exception:
        return False
    return True


def build_pairing_fixture(inputs: Mapping[str, Any]) -> Dict[str, str]:
    device_identity_private = _private_key_from_scalar(
        inputs["device_identity_private_scalar_hex"],
    )
    companion_identity_private = _private_key_from_scalar(
        inputs["companion_identity_private_scalar_hex"],
    )
    device_ephemeral_private = _private_key_from_scalar(
        inputs["device_ephemeral_private_scalar_hex"],
    )
    companion_ephemeral_private = _private_key_from_scalar(
        inputs["companion_ephemeral_private_scalar_hex"],
    )

    device_identity_sec1 = _public_sec1_hex(device_identity_private)
    companion_identity_sec1 = _public_sec1_hex(companion_identity_private)
    device_ephemeral_sec1 = _public_sec1_hex(device_ephemeral_private)
    companion_ephemeral_sec1 = _public_sec1_hex(companion_ephemeral_private)

    transcript = pairing_transcript(
        device_id=inputs["device_id"],
        companion_instance_id=inputs["companion_instance_id"],
        protocol_version=inputs["protocol_version"],
        device_identity_sec1=device_identity_sec1,
        companion_identity_sec1=companion_identity_sec1,
        device_ephemeral_sec1=device_ephemeral_sec1,
        companion_ephemeral_sec1=companion_ephemeral_sec1,
        device_nonce_hex=inputs["device_nonce_hex"],
        companion_nonce_hex=inputs["companion_nonce_hex"],
    )

    transcript_sha256 = hashlib.sha256(transcript).digest()
    pairing_root_hex, gatt_auth_hex = _derive_pairing_secret_keys(
        device_ephemeral_private_key=device_ephemeral_private,
        companion_ephemeral_public_key=companion_ephemeral_private.public_key(),
        transcript_sha256=transcript_sha256,
    )

    shared_secret = device_ephemeral_private.exchange(
        ec.ECDH(),
        companion_ephemeral_private.public_key(),
    )
    sas, sas_attempt = derive_sas(
        shared_secret=shared_secret,
        salt=transcript_sha256,
    )

    return {
        "test_only": True,
        "warnings": [
            "These are deterministic test-only fixtures and must not be used as production secrets."
        ],
        "pairing_version": PAIRING_VERSION,
        "device_id": inputs["device_id"],
        "companion_instance_id": inputs["companion_instance_id"],
        "protocol_version": inputs["protocol_version"],
        "role": inputs["role"],
        "device_identity_private_scalar_hex": inputs["device_identity_private_scalar_hex"],
        "companion_identity_private_scalar_hex": inputs[
            "companion_identity_private_scalar_hex"
        ],
        "device_ephemeral_private_scalar_hex": inputs["device_ephemeral_private_scalar_hex"],
        "companion_ephemeral_private_scalar_hex": inputs[
            "companion_ephemeral_private_scalar_hex"
        ],
        "device_identity_sec1_hex": device_identity_sec1,
        "companion_identity_sec1_hex": companion_identity_sec1,
        "device_ephemeral_sec1_hex": device_ephemeral_sec1,
        "companion_ephemeral_sec1_hex": companion_ephemeral_sec1,
        "device_nonce_hex": inputs["device_nonce_hex"],
        "companion_nonce_hex": inputs["companion_nonce_hex"],
        "transcript_hex": transcript.hex(),
        "pairing_root_hex": pairing_root_hex,
        "gatt_auth_hex": gatt_auth_hex,
        "sas": sas,
        "sas_attempt": sas_attempt,
        "transcript_sha256": hashlib.sha256(transcript).hexdigest(),
        "pairing_hkdf_labels": {
            "pairing_root": HKDF_LABEL_PAIRING_ROOT.decode("ascii"),
            "gatt_auth": HKDF_LABEL_GATT_AUTH.decode("ascii"),
            "sas_prefix": HKDF_LABEL_SAS.decode("ascii"),
        },
    }


def build_gatt_fixture(
    inputs: Mapping[str, Any],
    *,
    gatt_auth_hex: str,
) -> Dict[str, Any]:
    full_message_utf8_hex = inputs["gatt_full_message_utf8_hex"]
    fragment_hex = inputs["gatt_fragment_hex"]
    fragment_index = inputs["gatt_fragment_index"]
    fragment_count = inputs["gatt_fragment_count"]
    counter = inputs["gatt_counter"]

    canonical_frame = gatt_frame(
        connection_id_hex=inputs["gatt_connection_id_hex"],
        operation_id_hex=inputs["gatt_operation_id_hex"],
        counter=counter,
        fragment_index=fragment_index,
        fragment_count=fragment_count,
        full_message_utf8_hex=full_message_utf8_hex,
        fragment_hex=fragment_hex,
        flags=inputs["gatt_flags"],
    )

    return {
        "test_only": True,
        "warnings": [
            "These are deterministic test-only fixtures and must not be used as production secrets."
        ],
        "gatt_hmac_label": HKDF_LABEL_GATT_AUTH.decode("ascii"),
        "gatt_flags": inputs["gatt_flags"],
        "gatt_connection_id_hex": inputs["gatt_connection_id_hex"],
        "gatt_operation_id_hex": inputs["gatt_operation_id_hex"],
        "gatt_auth_hex": gatt_auth_hex,
        "gatt_counter": counter,
        "gatt_fragment_index": fragment_index,
        "gatt_fragment_count": fragment_count,
        "gatt_full_message_utf8_hex": full_message_utf8_hex,
        "gatt_full_message_utf8_length": len(_to_bytes(full_message_utf8_hex)),
        "gatt_full_message_sha256_hex": hashlib.sha256(
            _to_bytes(full_message_utf8_hex),
        ).hexdigest(),
        "gatt_fragment_hex": fragment_hex,
        "gatt_fragment_length": len(_to_bytes(fragment_hex)),
        "canonical_frame_hex": canonical_frame.hex(),
        "tag_hex": hmac.new(
            bytes.fromhex(gatt_auth_hex),
            b"cardputer-codex/gatt-auth/v1" + canonical_frame,
            hashlib.sha256,
        ).digest()[:16].hex(),
        "counter_window_size": GATT_COUNTER_WINDOW,
    }


def build_wss_fixture(
    inputs: Mapping[str, Any],
    *,
    signer_private_key: ec.EllipticCurvePrivateKey,
    companion_identity_private_key: ec.EllipticCurvePrivateKey,
    role: str,
) -> Dict[str, Any]:
    if role != WSS_ROLE_DEVICE:
        raise FixtureValidationError("Unsupported WSS signer role")

    tls_exporter = _require_exact_bytes(
        inputs["tls_exporter_hex"],
        WSS_SIGNED_EXPORTER_LENGTH,
        "TLS exporter",
    )

    tls_exporter_context = inputs["tls_exporter_context_hex"]
    if tls_exporter_context != "":
        raise FixtureValidationError("TLS exporter context must be empty")

    challenge = _require_exact_bytes(
        inputs["wss_challenge_hex"],
        WSS_CHALLENGE_LENGTH,
        "WSS challenge",
    )

    expected_peer_spki_sha256 = inputs.get("peer_spki_sha256_hex")
    actual_peer_spki_sha256 = _public_digest_hex(companion_identity_private_key)
    if actual_peer_spki_sha256 != expected_peer_spki_sha256:
        raise FixtureValidationError("Companion SPKI pin mismatch")

    message = wss_canonical_message(
        companion_instance_id=inputs["companion_instance_id"],
        device_id=inputs["device_id"],
        protocol_version=inputs["protocol_version"],
        tls_exporter=tls_exporter,
        challenge=challenge,
    )

    signature_hex = _raw_ecdsa_signature_from_private(
        signer_private_key,
        message,
    )

    spki_hex = _public_spki_hex(companion_identity_private_key)

    return {
        "test_only": True,
        "warnings": [
            "These are deterministic test-only fixtures and must not be used as production secrets."
        ],
        "role": role,
        "protocol_version": inputs["protocol_version"],
        "device_id": inputs["device_id"],
        "companion_instance_id": inputs["companion_instance_id"],
        "exporter_label": WSS_EXPORTER_LABEL,
        "exporter_context_hex": tls_exporter_context,
        "tls_exporter_hex": inputs["tls_exporter_hex"],
        "wss_challenge_hex": inputs["wss_challenge_hex"],
        "signer_private_scalar_hex": inputs["device_identity_private_scalar_hex"],
        "signer_public_sec1_hex": _public_sec1_hex(signer_private_key),
        "signature_hex": signature_hex,
        "signature_format": "raw-rs256-64bytes",
        "peer_spki_hex": spki_hex,
        "peer_spki_sha256_hex": actual_peer_spki_sha256,
        "canonical_message_hex": message.hex(),
        "canonical_message_sha256": hashlib.sha256(message).hexdigest(),
    }


def _build_all_fixtures(inputs: Mapping[str, Any]) -> Dict[str, Dict[str, Any]]:
    pairing = build_pairing_fixture(inputs)

    gatt = build_gatt_fixture(
        inputs,
        gatt_auth_hex=pairing["gatt_auth_hex"],
    )

    device_identity_private = _private_key_from_scalar(
        inputs["device_identity_private_scalar_hex"],
    )
    companion_identity_private = _private_key_from_scalar(
        inputs["companion_identity_private_scalar_hex"],
    )

    wss = build_wss_fixture(
        inputs,
        signer_private_key=device_identity_private,
        companion_identity_private_key=companion_identity_private,
        role=inputs["role"],
    )

    return {
        "pairing-v1": pairing,
        "gatt-auth-v1": {
            **gatt,
            "pairing_root_hex": pairing["pairing_root_hex"],
        },
        "wss-auth-v1": wss,
    }


def _serialize_fixture(path: Path, payload: Dict[str, Any]) -> None:
    path.write_text(
        json.dumps(payload, ensure_ascii=False, sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
    )


def _write_fixtures(output_dir: Path, fixtures: Dict[str, Dict[str, Any]]) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    _serialize_fixture(output_dir / "pairing-v1.json", fixtures["pairing-v1"])
    _serialize_fixture(output_dir / "gatt-auth-v1.json", fixtures["gatt-auth-v1"])
    _serialize_fixture(output_dir / "wss-auth-v1.json", fixtures["wss-auth-v1"])


def _emit_transcript(path: Path, transcript_hex: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes.fromhex(transcript_hex))


def _default_inputs() -> dict[str, Any]:
    companion_identity_private_key = _private_key_from_scalar(
        "2222222222222222222222222222222222222222222222222222222222222222",
    )

    return {
        "role": WSS_ROLE_DEVICE,
        "device_id": "2e1d4f3c-1a82-41a2-bff9-7d0ea3b58f11",
        "companion_instance_id": "f5a7e11b-3fd2-4b4c-88f0-2d7a8f9f6a73",
        "protocol_version": "1.0",
        "device_identity_private_scalar_hex": (
            "1111111111111111111111111111111111111111111111111111111111111111"
        ),
        "companion_identity_private_scalar_hex": (
            "2222222222222222222222222222222222222222222222222222222222222222"
        ),
        "device_ephemeral_private_scalar_hex": (
            "3333333333333333333333333333333333333333333333333333333333333333"
        ),
        "companion_ephemeral_private_scalar_hex": (
            "4444444444444444444444444444444444444444444444444444444444444444"
        ),
        "device_nonce_hex": "00" * 32,
        "companion_nonce_hex": "ff" * 32,
        "gatt_connection_id_hex": "00112233445566778899aabbccddeeff",
        "gatt_operation_id_hex": "aabbccddeeff00112233445566778899",
        "gatt_flags": 0x00,
        "gatt_counter": 0,
        "gatt_fragment_index": 0,
        "gatt_fragment_count": 1,
        "gatt_full_message_utf8_hex": "48616c6c6f20706f7274666f726365",
        "gatt_fragment_hex": "48656c6c6f",
        "tls_exporter_hex": "0123456789abcdef" * 4,
        "tls_exporter_context_hex": "",
        "peer_spki_sha256_hex": _public_digest_hex(companion_identity_private_key),
        "wss_challenge_hex": (
            "0123456789abcdef" * 2
            + "0123456789abcdef" * 2
        ),
    }


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate deterministic phase0 vectors")
    parser.add_argument(
        "--write",
        type=Path,
        default=Path("protocol/phase0/fixtures"),
        help="Output directory for generated fixture json",
    )
    parser.add_argument(
        "--emit-transcript",
        type=Path,
        default=None,
        help="Optional path to write pairing transcript bytes",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    inputs = _default_inputs()

    companion_identity_private_key = _private_key_from_scalar(
        inputs["companion_identity_private_scalar_hex"],
    )
    inputs["peer_spki_sha256_hex"] = _public_digest_hex(companion_identity_private_key)

    fixtures = _build_all_fixtures(inputs)
    _write_fixtures(args.write, fixtures)

    if args.emit_transcript is not None:
        _emit_transcript(args.emit_transcript, fixtures["pairing-v1"]["transcript_hex"])

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
