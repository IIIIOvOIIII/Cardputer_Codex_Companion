from __future__ import annotations

import hashlib
import io
import json
import os
from dataclasses import dataclass
from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from scripts.phase0 import run_concurrency_hil as hil
from tools.phase0.validate_concurrency_report import validate_report


@dataclass(frozen=True)
class FakePort:
    device: str
    description: str = ""


class FakeCompleted:
    def __init__(self, command: list[str] | None = None, returncode: int = 0, stdout: str = "", stderr: str = ""):
        self.args = command or []
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr


def _ifconfig_output(source_count: int = 20, base: str = "192.168.1") -> str:
    return "\n".join(
        f"inet {base}.{index} netmask 255.255.255.0"
        for index in range(1, source_count + 1)
    )


def _canonical(event: dict) -> str:
    return json.dumps(event, separators=(",", ":"), sort_keys=True)


def _build_manifest(path: Path, *, chip_model: str = "ESP32-S3", flash_bytes: int = 8_388_608, psram_bytes: int = 0) -> None:
    payload = {
        "manifest_version": 1,
        "model": "M5Stack Cardputer",
        "product_revision": "rev-1",
        "pcb_revision": "rev-2",
        "chip_model": chip_model,
        "chip_revision": 1,
        "flash_jedec_id": "123456",
        "flash_bytes": flash_bytes,
        "psram_bytes": psram_bytes,
        "usb_serial_sha256": "0" * 64,
        "keyboard_matrix_source": {
            "repository": "m5stack/M5Cardputer",
            "commit": "d" * 40,
            "outputs": [1, 2, 3],
            "inputs": [4, 5, 6],
            "physically_verified": True,
        },
        "captured_at": "2026-07-24T12:00:00Z",
    }
    path.write_text(json.dumps(payload), encoding="utf-8")


def _help_text() -> str:
    return (
        "--interface --interface-address --interface-netmask --peripheral-id "
        "--device-id-hex --gatt-secret-file --tls-identity-label --run-id --boot-id "
        "--app-elf-sha256 --probe-firmware-sha256 --duration-seconds\n"
    )


def _args_for(tmp_path: Path, *, duration: int = 1800) -> tuple:
    firmware = tmp_path / "firmware.bin"
    firmware.write_bytes(b"\x00" * 4096)

    manifest = tmp_path / "manifest.json"
    _build_manifest(manifest)

    companion = tmp_path / "probe"
    companion.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
    companion.chmod(0o755)

    gatt = tmp_path / "gatt-secret.bin"
    gatt.write_bytes(b"x" * 32)
    gatt.chmod(0o600)

    parser = hil._build_parser()
    argv = [
        "--auto-port",
        "--firmware-bin",
        str(firmware),
        "--hardware-manifest",
        str(manifest),
        "--companion-probe",
        str(companion),
        "--companion-interface",
        "en0",
        "--companion-address",
        "192.168.1.10",
        "--companion-netmask",
        "255.255.255.0",
        "--companion-peripheral-id",
        "aaaaaaaa-bbbb-4aaa-8bbb-cccccccccccc",
        "--companion-device-id-hex",
        "00112233445566778899aabbccddeeff",
        "--companion-gatt-secret-file",
        str(gatt),
        "--companion-tls-identity-label",
        "label",
        "--attacker-interface",
        "auto",
        "--duration-seconds",
        str(duration),
        "--output",
        str(tmp_path / "output"),
    ]
    return parser.parse_args(argv), argv


def _runner_default(cmd: list[str], **_: object) -> FakeCompleted:
    if "--help" in cmd:
        return FakeCompleted(cmd, 0, _help_text(), "")
    if "chip_id" in cmd:
        return FakeCompleted(cmd, 0, "Chip is ESP32-S3", "")
    if "flash_id" in cmd:
        return FakeCompleted(cmd, 0, "Detected flash size: 8MB", "")
    if "rev-parse" in cmd:
        return FakeCompleted(cmd, 0, hil.EXPECTED_IDF_COMMIT + "\n", "")
    if "status" in cmd:
        return FakeCompleted(cmd, 0, "", "")
    if cmd and cmd[0] == "esptool.py" and cmd[1] == "image_info":
        return FakeCompleted(cmd, 0, "app_elf_sha256=" + ("aa" * 32))
    if cmd and cmd[0] == "esptool.py" and cmd[1] == "write_flash":
        return FakeCompleted(cmd, 0)
    return FakeCompleted(cmd, 0, "")


