from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

from generate_security_vectors import (
    GATT_COUNTER_WINDOW,
    PAIRING_MAGIC,
    PROTOCOL_VERSION_BYTES,
    SAS_ATTEMPT_MAX,
    SAS_REJECTION_LIMIT,
    _build_all_fixtures,
)

CANONICAL_SOURCE_SHA256 = {
    "pairing-v1.md": "67b4f3368bc049af65591e3621c3aa91dbf244504b789558f190d1d2b84c668f",
    "fixtures/pairing-v1.json": "c0c5f2ecec0667274d7317d45aace9bd9ca78c07802f2a574ac7bad31c574ac5",
    "gatt-auth-v1.md": "0183d541bb45e3c333ad71da405dca8fdc2709fec41609cc4d114d25d818b231",
    "fixtures/gatt-auth-v1.json": "d73fc79a62f883657b68a86f62b595d38aba7355aa4bad75fa422b4c54cbe9c5",
    "wss-auth-v1.md": "3947aa3038833c19ddb0c8ac6b23c540b508c44e808e2b221f52a6cdc296e50b",
    "fixtures/wss-auth-v1.json": "d696006f7cc748c79e79d133a3f6cc9a9c12a40d34537c4797d9e21b0a445ad1",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate canonical phase-0 C++ protocol vector header"
    )
    parser.add_argument(
        "--protocol-root",
        required=True,
        type=Path,
        help="Root directory containing pairing/gatt/wss docs and fixtures",
    )
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="Output path for generated C++ header",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Verify that the existing output exactly matches canonical sources",
    )
    return parser.parse_args()


def require_file(path: Path) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"missing required source file: {path}")


def require_fields(payload: dict[str, Any], required: list[str], path: Path) -> None:
    missing = [name for name in required if name not in payload]
    if missing:
        raise ValueError(
            f"fixture missing required field(s) in {path}: {', '.join(missing)}"
        )


def parse_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON in {path}: {exc}") from exc


def hex_bytes(value: str) -> bytes:
    try:
        return bytes.fromhex(value)
    except ValueError as exc:
        raise ValueError(f"invalid hex field: {value}") from exc


def byte_expr_from_hex(value: str) -> str:
    return ", ".join(f"0x{byte:02x}" for byte in hex_bytes(value))


def byte_array_expr(value: str, expected_len: int) -> str:
    data = hex_bytes(value)
    if len(data) != expected_len:
        raise ValueError(f"expected {expected_len} bytes, got {len(data)}")
    if not data:
        return "{}"
    return "{" + byte_expr_from_hex(value) + "}"


def byte_vector_expr(value: str) -> str:
    if not value:
        return "{}"
    return "{" + byte_expr_from_hex(value) + "}"


