import hashlib
import hmac
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.phase0 import generate_security_vectors as vectors


class SecurityVectorFixtureTest(unittest.TestCase):
    PROJECT_ROOT = Path(__file__).resolve().parents[2]
    FIXTURE_DIR = PROJECT_ROOT / "protocol" / "phase0" / "fixtures"
    PAIRING_FIXTURE = FIXTURE_DIR / "pairing-v1.json"
    GATT_FIXTURE = FIXTURE_DIR / "gatt-auth-v1.json"
    WSS_FIXTURE = FIXTURE_DIR / "wss-auth-v1.json"

    def _load(self, path: Path) -> dict:
        return json.loads(path.read_text(encoding="utf-8"))

    def test_pairing_fixture_is_self_consistent(self) -> None:
        fixture = self._load(self.PAIRING_FIXTURE)

        transcript = bytes.fromhex(fixture["transcript_hex"])
        self.assertEqual(fixture["transcript_sha256"], hashlib.sha256(transcript).hexdigest())
        self.assertNotEqual(fixture["pairing_root_hex"], fixture["gatt_auth_hex"])
        self.assertRegex(fixture["sas"], r"^[0-9]{6}$")
        self.assertEqual(64, len(fixture["device_nonce_hex"]))
        self.assertEqual(64, len(fixture["companion_nonce_hex"]))
        self.assertEqual("cardputer-codex/pair-root/v1", fixture["pairing_hkdf_labels"]["pairing_root"])
        self.assertTrue(fixture["test_only"])
        self.assertGreater(len(fixture["warnings"]), 0)

        self.assertEqual(130, len(fixture["device_identity_sec1_hex"]))
        self.assertEqual(130, len(fixture["companion_identity_sec1_hex"]))
        self.assertEqual(130, len(fixture["device_ephemeral_sec1_hex"]))
        self.assertEqual(130, len(fixture["companion_ephemeral_sec1_hex"]))
        self.assertEqual("1", fixture["post_sas_policy"]["version"])
        self.assertEqual(32, fixture["post_sas_policy"]["challenge_length_bytes"])
        self.assertEqual(60, fixture["post_sas_policy"]["challenge_ttl_seconds"])
        self.assertEqual(2, fixture["post_sas_requirements"]["required_sas_confirmations"])

    def test_pairing_mutation_changes_outputs(self) -> None:
        base_inputs = vectors._default_inputs()
        base = vectors._build_all_fixtures(base_inputs)

        mutated_nonce = dict(base_inputs)
        mutated_nonce["device_nonce_hex"] = "11" * 32
        self.assertNotEqual(
            base["pairing-v1"]["pairing_root_hex"],
            vectors._build_all_fixtures(mutated_nonce)["pairing-v1"]["pairing_root_hex"],
        )

        mutated_ephemeral = dict(base_inputs)
        mutated_ephemeral["companion_ephemeral_private_scalar_hex"] = (
            "3333333333333333333333333333333333333333333333333333333333333333"
        )
        self.assertNotEqual(
            base["pairing-v1"]["pairing_root_hex"],
            vectors._build_all_fixtures(mutated_ephemeral)["pairing-v1"]["pairing_root_hex"],
        )

        mutated_device_identity = dict(base_inputs)
        mutated_device_identity["device_identity_private_scalar_hex"] = (
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        )
        self.assertNotEqual(
            base["pairing-v1"]["pairing_root_hex"],
            vectors._build_all_fixtures(mutated_device_identity)["pairing-v1"]["pairing_root_hex"],
        )

        mutated_companion_identity = dict(base_inputs)
        mutated_companion_identity["companion_identity_private_scalar_hex"] = (
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        )
        mutated_companion_identity["peer_spki_sha256_hex"] = vectors._public_digest_hex(
            vectors._private_key_from_scalar(
                mutated_companion_identity["companion_identity_private_scalar_hex"],
            )
        )
        self.assertNotEqual(
            base["pairing-v1"]["pairing_root_hex"],
            vectors._build_all_fixtures(mutated_companion_identity)["pairing-v1"]["pairing_root_hex"],
        )

        mutated_version = dict(base_inputs)
        mutated_version["protocol_version"] = "2.0"
        mutated_pairing = vectors._build_all_fixtures(mutated_version)
        self.assertNotEqual(
            base["pairing-v1"]["pairing_root_hex"],
            mutated_pairing["pairing-v1"]["pairing_root_hex"],
        )
        self.assertNotEqual(
            base["pairing-v1"]["transcript_hex"],
            mutated_pairing["pairing-v1"]["transcript_hex"],
        )

    def test_gatt_tag_is_16_bytes_and_frame_shape(self) -> None:
        fixture = self._load(self.GATT_FIXTURE)
        self.assertEqual(32, len(fixture["tag_hex"]))
        self.assertEqual("cardputer-codex/gatt-auth/v1", fixture["gatt_hmac_label"])
        self.assertTrue(fixture["test_only"])
        self.assertGreater(len(fixture["warnings"]), 0)
        frame = bytes.fromhex(fixture["canonical_frame_hex"])
        self.assertGreaterEqual(len(frame), 84)
        self.assertLessEqual(len(frame), 496)
        self.assertGreaterEqual(len(frame) + 16, 100)
        self.assertLessEqual(len(frame) + 16, 512)

        self.assertEqual(1, frame[0])
        self.assertEqual(fixture["gatt_flags"], frame[1])
        self.assertEqual(bytes.fromhex(fixture["gatt_connection_id_hex"]), frame[2:18])
        self.assertEqual(bytes.fromhex(fixture["gatt_operation_id_hex"]), frame[18:34])
        self.assertEqual(fixture["gatt_counter"], int.from_bytes(frame[34:42], "big"))
        self.assertEqual(fixture["gatt_fragment_index"], int.from_bytes(frame[42:44], "big"))
        self.assertEqual(fixture["gatt_fragment_count"], int.from_bytes(frame[44:46], "big"))
        self.assertEqual(fixture["gatt_full_message_utf8_length"], int.from_bytes(frame[46:50], "big"))
        self.assertEqual(
            fixture["gatt_full_message_sha256_hex"],
            hashlib.sha256(bytes.fromhex(fixture["gatt_full_message_utf8_hex"])).hexdigest(),
        )
        self.assertEqual(fixture["gatt_fragment_length"], int.from_bytes(frame[82:84], "big"))
        self.assertEqual(
            bytes.fromhex(fixture["gatt_fragment_hex"]),
            frame[84 : 84 + fixture["gatt_fragment_length"]],
        )
        expected_tag = hmac.new(
            bytes.fromhex(fixture["gatt_auth_hex"]),
            fixture["gatt_hmac_label"].encode("ascii") + frame,
            hashlib.sha256,
        ).digest()[:16].hex()
        self.assertEqual(expected_tag, fixture["tag_hex"])

        self.assertTrue(
            vectors.verify_gatt_tag(
                gatt_auth_key_hex=fixture["gatt_auth_hex"],
                canonical_frame_hex=fixture["canonical_frame_hex"],
                tag_hex=fixture["tag_hex"],
                label=fixture["gatt_hmac_label"].encode("ascii"),
            )
        )
        self.assertFalse(
            vectors.verify_gatt_tag(
                gatt_auth_key_hex=fixture["gatt_auth_hex"],
                canonical_frame_hex=fixture["canonical_frame_hex"],
                tag_hex=("11" + fixture["tag_hex"][2:]),
                label=fixture["gatt_hmac_label"].encode("ascii"),
            )
        )

    def test_wss_signature_is_fixed_width_raw_rs(self) -> None:
        fixture = self._load(self.WSS_FIXTURE)
        self.assertEqual(128, len(fixture["signature_hex"]))
        self.assertEqual("device", fixture["role"])
        self.assertTrue(fixture["test_only"])

        signer_private = vectors._private_key_from_scalar(fixture["signer_private_scalar_hex"])
        message = bytes.fromhex(fixture["canonical_message_hex"])

        self.assertTrue(
            vectors.verify_wss_signature(
                public_key=signer_private.public_key(),
                message=message,
                signature_hex=fixture["signature_hex"],
            )
        )

        expected_message = (
            vectors.lp16(bytes.fromhex(fixture["tls_exporter_hex"]))
            + vectors.lp16(fixture["companion_instance_id"].encode("utf-8"))
            + vectors.lp16(fixture["device_id"].encode("utf-8"))
            + vectors.lp16(fixture["protocol_version"].encode("utf-8"))
            + vectors.lp16(bytes.fromhex(fixture["wss_challenge_hex"]))
        )
        self.assertEqual(expected_message, bytes.fromhex(fixture["canonical_message_hex"]))

        self.assertEqual(182, len(fixture["peer_spki_hex"]))

        with self.assertRaises(ValueError):
            bad = vectors._default_inputs()
            bad["role"] = "companion"
            vectors._build_all_fixtures(bad)

    def test_gatt_counter_window_logic(self) -> None:
        self.assertTrue(
            vectors.is_counter_acceptable(
                0,
                highest_counter=-1,
                accepted_counters=set(),
                connection_id="00112233445566778899aabbccddeeff00",
                expected_connection_id="00112233445566778899aabbccddeeff00",
                window=32,
            )
        )
        self.assertFalse(
            vectors.is_counter_acceptable(
                1,
                highest_counter=-1,
                accepted_counters=set(),
                connection_id="00112233445566778899aabbccddeeff00",
                expected_connection_id="00112233445566778899aabbccddeeff00",
                window=32,
            )
        )
        self.assertTrue(
            vectors.is_counter_acceptable(
                13,
                highest_counter=12,
                accepted_counters=set(),
                connection_id="00112233445566778899aabbccddeeff00",
                expected_connection_id="00112233445566778899aabbccddeeff00",
                window=32,
            )
        )
        self.assertTrue(
            vectors.is_counter_acceptable(
                10,
                highest_counter=12,
                accepted_counters={11},
                connection_id="00112233445566778899aabbccddeeff00",
                expected_connection_id="00112233445566778899aabbccddeeff00",
                window=32,
            )
        )
        self.assertFalse(
            vectors.is_counter_acceptable(
                10,
                highest_counter=12,
                accepted_counters={10},
                connection_id="00112233445566778899aabbccddeeff00",
                expected_connection_id="00112233445566778899aabbccddeeff00",
                window=32,
            )
        )
        self.assertFalse(
            vectors.is_counter_acceptable(
                45,
                highest_counter=12,
                accepted_counters=set(),
                connection_id="00112233445566778899aabbccddeeff00",
                expected_connection_id="00112233445566778899aabbccddeeff00",
                window=32,
            )
        )
        self.assertFalse(
            vectors.is_counter_acceptable(
                9,
                highest_counter=12,
                accepted_counters=set(),
                connection_id="00112233445566778899aabbccddeeff99",
                expected_connection_id="00112233445566778899aabbccddeeff00",
                window=32,
            )
        )

    def test_sas_attempt_window_enforces_rejection_and_exhaustion(self) -> None:
        default_inputs = vectors._default_inputs()
        shared_secret = vectors._private_key_from_scalar(
            default_inputs["device_ephemeral_private_scalar_hex"],
        ).exchange(
            vectors.ec.ECDH(),
            vectors._private_key_from_scalar(
                default_inputs["companion_ephemeral_private_scalar_hex"],
            ).public_key(),
        )

        transcript_sha256 = hashlib.sha256(
            bytes.fromhex(vectors.build_pairing_fixture(default_inputs)["transcript_hex"])
        ).digest()

        vectors.derive_sas(
            shared_secret=shared_secret,
            salt=transcript_sha256,
            attempt_min=vectors.SAS_ATTEMPT_MIN,
            attempt_max=vectors.SAS_ATTEMPT_MAX,
        )

        with self.assertRaises(ValueError):
            vectors.derive_sas(
                shared_secret=shared_secret,
                salt=transcript_sha256,
                attempt_min=0,
                attempt_max=0,
                sample=lambda *_: (0xFFFFFFFF).to_bytes(4, "big"),
            )

        def always_reject(_: bytes, __: bytes, ___: bytes) -> bytes:
            return (vectors.SAS_REJECTION_LIMIT + 1).to_bytes(4, "big")

        with self.assertRaises(ValueError):
            vectors.derive_sas(
                shared_secret=shared_secret,
                salt=transcript_sha256,
                sample=always_reject,
            )

    def test_fixture_values_match_generator_and_mutation_changes_output(self) -> None:
        base_inputs = vectors._default_inputs()
        base = vectors._build_all_fixtures(base_inputs)

        with self.assertRaises(ValueError):
            bad = dict(base_inputs)
            bad["role"] = "companion"
            vectors._build_all_fixtures(bad)

        mutated_connection_id = dict(base_inputs)
        mutated_connection_id["gatt_connection_id_hex"] = "00112233445566778899aabbccddeefe"
        mutated_gatt = vectors._build_all_fixtures(mutated_connection_id)
        self.assertNotEqual(base["gatt-auth-v1"]["tag_hex"], mutated_gatt["gatt-auth-v1"]["tag_hex"])

        mutated_op = dict(base_inputs)
        mutated_op["gatt_operation_id_hex"] = "aabbccddeeff00112233445566778898"
        mutated_gatt = vectors._build_all_fixtures(mutated_op)
        self.assertNotEqual(base["gatt-auth-v1"]["tag_hex"], mutated_gatt["gatt-auth-v1"]["tag_hex"])

        mutated_fragment = dict(base_inputs)
        mutated_fragment["gatt_fragment_hex"] = "00" + base_inputs["gatt_fragment_hex"][2:]
        mutated_gatt = vectors._build_all_fixtures(mutated_fragment)
        self.assertNotEqual(base["gatt-auth-v1"]["tag_hex"], mutated_gatt["gatt-auth-v1"]["tag_hex"])

        mutated_full_message = dict(base_inputs)
        mutated_full_message["gatt_full_message_utf8_hex"] = "48656c6c6f20706f7274666f7263652121"
        mutated_full_message_gatt = vectors._build_all_fixtures(mutated_full_message)["gatt-auth-v1"]
        self.assertNotEqual(
            base["gatt-auth-v1"]["gatt_full_message_sha256_hex"],
            mutated_full_message_gatt["gatt_full_message_sha256_hex"],
        )
        self.assertNotEqual(base["gatt-auth-v1"]["tag_hex"], mutated_full_message_gatt["tag_hex"])

        mutated_exporter = dict(base_inputs)
        mutated_exporter["tls_exporter_hex"] = "bb" * 32
        mutated_wss = vectors._build_all_fixtures(mutated_exporter)
        self.assertNotEqual(
            base["wss-auth-v1"]["signature_hex"],
            mutated_wss["wss-auth-v1"]["signature_hex"],
        )

        mutated_challenge = dict(base_inputs)
        mutated_challenge["wss_challenge_hex"] = ("bb" * 32)
        mutated_wss = vectors._build_all_fixtures(mutated_challenge)
        self.assertNotEqual(
            base["wss-auth-v1"]["signature_hex"],
            mutated_wss["wss-auth-v1"]["signature_hex"],
        )

        mutated_identity = dict(base_inputs)
        mutated_identity["device_identity_private_scalar_hex"] = (
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        )
        mutated_wss = vectors._build_all_fixtures(mutated_identity)
        self.assertNotEqual(
            base["wss-auth-v1"]["signature_hex"],
            mutated_wss["wss-auth-v1"]["signature_hex"],
        )

        with self.assertRaises(ValueError):
            wrong_spki = dict(base_inputs)
            wrong_spki["peer_spki_sha256_hex"] = "00" * 32
            vectors._build_all_fixtures(wrong_spki)

        with self.assertRaises(ValueError):
            wrong_exporter_context = dict(base_inputs)
            wrong_exporter_context["tls_exporter_context_hex"] = "1122"
            vectors._build_all_fixtures(wrong_exporter_context)

        with self.assertRaises(ValueError):
            bad_private_scalar = dict(base_inputs)
            bad_private_scalar["device_identity_private_scalar_hex"] = "00" * 32
            vectors._build_all_fixtures(bad_private_scalar)

    def test_private_scalar_width_and_range_validation(self) -> None:
        with self.assertRaises(ValueError):
            vectors._private_key_from_scalar("00")

        with self.assertRaises(ValueError):
            vectors._private_key_from_scalar("zz" * 32)

        with self.assertRaises(ValueError):
            vectors._private_key_from_scalar("00" * 31)

        with self.assertRaises(ValueError):
            vectors._private_key_from_scalar("00" * 33)

        with self.assertRaises(ValueError):
            vectors._private_key_from_scalar("00" * 32)

        with self.assertRaises(ValueError):
            vectors._private_key_from_scalar(f"{vectors.CURVE.group_order:064x}")

    def test_post_sas_binding_validator(self) -> None:
        base_inputs = vectors._default_inputs()
        pairing = vectors._build_all_fixtures(base_inputs)["pairing-v1"]
        policy = pairing["post_sas_policy"]
        requirements = pairing["post_sas_requirements"]
        expected_challenge = pairing["post_sas_binding_state"]["wss_challenge_hex"]
        expected_peer = pairing["post_sas_binding_state"]["peer_spki_sha256_hex"]

        self.assertTrue(
            vectors.validate_post_sas_binding(
                policy=policy,
                requirement=requirements,
                sas_confirmation_unix_s=1_000,
                wss_arrival_unix_s=1_030,
                challenge_hex=expected_challenge,
                expected_challenge_hex=expected_challenge,
                observed_peer_spki_sha256_hex=expected_peer,
                expected_peer_spki_sha256_hex=expected_peer,
                has_wss_channel=True,
                has_encrypted_bonded_gatt=True,
            )
        )

        self.assertFalse(
            vectors.validate_post_sas_binding(
                policy=policy,
                requirement=requirements,
                sas_confirmation_unix_s=1_000,
                wss_arrival_unix_s=1_030,
                challenge_hex=expected_challenge,
                expected_challenge_hex=expected_challenge,
                observed_peer_spki_sha256_hex=expected_peer,
                expected_peer_spki_sha256_hex=expected_peer,
                has_wss_channel=False,
                has_encrypted_bonded_gatt=True,
            )
        )
        self.assertFalse(
            vectors.validate_post_sas_binding(
                policy=policy,
                requirement=requirements,
                sas_confirmation_unix_s=1_000,
                wss_arrival_unix_s=1_030,
                challenge_hex=expected_challenge,
                expected_challenge_hex=expected_challenge,
                observed_peer_spki_sha256_hex=expected_peer,
                expected_peer_spki_sha256_hex=expected_peer,
                has_wss_channel=True,
                has_encrypted_bonded_gatt=False,
            )
        )

        self.assertFalse(
            vectors.validate_post_sas_binding(
                policy=policy,
                requirement=requirements,
                sas_confirmation_unix_s=1_000,
                wss_arrival_unix_s=1_070,
                challenge_hex=expected_challenge,
                expected_challenge_hex=expected_challenge,
                observed_peer_spki_sha256_hex=expected_peer,
                expected_peer_spki_sha256_hex=expected_peer,
                has_wss_channel=True,
                has_encrypted_bonded_gatt=True,
            )
        )

        self.assertFalse(
            vectors.validate_post_sas_binding(
                policy=policy,
                requirement=requirements,
                sas_confirmation_unix_s=1_000,
                wss_arrival_unix_s=1_010,
                challenge_hex="11" + expected_challenge[2:],
                expected_challenge_hex=expected_challenge,
                observed_peer_spki_sha256_hex=expected_peer,
                expected_peer_spki_sha256_hex=expected_peer,
                has_wss_channel=True,
                has_encrypted_bonded_gatt=True,
            )
        )

        self.assertFalse(
            vectors.validate_post_sas_binding(
                policy=policy,
                requirement=requirements,
                sas_confirmation_unix_s=1_000,
                wss_arrival_unix_s=1_010,
                challenge_hex=("11" * 31),
                expected_challenge_hex=expected_challenge,
                observed_peer_spki_sha256_hex=expected_peer,
                expected_peer_spki_sha256_hex=expected_peer,
                has_wss_channel=True,
                has_encrypted_bonded_gatt=True,
            )
        )

    def test_width_and_context_rejections(self) -> None:
        base_inputs = vectors._default_inputs()

        short_nonce = dict(base_inputs)
        short_nonce["device_nonce_hex"] = "00" * 16
        with self.assertRaises(ValueError):
            vectors._build_all_fixtures(short_nonce)

        short_connection_id = dict(base_inputs)
        short_connection_id["gatt_connection_id_hex"] = "00" * 15
        with self.assertRaises(ValueError):
            vectors._build_all_fixtures(short_connection_id)

        short_operation_id = dict(base_inputs)
        short_operation_id["gatt_operation_id_hex"] = "00" * 1
        with self.assertRaises(ValueError):
            vectors._build_all_fixtures(short_operation_id)

        short_exporter = dict(base_inputs)
        short_exporter["tls_exporter_hex"] = "00" * 31
        with self.assertRaises(ValueError):
            vectors._build_all_fixtures(short_exporter)

        short_challenge = dict(base_inputs)
        short_challenge["wss_challenge_hex"] = "00" * 31
        with self.assertRaises(ValueError):
            vectors._build_all_fixtures(short_challenge)

    def test_deterministic_regeneration(self) -> None:
        script = self.PROJECT_ROOT / "tools/phase0/generate_security_vectors.py"
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            transcript_path = temp / "transcript.bin"
            result = subprocess.run(
                [
                    sys.executable,
                    str(script),
                    "--write",
                    str(temp / "protocol/phase0/fixtures"),
                    "--emit-transcript",
                    str(transcript_path),
                ],
                cwd=str(self.PROJECT_ROOT),
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0)
            self.assertTrue(transcript_path.exists())

            generated_pairing = self._load(temp / "protocol/phase0/fixtures/pairing-v1.json")
            generated_gatt = self._load(temp / "protocol/phase0/fixtures/gatt-auth-v1.json")
            generated_wss = self._load(temp / "protocol/phase0/fixtures/wss-auth-v1.json")

            self.assertEqual(generated_pairing, self._load(self.PAIRING_FIXTURE))
            self.assertEqual(generated_gatt, self._load(self.GATT_FIXTURE))
            self.assertEqual(generated_wss, self._load(self.WSS_FIXTURE))


if __name__ == "__main__":
    unittest.main()