def test_cross_boot_or_nonoverlap_is_rejected() -> None:
    records = [
        hil.EvidenceClock(
            producer="firmware",
            run_id="run-a",
            boot_id="boot-a",
            app_elf_sha256="22" * 32,
            firmware_image_sha256="33" * 32,
            device_id_sha256="11" * 32,
            first_ns=0,
            last_ns=120_000_000_000,
        ),
        hil.EvidenceClock(
            producer="macos_companion",
            run_id="run-a",
            boot_id="boot-b",
            app_elf_sha256="22" * 32,
            firmware_image_sha256="33" * 32,
            device_id_sha256="11" * 32,
            first_ns=10_000_000_000,
            last_ns=110_000_000_000,
        ),
        hil.EvidenceClock(
            producer="attacker",
            run_id="run-a",
            boot_id="boot-a",
            app_elf_sha256="22" * 32,
            firmware_image_sha256="33" * 32,
            device_id_sha256="11" * 32,
            first_ns=20_000_000_000,
            last_ns=130_000_000_000,
        ),
    ]
    assert hil.validate_continuous_window(records) == ["boot_id differs across evidence"]


def test_queue_loss_is_reported_even_when_p95_is_low() -> None:
    assert hil.validate_hid_measurement(
        {
            "generated": 10_000,
            "queued": 9_999,
            "queue_failures": 1,
            "overflow_samples": 0,
            "p95_upper_bound_us": 1000,
            "release_all_observed": True,
        }
    ) == [
        "generated must equal queued",
        "queue_failures must equal zero",
    ]


def test_serial_port_count_must_be_exactly_one(tmp_path: Path) -> None:
    args, _ = _args_for(tmp_path)

    blockers, context = hil._validate_preflight(
        args,
        list_ports=lambda: [],
        command_runner=_runner_default,
        ifconfig_runner=lambda _: FakeCompleted([], 0, _ifconfig_output()),
    )
    assert "no_esp32s3_serial_candidates" in blockers
    assert context is None

    blockers, context = hil._validate_preflight(
        args,
        list_ports=lambda: [FakePort("/dev/ttyACM0", "ESP32-S3"), FakePort("/dev/ttyACM1", "ESP32-S3")],
        command_runner=_runner_default,
        ifconfig_runner=lambda _: FakeCompleted([], 0, _ifconfig_output()),
    )
    assert "multiple_esp32s3_serial_candidates" in blockers
    assert context is None

    blockers, context = hil._validate_preflight(
        args,
        list_ports=lambda: [FakePort("/dev/ttyACM0", "ESP32-S3")],
        command_runner=_runner_default,
        ifconfig_runner=lambda _: FakeCompleted([], 0, _ifconfig_output()),
    )
    assert not blockers
    assert context is not None
    assert context.serial_port == "/dev/ttyACM0"


def test_serial_probe_requires_esp32s3_with_exactly_8mb_flash() -> None:
    ports = [
        FakePort("/dev/ttyESP32", "ESP32"),
        FakePort("/dev/ttyS3-small", "ESP32-S3"),
        FakePort("/dev/ttyS3", "ESP32-S3"),
    ]

    def probe(cmd: list[str], **_: object) -> FakeCompleted:
        port = cmd[cmd.index("--port") + 1]
        if "chip_id" in cmd:
            model = "ESP32-S3" if port != "/dev/ttyESP32" else "ESP32"
            return FakeCompleted(cmd, 0, f"Chip is {model}")
        if "flash_id" in cmd:
            size = "4MB" if port == "/dev/ttyS3-small" else "8MB"
            return FakeCompleted(cmd, 0, f"Detected flash size: {size}")
        raise AssertionError(f"unexpected probe command: {cmd}")

    selected, blockers = hil._select_port(
        list_ports=lambda: ports,
        command_runner=probe,
    )

    assert blockers == []
    assert selected == "/dev/ttyS3"