def string_expr(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', r'\"')
    return f'"{escaped}"'


def source_sha256(*paths: Path) -> str:
    digest = hashlib.sha256()
    for path in paths:
        digest.update(path.read_bytes())
    return digest.hexdigest()


def verify_canonical_source_hashes(protocol_root: Path) -> None:
    for relative_path, expected_sha256 in CANONICAL_SOURCE_SHA256.items():
        actual_sha256 = source_sha256(protocol_root / relative_path)
        if actual_sha256 != expected_sha256:
            raise ValueError(
                "canonical protocol source hash mismatch: "
                f"{relative_path}: expected {expected_sha256}, got {actual_sha256}"
            )


def build_and_validate_inputs(
    pairing: dict[str, Any],
    gatt: dict[str, Any],
    wss: dict[str, Any],
    pairing_fixture: Path,
    gatt_fixture: Path,
    wss_fixture: Path,
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    require_fields(
        pairing,
        [
            "role",
            "device_id",
            "companion_instance_id",
            "protocol_version",
            "device_identity_private_scalar_hex",
            "companion_identity_private_scalar_hex",
            "device_ephemeral_private_scalar_hex",
            "companion_ephemeral_private_scalar_hex",
            "device_identity_sec1_hex",
            "companion_identity_sec1_hex",
            "device_ephemeral_sec1_hex",
            "companion_ephemeral_sec1_hex",
            "device_nonce_hex",
            "companion_nonce_hex",
            "pairing_root_hex",
            "gatt_auth_hex",
            "transcript_hex",
            "sas",
            "sas_attempt",
            "pairing_hkdf_labels",
            "post_sas_binding_state",
        ],
        pairing_fixture,
    )
    require_fields(
        gatt,
        [
            "gatt_connection_id_hex",
            "gatt_operation_id_hex",
            "gatt_counter",
            "gatt_fragment_index",
            "gatt_fragment_count",
            "gatt_flags",
            "gatt_full_message_utf8_hex",
            "gatt_fragment_hex",
            "gatt_hmac_label",
            "tag_hex",
            "canonical_frame_hex",
        ],
        gatt_fixture,
    )
    require_fields(
        wss,
        [
            "protocol_version",
            "companion_instance_id",
            "device_id",
            "tls_exporter_hex",
            "wss_challenge_hex",
            "exporter_label",
            "signature_hex",
            "peer_spki_sha256_hex",
            "peer_spki_hex",
            "canonical_message_hex",
            "exporter_context_hex",
        ],
        wss_fixture,
    )

    inputs = dict(pairing)
    inputs.update(
        {
            "gatt_connection_id_hex": gatt["gatt_connection_id_hex"],
            "gatt_operation_id_hex": gatt["gatt_operation_id_hex"],
            "gatt_flags": gatt["gatt_flags"],
            "gatt_counter": gatt["gatt_counter"],
            "gatt_fragment_index": gatt["gatt_fragment_index"],
            "gatt_fragment_count": gatt["gatt_fragment_count"],
            "gatt_full_message_utf8_hex": gatt["gatt_full_message_utf8_hex"],
            "gatt_fragment_hex": gatt["gatt_fragment_hex"],
            "wss_challenge_hex": wss["wss_challenge_hex"],
            "tls_exporter_hex": wss["tls_exporter_hex"],
            "tls_exporter_context_hex": wss["exporter_context_hex"],
            "peer_spki_sha256_hex": wss["peer_spki_sha256_hex"],
        }
    )

    computed = _build_all_fixtures(inputs)

    if computed["pairing-v1"]["transcript_hex"] != pairing["transcript_hex"]:
        raise ValueError("pairing fixture mismatch: transcript")
    if computed["pairing-v1"]["pairing_root_hex"] != pairing["pairing_root_hex"]:
        raise ValueError("pairing fixture mismatch: pairing_root")
    if computed["pairing-v1"]["gatt_auth_hex"] != pairing["gatt_auth_hex"]:
        raise ValueError("pairing fixture mismatch: gatt_auth")
    if computed["pairing-v1"]["sas"] != pairing["sas"]:
        raise ValueError("pairing fixture mismatch: sas")
    if computed["pairing-v1"]["sas_attempt"] != pairing["sas_attempt"]:
        raise ValueError("pairing fixture mismatch: sas_attempt")

    if computed["gatt-auth-v1"]["canonical_frame_hex"] != gatt["canonical_frame_hex"]:
        raise ValueError("gatt fixture mismatch: canonical_frame")
    if computed["gatt-auth-v1"]["tag_hex"] != gatt["tag_hex"]:
        raise ValueError("gatt fixture mismatch: tag")
    if computed["gatt-auth-v1"]["gatt_hmac_label"] != gatt["gatt_hmac_label"]:
        raise ValueError("gatt fixture mismatch: gatt_hmac_label")

    if computed["wss-auth-v1"]["canonical_message_hex"] != wss["canonical_message_hex"]:
        raise ValueError("wss fixture mismatch: canonical_message")
    if computed["wss-auth-v1"]["signature_hex"] != wss["signature_hex"]:
        raise ValueError("wss fixture mismatch: signature")
    if computed["wss-auth-v1"]["exporter_label"] != wss["exporter_label"]:
        raise ValueError("wss fixture mismatch: exporter_label")

    return pairing, gatt, wss


def generate_cpp(protocol_root: Path, output: Path, *, check: bool = False) -> None:
    pairing_doc = protocol_root / "pairing-v1.md"
    gatt_doc = protocol_root / "gatt-auth-v1.md"
    wss_doc = protocol_root / "wss-auth-v1.md"

    pairing_fixture = protocol_root / "fixtures" / "pairing-v1.json"
    gatt_fixture = protocol_root / "fixtures" / "gatt-auth-v1.json"
    wss_fixture = protocol_root / "fixtures" / "wss-auth-v1.json"

    for path in [
        pairing_doc,
        gatt_doc,
        wss_doc,
        pairing_fixture,
        gatt_fixture,
        wss_fixture,
    ]:
        require_file(path)
    verify_canonical_source_hashes(protocol_root)

    pairing = parse_json(pairing_fixture)
    gatt = parse_json(gatt_fixture)
    wss = parse_json(wss_fixture)

    pairing, gatt, wss = build_and_validate_inputs(
        pairing,
        gatt,
        wss,
        pairing_fixture,
        gatt_fixture,
        wss_fixture,
    )

    pairing_sha = source_sha256(pairing_doc, pairing_fixture)
    gatt_sha = source_sha256(gatt_doc, gatt_fixture)
    wss_sha = source_sha256(wss_doc, wss_fixture)

    pairing_doc_sha = source_sha256(pairing_doc)
    pairing_fixture_sha = source_sha256(pairing_fixture)
    gatt_doc_sha = source_sha256(gatt_doc)
    gatt_fixture_sha = source_sha256(gatt_fixture)
    wss_doc_sha = source_sha256(wss_doc)
    wss_fixture_sha = source_sha256(wss_fixture)

    lines: list[str] = []
    lines.extend(
        [
            "#include <array>",
            "#include <cstddef>",
            "#include <cstdint>",
            "#include <span>",
            "#include <string>",
            "#include <string_view>",
            "#include <vector>",
            "",
            '#include \"probe/protocol_codec.hpp\"',
            "",
            "namespace protocol_vectors {",
            "",
            "namespace source_hashes {",
            f"constexpr std::array<uint8_t, 32> pairing_doc = {byte_array_expr(pairing_doc_sha, 32)};",
            f"constexpr std::array<uint8_t, 32> pairing_fixture = {byte_array_expr(pairing_fixture_sha, 32)};",
            f"constexpr std::array<uint8_t, 32> gatt_doc = {byte_array_expr(gatt_doc_sha, 32)};",
            f"constexpr std::array<uint8_t, 32> gatt_fixture = {byte_array_expr(gatt_fixture_sha, 32)};",
            f"constexpr std::array<uint8_t, 32> wss_doc = {byte_array_expr(wss_doc_sha, 32)};",
            f"constexpr std::array<uint8_t, 32> wss_fixture = {byte_array_expr(wss_fixture_sha, 32)};",
            "}",
            "",
            "namespace expected_source_hashes {",
            f"constexpr std::array<uint8_t, 32> pairing_doc = {byte_array_expr(CANONICAL_SOURCE_SHA256['pairing-v1.md'], 32)};",
            f"constexpr std::array<uint8_t, 32> pairing_fixture = {byte_array_expr(CANONICAL_SOURCE_SHA256['fixtures/pairing-v1.json'], 32)};",
            f"constexpr std::array<uint8_t, 32> gatt_doc = {byte_array_expr(CANONICAL_SOURCE_SHA256['gatt-auth-v1.md'], 32)};",
            f"constexpr std::array<uint8_t, 32> gatt_fixture = {byte_array_expr(CANONICAL_SOURCE_SHA256['fixtures/gatt-auth-v1.json'], 32)};",
            f"constexpr std::array<uint8_t, 32> wss_doc = {byte_array_expr(CANONICAL_SOURCE_SHA256['wss-auth-v1.md'], 32)};",
            f"constexpr std::array<uint8_t, 32> wss_fixture = {byte_array_expr(CANONICAL_SOURCE_SHA256['fixtures/wss-auth-v1.json'], 32)};",
            "}",
            "",
            "inline bool source_files_verified() {",
            "  return source_hashes::pairing_doc ==",
            "             expected_source_hashes::pairing_doc &&",
            "         source_hashes::pairing_fixture ==",
            "             expected_source_hashes::pairing_fixture &&",
            "         source_hashes::gatt_doc ==",
            "             expected_source_hashes::gatt_doc &&",
            "         source_hashes::gatt_fixture ==",
            "             expected_source_hashes::gatt_fixture &&",
            "         source_hashes::wss_doc ==",
            "             expected_source_hashes::wss_doc &&",
            "         source_hashes::wss_fixture ==",
            "             expected_source_hashes::wss_fixture;",
            "}",
            "",
            "namespace pairing {",
            f"constexpr std::array<uint8_t, 32> source_sha256 = {byte_array_expr(pairing_sha, 32)};",
            f"constexpr std::array<uint8_t, {len(PAIRING_MAGIC)}> magic = {byte_array_expr(PAIRING_MAGIC.hex(), len(PAIRING_MAGIC))};",
            f"constexpr std::array<uint8_t, {len(PROTOCOL_VERSION_BYTES)}> version_bytes = {byte_array_expr(PROTOCOL_VERSION_BYTES.hex(), len(PROTOCOL_VERSION_BYTES))};",
            f"constexpr size_t sec1_bytes = {len(hex_bytes(pairing['device_ephemeral_sec1_hex']))};",
            f"constexpr size_t private_scalar_bytes = {len(hex_bytes(pairing['device_ephemeral_private_scalar_hex']))};",
            f"constexpr size_t nonce_bytes = {len(hex_bytes(pairing['device_nonce_hex']))};",
            f"constexpr std::string_view hkdf_label_root = {string_expr(pairing['pairing_hkdf_labels']['pairing_root'])};",
            f"constexpr std::string_view hkdf_label_gatt_auth = {string_expr(pairing['pairing_hkdf_labels']['gatt_auth'])};",
            f"constexpr std::string_view hkdf_label_sas = {string_expr(pairing['pairing_hkdf_labels']['sas_prefix'])};",
            f"constexpr uint32_t sas_max_attempt = {SAS_ATTEMPT_MAX};",
            f"constexpr uint32_t sas_rejection_limit = {SAS_REJECTION_LIMIT}u;",
            "",
            "const PairingInput input = {",
            f"  .device_id = {string_expr(pairing['device_id'])},",
            f"  .companion_instance_id = {string_expr(pairing['companion_instance_id'])},",
            f"  .protocol_version = {string_expr(pairing['protocol_version'])},",
            f"  .device_identity_sec1_hex = {string_expr(pairing['device_identity_sec1_hex'])},",
            f"  .companion_identity_sec1_hex = {string_expr(pairing['companion_identity_sec1_hex'])},",
            f"  .device_ephemeral_sec1_hex = {string_expr(pairing['device_ephemeral_sec1_hex'])},",
            f"  .companion_ephemeral_sec1_hex = {string_expr(pairing['companion_ephemeral_sec1_hex'])},",
            f"  .device_nonce_hex = {string_expr(pairing['device_nonce_hex'])}",
            f"  , .companion_nonce_hex = {string_expr(pairing['companion_nonce_hex'])}",
            f"  , .device_ephemeral_private_scalar_hex = {string_expr(pairing['device_ephemeral_private_scalar_hex'])}",
            "};",
            "",
            f"const ByteVector canonical_transcript = {byte_vector_expr(pairing['transcript_hex'])};",
            "",
            "const PairingExpected expected_values = {",
            f"  .pairing_root = {byte_array_expr(pairing['pairing_root_hex'], 32)},",
            f"  .gatt_auth_key = {byte_array_expr(pairing['gatt_auth_hex'], 32)},",
            f"  .sas = {string_expr(pairing['sas'])},",
            f"  .sas_attempt = {int(pairing['sas_attempt'])}",
            "};",
            "",
            "}",
            "",
            "namespace gatt {",
            f"constexpr std::array<uint8_t, 32> source_sha256 = {byte_array_expr(gatt_sha, 32)};",
            f"constexpr uint8_t frame_version = {hex_bytes(gatt['canonical_frame_hex'])[0]};",
            f"const GattAuthKey auth_key = {byte_array_expr(pairing['gatt_auth_hex'], 32)};",
            f"const ConnectionId connection_id = {byte_array_expr(gatt['gatt_connection_id_hex'], 16)};",
            "const GattMessage message = {",
            f"  .operation_id = {byte_array_expr(gatt['gatt_operation_id_hex'], 16)},",
            f"  .full_message_utf8 = {byte_vector_expr(gatt['gatt_full_message_utf8_hex'])},",
            f"  .fragment = {byte_vector_expr(gatt['gatt_fragment_hex'])},",
            f"  .counter = {int(gatt['gatt_counter'])},",
            f"  .fragment_index = {int(gatt['gatt_fragment_index'])},",
            f"  .fragment_count = {int(gatt['gatt_fragment_count'])},",
            f"  .flags = {int(gatt['gatt_flags'])}",
            "};",
            f"const ByteVector authenticated_bytes = {byte_vector_expr(gatt['canonical_frame_hex'])};",
            f"const AuthTag tag = {byte_array_expr(gatt['tag_hex'], 16)};",
            f"constexpr Counter initial_counter = static_cast<Counter>({int(gatt['gatt_counter'])});",
            f"constexpr Counter counter_after_first_frame = static_cast<Counter>({int(gatt['gatt_counter']) + 1});",
            f"const std::string_view hmac_label = {string_expr(gatt['gatt_hmac_label'])};",
            f"constexpr size_t counter_window = {GATT_COUNTER_WINDOW};",
            f"constexpr size_t fragment_length = {len(hex_bytes(gatt['gatt_fragment_hex']))};",
            "",
            "}",
            "",
            "namespace wss {",
            f"constexpr std::array<uint8_t, 32> source_sha256 = {byte_array_expr(wss_sha, 32)};",
            "const WssAuthInput input = {",
            f"  .companion_instance_id = {string_expr(wss['companion_instance_id'])},",
            f"  .device_id = {string_expr(wss['device_id'])},",
            f"  .protocol_version = {string_expr(wss['protocol_version'])}",
            f"  , .exporter_hex = {string_expr(wss['tls_exporter_hex'])}",
            f"  , .challenge_hex = {string_expr(wss['wss_challenge_hex'])}",
            "};",
            f"const ByteVector canonical_message = {byte_vector_expr(wss['canonical_message_hex'])};",
            f"const SignatureBytes signature = {byte_array_expr(wss['signature_hex'], 64)};",
            f"const PublicKeyBytes device_public_key = {byte_vector_expr(wss['peer_spki_hex'])};",
            f"constexpr std::array<uint8_t, 32> spki_sha256 = {byte_array_expr(wss['peer_spki_sha256_hex'], 32)};",
            f"const std::string_view exporter_label = {string_expr(wss['exporter_label'])};",
            f"constexpr size_t exporter_bytes = {len(hex_bytes(wss['tls_exporter_hex']))};",
            "",
            "inline WssAuthInput make_runtime_input(",
            "    std::span<const uint8_t> exporter,",
            "    std::span<const uint8_t> challenge,",
            "    const RuntimeWssIdentity& identity) {",
            "  return WssAuthInput{",
            "    .companion_instance_id = identity.companion_instance_id,",
            "    .device_id = identity.device_id,",
            "    .protocol_version = identity.protocol_version,",
            "    .exporter_hex = [] (std::span<const uint8_t> bytes) {",
            "      constexpr char kHexTable[] = \"0123456789abcdef\";",
            "      std::string out;",
            "      out.reserve(bytes.size() * 2);",
            "      for (uint8_t value : bytes) {",
            "        out.push_back(kHexTable[(value >> 4) & 0x0F]);",
            "        out.push_back(kHexTable[value & 0x0F]);",
            "      }",
            "      return out;",
            "    }(exporter),",
            "    .challenge_hex = [] (std::span<const uint8_t> bytes) {",
            "      constexpr char kHexTable[] = \"0123456789abcdef\";",
            "      std::string out;",
            "      out.reserve(bytes.size() * 2);",
            "      for (uint8_t value : bytes) {",
            "        out.push_back(kHexTable[(value >> 4) & 0x0F]);",
            "        out.push_back(kHexTable[value & 0x0F]);",
            "      }",
            "      return out;",
            "    }(challenge),",
            "  };",
            "}",
            "",
            "}",
            "",
            "}",
            "",
        ]
    )

    rendered = "\n".join(lines)
    if check:
        if not output.is_file() or output.read_text(encoding="utf-8") != rendered:
            raise ValueError("generated protocol header is stale")
        return

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(rendered, encoding="utf-8")


def main() -> int:
    args = parse_args()
    generate_cpp(args.protocol_root, args.output, check=args.check)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