def test_duration_validation_and_source_boundary(tmp_path: Path) -> None:
    args, _ = _args_for(tmp_path, duration=1799)
    blockers, context = hil._validate_preflight(
        args,
        list_ports=lambda: [FakePort("/dev/ttyACM0", "ESP32-S3")],
        command_runner=_runner_default,
        ifconfig_runner=lambda _: FakeCompleted([], 0, _ifconfig_output()),
    )
    assert "duration_not_1800" in blockers
    assert context is None

    args, _ = _args_for(tmp_path)
    blockers, context = hil._validate_preflight(
        args,
        list_ports=lambda: [FakePort("/dev/ttyACM0", "ESP32-S3")],
        command_runner=_runner_default,
        ifconfig_runner=lambda _: FakeCompleted([], 0, _ifconfig_output(source_count=16)),
    )
    assert "insufficient_assigned_source_addresses" in blockers
    assert "insufficient_routable_source_addresses" in blockers
    assert context is None


def test_idf_locks_bind_the_exact_checkout(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    firmware_dir = tmp_path / "firmware"
    firmware_dir.mkdir()
    (tmp_path / "toolchain.lock.json").write_text(
        json.dumps(
            {
                "esp_idf": {
                    "tag": hil.EXPECTED_IDF_TAG,
                    "commit": hil.EXPECTED_IDF_COMMIT,
                }
            }
        ),
        encoding="utf-8",
    )
    (firmware_dir / "dependencies.lock").write_text(
        "dependencies:\n"
        "  idf:\n"
        "    source:\n"
        "      type: idf\n"
        "    version: 5.5.4\n"
        "target: esp32s3\n",
        encoding="utf-8",
    )
    monkeypatch.setattr(hil, "REPO_ROOT", tmp_path)

    assert hil._validate_idf_locks(command_runner=_runner_default) == []

    def wrong_checkout(cmd: list[str], **_: object) -> FakeCompleted:
        return FakeCompleted(cmd, 0, "0" * 40 + "\n")

    assert hil._validate_idf_locks(command_runner=wrong_checkout) == [
        "idf_checkout_commit_mismatch"
    ]

    (tmp_path / "toolchain.lock.json").write_text(
        json.dumps({"esp_idf": {"tag": "v0", "commit": "0" * 40}}),
        encoding="utf-8",
    )
    assert hil._validate_idf_locks(command_runner=_runner_default) == [
        "toolchain_lock_idf_commit_mismatch",
        "toolchain_lock_idf_tag_mismatch",
    ]


def test_manifest_secret_and_help_failure_blocks_preflight(tmp_path: Path) -> None:
    args, _ = _args_for(tmp_path)

    # missing manifest
    args.hardware_manifest = tmp_path / "missing.json"
    blockers, context = hil._validate_preflight(
        args,
        list_ports=lambda: [FakePort("/dev/ttyACM0", "ESP32-S3")],
        command_runner=_runner_default,
        ifconfig_runner=lambda _: FakeCompleted([], 0, _ifconfig_output()),
    )
    assert "missing_hardware_manifest" in blockers
    assert context is None

    # invalid chip / psram
    args.hardware_manifest = tmp_path / "manifest.json"
    _build_manifest(args.hardware_manifest, chip_model="ESP32", psram_bytes=16)
    blockers, context = hil._validate_preflight(
        args,
        list_ports=lambda: [FakePort("/dev/ttyACM0", "ESP32-S3")],
        command_runner=_runner_default,
        ifconfig_runner=lambda _: FakeCompleted([], 0, _ifconfig_output()),
    )
    assert "hardware_manifest_chip_not_esp32s3" in blockers
    assert "hardware_manifest_psram_not_zero" in blockers
    assert context is None

    # invalid secret mode
    args.hardware_manifest = tmp_path / "manifest.json"
    _build_manifest(args.hardware_manifest)
    os.chmod(args.companion_gatt_secret_file, 0o644)
    blockers, context = hil._validate_preflight(
        args,
        list_ports=lambda: [FakePort("/dev/ttyACM0", "ESP32-S3")],
        command_runner=_runner_default,
        ifconfig_runner=lambda _: FakeCompleted([], 0, _ifconfig_output()),
    )
    assert "gatt_secret_not_mode_0600" in blockers
    assert context is None
    os.chmod(args.companion_gatt_secret_file, 0o600)

    def _help_missing(cmd: list[str], **_: object) -> FakeCompleted:
        return FakeCompleted(cmd, 0, "--interface\n") if "--help" in cmd else _runner_default(cmd)

    blockers, context = hil._validate_preflight(
        args,
        list_ports=lambda: [FakePort("/dev/ttyACM0", "ESP32-S3")],
        command_runner=_help_missing,
        ifconfig_runner=lambda _: FakeCompleted([], 0, _ifconfig_output()),
    )
    assert any(item.startswith("companion_help_missing:") for item in blockers)
    assert context is None


def test_unavailable_web_flow_prevents_all_prompts_backup_and_flash(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    args, _ = _args_for(tmp_path)

    flash_calls: list[str] = []

    def no_flash(*_: object, **__: object) -> None:
        flash_calls.append("flash")

    monkeypatch.setattr(
        hil,
        "_validate_preflight",
        lambda *_a, **_k: (
            [],
                hil.ConcurrencyContext(
                    run_id="11" * 16,
                    firmware_bin=args.firmware_bin,
                    firmware_sha256=hil._sha256_file(args.firmware_bin),
                    firmware_bytes=args.firmware_bin.stat().st_size,
                manifest={},
                serial_port="/dev/ttyACM0",
                companion_probe=args.companion_probe,
                companion_interface=args.companion_interface,
                companion_address=args.companion_address,
                companion_netmask=args.companion_netmask,
                    companion_peripheral_id=args.companion_peripheral_id,
                    companion_device_id_hex=args.companion_device_id_hex,
                    companion_device_id_sha256=hil._parse_device_id_sha256(args.companion_device_id_hex),
                    companion_gatt_secret_file=args.companion_gatt_secret_file,
                    companion_tls_identity_label=args.companion_tls_identity_label,
                    duration_seconds=args.duration_seconds,
                    output=args.output,
                ),
        ),
    )
    prompt_calls: list[str] = []
    backup_calls: list[str] = []
    monkeypatch.setattr(
        "builtins.input", lambda *_: prompt_calls.append("input") or ""
    )
    monkeypatch.setattr(
        hil, "_backup_flash", lambda **_: backup_calls.append("backup")
    )
    monkeypatch.setattr(hil, "_flash_firmware", lambda **_: no_flash())

    return_code, report, blockers = hil._run_main_flow(
        args,
        command_runner=_runner_default,
        ifconfig_runner=lambda _: FakeCompleted([], 0, _ifconfig_output()),
        is_tty=lambda: True,
        report_writer=lambda *_: None,
    )
    assert return_code == 1
    assert blockers == ["web_pairing_tls_flow_unavailable"]
    assert report["run"] == {"run_id": report["run"]["run_id"]}
    assert not prompt_calls
    assert not backup_calls
    assert not flash_calls


def test_full_flash_backup_and_app_offset_are_fixed(tmp_path: Path) -> None:
    commands: list[list[str]] = []

    def command_runner(command: list[str], **_: object) -> FakeCompleted:
        commands.append(command)
        if "read_flash" in command:
            Path(command[-1]).write_bytes(b"\x5a" * hil.FLASH_SIZE_BYTES)
        return FakeCompleted(command, 0)

    backup, digest = hil._backup_flash(
        serial_port="/dev/ttyACM0",
        output=tmp_path / "out",
        command_runner=command_runner,
    )
    assert backup.stat().st_size == hil.FLASH_SIZE_BYTES
    assert digest == hashlib.sha256(b"\x5a" * hil.FLASH_SIZE_BYTES).hexdigest()
    assert "0x800000" in commands[0]

    firmware = tmp_path / "firmware.bin"
    firmware.write_bytes(b"firmware")
    hil._flash_firmware(
        firmware_bin=firmware,
        serial_port="/dev/ttyACM0",
        command_runner=command_runner,
    )
    assert "0x10000" in commands[1]


def test_artifact_must_stay_inside_its_output_directory(tmp_path: Path) -> None:
    outside = tmp_path / "outside.log"
    outside.write_text("{}\n", encoding="utf-8")

    with pytest.raises(RuntimeError, match="artifact_outside_output_root"):
        hil._artifact_reference(
            outside,
            1,
            2,
            output_root=tmp_path / "output",
        )


class FakePopen:
    def __init__(self, command: list[str], *_, **__):
        self.command = command
        self.stdout = io.StringIO("")
        self.stderr = io.StringIO("")
        self.returncode = 0

    def poll(self) -> int | None:
        return self.returncode

    def wait(self, timeout: float | None = None) -> int:
        return self.returncode

    def send_signal(self, *_a: object) -> None:
        self.returncode = 143

    def kill(self) -> None:
        self.returncode = 143


def test_companion_session_happy_path_and_failures(tmp_path: Path) -> None:
    args, _ = _args_for(tmp_path)

    valid_events = [
        {
            "producer": "macos_companion",
            "kind": "ready",
            "run_id": "11" * 16,
            "boot_id": "22" * 16,
            "app_elf_sha256": "33" * 32,
            "firmware_image_sha256": "44" * 32,
            "device_id_sha256": hil._parse_device_id_sha256(args.companion_device_id_hex),
            "producer_monotonic_ns": 1,
        },
        {
            "producer": "macos_companion",
            "kind": "heartbeat",
            "run_id": "11" * 16,
            "boot_id": "22" * 16,
            "app_elf_sha256": "33" * 32,
            "firmware_image_sha256": "44" * 32,
            "device_id_sha256": hil._parse_device_id_sha256(args.companion_device_id_hex),
            "producer_monotonic_ns": 2,
        },
        {
            "producer": "macos_companion",
            "kind": "stopped",
            "run_id": "11" * 16,
            "boot_id": "22" * 16,
            "app_elf_sha256": "33" * 32,
            "firmware_image_sha256": "44" * 32,
            "device_id_sha256": hil._parse_device_id_sha256(args.companion_device_id_hex),
            "producer_monotonic_ns": 3,
        },
    ]

    class GoodProcess(FakePopen):
        def __init__(self, command: list[str], *_, **__):
            super().__init__(command)
            self.stdout = io.StringIO("\n".join(_canonical(event) for event in valid_events) + "\n")

    session = hil.ConcurrencyCompanionSession(
        probe=args.companion_probe,
        interface=args.companion_interface,
        interface_address=args.companion_address,
        interface_netmask=args.companion_netmask,
        peripheral_id=args.companion_peripheral_id,
        device_id_hex=args.companion_device_id_hex,
        run_id="11" * 16,
        boot_id="22" * 16,
        app_elf_sha256="33" * 32,
        firmware_image_sha256="44" * 32,
        duration_seconds=1800,
        gatt_secret_file=args.companion_gatt_secret_file,
        tls_identity_label=args.companion_tls_identity_label,
        raw_event_path=tmp_path / "raw" / "companion.log",
        clock=lambda: 1,
        popen_factory=GoodProcess,
    )
    events, errors, artifact = session.run()
    assert not errors
    assert [event["kind"] for event in events] == ["ready", "heartbeat", "stopped"]
    assert artifact is not None
    assert "concurrency-hil-agent" in session.command
    assert "--app-elf-sha256" in session.command

    bad_events = [
        valid_events[0],
        valid_events[0],
    ]

    class DuplicateReadyProcess(FakePopen):
        def __init__(self, command: list[str], *_, **__):
            super().__init__(command)
            self.stdout = io.StringIO("\n".join(_canonical(event) for event in bad_events) + "\n")

    session = hil.ConcurrencyCompanionSession(
        probe=args.companion_probe,
        interface=args.companion_interface,
        interface_address=args.companion_address,
        interface_netmask=args.companion_netmask,
        peripheral_id=args.companion_peripheral_id,
        device_id_hex=args.companion_device_id_hex,
        run_id="11" * 16,
        boot_id="22" * 16,
        app_elf_sha256="33" * 32,
        firmware_image_sha256="44" * 32,
        duration_seconds=1800,
        gatt_secret_file=args.companion_gatt_secret_file,
        tls_identity_label=args.companion_tls_identity_label,
        raw_event_path=tmp_path / "raw" / "dup.log",
        clock=lambda: 2,
        popen_factory=DuplicateReadyProcess,
    )
    _events, errors, _artifact = session.run()
    assert "duplicate_ready" in errors

    malformed = "{invalid json\n"

    class BadJsonProcess(FakePopen):
        def __init__(self, command: list[str], *_, **__):
            super().__init__(command)
            self.stdout = io.StringIO(malformed)

    session = hil.ConcurrencyCompanionSession(
        probe=args.companion_probe,
        interface=args.companion_interface,
        interface_address=args.companion_address,
        interface_netmask=args.companion_netmask,
        peripheral_id=args.companion_peripheral_id,
        device_id_hex=args.companion_device_id_hex,
        run_id="11" * 16,
        boot_id="22" * 16,
        app_elf_sha256="33" * 32,
        firmware_image_sha256="44" * 32,
        duration_seconds=1800,
        gatt_secret_file=args.companion_gatt_secret_file,
        tls_identity_label=args.companion_tls_identity_label,
        raw_event_path=tmp_path / "raw" / "bad.log",
        clock=lambda: 1,
        popen_factory=BadJsonProcess,
    )
    _events, errors, _artifact = session.run()
    assert errors


def test_main_unlinks_secret_on_all_paths(tmp_path: Path) -> None:
    args, argv = _args_for(tmp_path)

    def _success_flow(*_, **__) -> tuple[int, dict, list[str]]:
        return 0, hil._build_incomplete_report("11" * 16, []), []

    def _failure_flow(*_, **__) -> tuple[int, dict, list[str]]:
        return 1, hil._build_incomplete_report("11" * 16, ["blocked"]), ["blocked"]

    for flow in (_success_flow, _failure_flow):
        monkeypatch = pytest.MonkeyPatch()
        monkeypatch.setattr(hil, "_run_main_flow", flow)
        rc = hil.main(argv)
        assert rc in {0, 1}
        assert not args.companion_gatt_secret_file.exists()
        monkeypatch.undo()

    def _interrupt(*_, **__) -> tuple[int, dict, list[str]]:
        raise KeyboardInterrupt

    monkeypatch = pytest.MonkeyPatch()
    monkeypatch.setattr(hil, "_run_main_flow", _interrupt)
    assert hil.main(argv) == 1
    assert not args.companion_gatt_secret_file.exists()
    monkeypatch.undo()


def test_incomplete_report_has_only_known_run_identity_fields(tmp_path: Path) -> None:
    run_id = "11" * 16
    report = hil._build_incomplete_report(run_id, ["blocked"])
    assert set(report["run"].keys()) == {"run_id"}
    assert report["run"]["run_id"] == run_id

    report_path = tmp_path / "report.json"
    report_path.write_text(json.dumps(report), encoding="utf-8")
    assert validate_report(report, report_path) == []


def test_pairing_code_is_not_requested_when_tls_flow_is_unavailable(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    args, _ = _args_for(tmp_path)

    rc, report, blockers = hil._run_main_flow(
        args,
        list_ports=lambda: [FakePort("/dev/ttyACM0", "ESP32-S3")],
        command_runner=_runner_default,
        ifconfig_runner=lambda _: FakeCompleted([], 0, _ifconfig_output()),
        report_writer=lambda *_: None,
        is_tty=lambda: True,
    )

    assert rc == 1
    assert blockers == ["web_pairing_tls_flow_unavailable"]
    assert "12345678" not in json.dumps(report)

    if args.companion_gatt_secret_file.exists():
        args.companion_gatt_secret_file.unlink()
