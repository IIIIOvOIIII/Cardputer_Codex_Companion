from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import signal
import subprocess
import sys
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from ipaddress import IPv4Address, IPv4Network, ip_address
from jsonschema import Draft202012Validator
from pathlib import Path
from typing import Any, Callable, Iterable, Iterator

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.phase0.validate_concurrency_report import (
    EvidenceClock,
    validate_continuous_window,
    validate_hid_measurement,
)
from tools.phase0.validate_hardware_manifest import validate_manifest

SCHEMA_PATH = REPO_ROOT / "protocol/phase0/companion-probe-event.schema.json"
CONCURRENCY_SCHEDULE = (
    ("warmup", 0, 119),
    ("steady", 120, 599),
    ("transient", 600, 899),
    ("attack", 900, 1679),
    ("recovery", 1680, 1799),
)
DURATION_SECONDS = 1800
FLASH_SIZE_BYTES = 8_388_608
APP_PARTITION_OFFSET = 0x10000
EXPECTED_IDF_TAG = "v5.5.4"
EXPECTED_IDF_COMMIT = "735507283d5b2f9fb363a1901172dbd9e847945d"
EXPECTED_COMPANION_OPTIONS = (
    "--interface",
    "--interface-address",
    "--interface-netmask",
    "--peripheral-id",
    "--device-id-hex",
    "--gatt-secret-file",
    "--tls-identity-label",
    "--run-id",
    "--boot-id",
    "--app-elf-sha256",
    "--probe-firmware-sha256",
    "--duration-seconds",
)
CONCURRENCY_HELP_OPTIONS = set(EXPECTED_COMPANION_OPTIONS) | {"--help", "-h", "--version"}

PAIRING_CODE_RE = re.compile(rb"\b\d{8}\b")
MARKERS = (
    b"cp_admin=",
    b"x-csrf-token",
    b"wifi_credential",
    b"wifi_password",
    b"wifi_psk",
    b"exporter bytes",
    b"exporter_bytes",
    b"request body",
    b"request_body",
    b"text payload",
    b"text_payload",
)
PRIVATE_KEY_RE = re.compile(
    rb"-----begin (?:rsa |ec |openssh )?private key-----",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class ConcurrencyContext:
    run_id: str
    firmware_bin: Path
    firmware_sha256: str
    firmware_bytes: int
    manifest: dict[str, Any]
    serial_port: str
    companion_probe: Path
    companion_interface: str
    companion_address: str
    companion_netmask: str
    companion_peripheral_id: str
    companion_device_id_hex: str
    companion_device_id_sha256: str
    companion_gatt_secret_file: Path
    companion_tls_identity_label: str
    duration_seconds: int
    output: Path


@dataclass(frozen=True)
class ArtifactMetadata:
    path: str
    sha256: str
    byte_length: int
    first_runner_receipt_ns: int
    last_runner_receipt_ns: int


CompanionValidator = Draft202012Validator(json.loads(SCHEMA_PATH.read_text(encoding="utf-8")))


def now_iso_z() -> str:
    return datetime.now(tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def monotonic_ns() -> int:
    return __import__("time").monotonic_ns()


def _sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fp:
        for block in iter(lambda: fp.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _sha256_bytes(data: bytes) -> str:
    return _sha256_hex(data)


def _run_command(
    command: list[str],
    *,
    check: bool = False,
    text: bool = True,
    cwd: Path | None = None,
    **kwargs: Any,
) -> subprocess.CompletedProcess:
    return subprocess.run(
        command,
        check=check,
        cwd=str(cwd) if cwd is not None else None,
        text=text,
        capture_output=True,
        **kwargs,
    )


def _default_list_ports() -> list[Any]:
    import serial.tools.list_ports

    return list(serial.tools.list_ports.comports())


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run a 30-minute concurrency HIL capture")
    parser.add_argument("--auto-port", action="store_true")
    parser.add_argument("--firmware-bin", type=Path, required=True)
    parser.add_argument("--hardware-manifest", type=Path, required=True)
    parser.add_argument("--companion-probe", type=Path, required=True)
    parser.add_argument("--companion-interface", required=True)
    parser.add_argument("--companion-address", required=True)
    parser.add_argument("--companion-netmask", required=True)
    parser.add_argument("--companion-peripheral-id", required=True)
    parser.add_argument("--companion-device-id-hex", required=True)
    parser.add_argument("--companion-gatt-secret-file", type=Path, required=True)
    parser.add_argument("--companion-tls-identity-label", required=True)
    parser.add_argument("--attacker-interface", required=True)
    parser.add_argument("--duration-seconds", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser


def _parse_netmask(mask: str) -> str | None:
    if mask.startswith("0x") or mask.startswith("0X"):
        if len(mask) != 10:
            return None
        try:
            value = int(mask, 16)
        except ValueError:
            return None
        return ".".join(
            str((value >> shift) & 0xFF) for shift in (24, 16, 8, 0)
        )

    if re.fullmatch(r"\d+\.\d+\.\d+\.\d+", mask):
        return mask
    return None


def _iter_assigned_sources(interface_text: str) -> dict[str, str]:
    assigned: dict[str, str] = {}
    pattern = re.compile(
        r"inet\s+(?P<ip>\d+\.\d+\.\d+\.\d+)\s+netmask\s+(?P<netmask>0x[0-9a-fA-F]{8}|\d+\.\d+\.\d+\.\d+)"
    )
    for match in pattern.finditer(interface_text):
        ip = match.group("ip")
        raw_mask = match.group("netmask")
        mask = _parse_netmask(raw_mask)
        if mask is None:
            continue
        assigned[ip] = mask
    return assigned


def _validate_interface(
    interface: str,
    address: str,
    netmask: str,
    *,
    ifconfig_runner: Callable[[str], subprocess.CompletedProcess] = None,
) -> list[str]:
    if ifconfig_runner is None:
        ifconfig_runner = lambda name: _run_command(["ifconfig", name])

    blockers: list[str] = []

    if not interface:
        blockers.append("invalid_interface")

    completed = ifconfig_runner(interface)
    if completed.returncode != 0:
        blockers.append("interface_lookup_failed")
        return blockers

    assigned = _iter_assigned_sources((completed.stdout or "") + (completed.stderr or ""))
    if not assigned:
        blockers.append("no_assigned_interface_addresses")

    normalized_mask = _parse_netmask(netmask)
    if normalized_mask is None:
        blockers.append("invalid_interface_netmask")

    if blockers:
        return blockers

    try:
        parsed = IPv4Address(address)
    except ValueError:
        blockers.append("invalid_interface_address")
        return blockers

    if parsed.is_loopback or parsed.is_unspecified or parsed.is_multicast:
        blockers.append("invalid_interface_address")

    if address not in assigned:
        blockers.append("interface_address_not_assigned")
    else:
        if assigned[address] != normalized_mask:
            blockers.append("interface_netmask_mismatch")

    routable = {
        ip
        for ip, assigned_mask in assigned.items()
        if ip_address(ip).is_global or ip_address(ip).is_private
    }

    if len(routable) < 17:
        blockers.append("insufficient_routable_source_addresses")

    if len(assigned) < 17:
        blockers.append("insufficient_assigned_source_addresses")

    return sorted(set(blockers))


def _read_hw_manifest(
    manifest_path: Path,
) -> tuple[dict[str, Any] | None, list[str]]:
    if not manifest_path.is_file():
        return None, ["missing_hardware_manifest"]

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return None, [f"invalid_hardware_manifest:{exc.__class__.__name__}"]

    blockers = list(validate_manifest(manifest))
    if manifest.get("chip_model") != "ESP32-S3":
        blockers.append("hardware_manifest_chip_not_esp32s3")
    if manifest.get("flash_bytes") != 8_388_608:
        blockers.append("hardware_manifest_flash_not_8mb")
    if manifest.get("psram_bytes") != 0:
        blockers.append("hardware_manifest_psram_not_zero")

    return manifest, blockers


def _is_esp32s3_candidate(candidate: Any) -> bool:
    values = [
        getattr(candidate, "description", ""),
        getattr(candidate, "manufacturer", ""),
        getattr(candidate, "product", ""),
        getattr(candidate, "device", ""),
    ]
    joined = " ".join(str(v).lower() for v in values)
    return getattr(candidate, "vid", None) == 0x303A or "esp32" in joined


def _select_port(
    *,
    list_ports: Callable[[], Iterable[Any]] = _default_list_ports,
    command_runner: Callable[..., subprocess.CompletedProcess] = _run_command,
) -> tuple[str | None, list[str]]:
    usb_candidates = [port for port in list_ports() if _is_esp32s3_candidate(port)]
    candidates: list[Any] = []
    for candidate in usb_candidates:
        path = getattr(candidate, "device", None)
        if not isinstance(path, str) or not path:
            continue
        chip_result = command_runner(
            ["esptool.py", "--chip", "auto", "--port", path, "chip_id"]
        )
        chip_text = (
            (chip_result.stdout or "") + (chip_result.stderr or "")
        ).lower()
        if chip_result.returncode != 0 or "esp32-s3" not in chip_text:
            continue

        flash_result = command_runner(
            ["esptool.py", "--chip", "esp32s3", "--port", path, "flash_id"]
        )
        flash_text = (
            (flash_result.stdout or "") + (flash_result.stderr or "")
        ).lower()
        if (
            flash_result.returncode == 0
            and re.search(r"detected flash size:\s*8\s*mb\b", flash_text)
        ):
            candidates.append(candidate)
    if len(candidates) != 1:
        if not candidates:
            return None, ["no_esp32s3_serial_candidates"]
        return None, ["multiple_esp32s3_serial_candidates"]

    port = candidates[0]
    path = getattr(port, "device", None)
    if not isinstance(path, str) or not path:
        return None, ["serial_port_invalid"]
    return path, []


def _scan_secret_markers(path: Path) -> list[str]:
    if not path.is_file():
        return ["missing_raw_artifact"]

    data = path.read_bytes()
    lowered = data.lower()
    errors: list[str] = []
    if PAIRING_CODE_RE.search(data):
        errors.append("pairing_code_revealed")
    for marker in MARKERS:
        if marker in lowered:
            errors.append(f"marker:{marker.decode('utf-8')}")
    if PRIVATE_KEY_RE.search(data):
        errors.append("private_key_revealed")
    return sorted(set(errors))


def _validate_companion_probe(
    probe: Path,
    *,
    command_runner: Callable[..., subprocess.CompletedProcess] = _run_command,
) -> list[str]:
    if not probe.exists():
        return ["missing_companion_probe"]
    if not os.access(probe, os.X_OK):
        return ["companion_probe_not_executable"]

    completed = command_runner([str(probe), "concurrency-hil-agent", "--help"])
    if completed.returncode != 0:
        return ["companion_probe_help_failed"]

    tokens = set(re.findall(r"--[a-z0-9-]+", (completed.stdout or "") + (completed.stderr or "")))
    missing = sorted(set(EXPECTED_COMPANION_OPTIONS) - tokens)
    extras = sorted(tokens - CONCURRENCY_HELP_OPTIONS)
    if missing:
        return ["companion_help_missing:" + ",".join(missing)]
    if extras:
        return ["companion_help_extra:" + ",".join(extras)]
    return []


def _validate_device_id_hex(device_id_hex: str) -> list[str]:
    if len(device_id_hex) != 32:
        return ["invalid_device_id_hex_length"]
    if re.fullmatch(r"[0-9a-fA-F]{32}", device_id_hex) is None:
        return ["invalid_device_id_hex"]
    return []


def _validate_peripheral_id(value: str) -> list[str]:
    try:
        uuid.UUID(value)
    except (TypeError, ValueError):
        return ["invalid_peripheral_id"]
    return []


def _validate_gatt_secret(path: Path) -> list[str]:
    if not path.is_file():
        return ["missing_gatt_secret_file"]
    mode = os.stat(path).st_mode & 0o777
    if mode != 0o600:
        return ["gatt_secret_not_mode_0600"]
    return []


def _validate_git_tree(command_runner: Callable[..., subprocess.CompletedProcess] = _run_command) -> list[str]:
    completed = command_runner(["git", "-C", str(REPO_ROOT), "status", "--porcelain"])
    if completed.returncode != 0:
        return ["git_check_failed"]
    if completed.stdout.strip():
        return ["git_tree_not_clean"]
    return []


def _validate_idf_locks(
    command_runner: Callable[..., subprocess.CompletedProcess] = _run_command,
) -> list[str]:
    blockers: list[str] = []
    toolchain_lock = REPO_ROOT / "toolchain.lock.json"
    dependencies_lock = REPO_ROOT / "firmware/dependencies.lock"
    idf_repo = REPO_ROOT / ".tools/esp-idf"

    try:
        toolchain = json.loads(toolchain_lock.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return ["invalid_toolchain_lock"]

    idf = toolchain.get("esp_idf")
    if not isinstance(idf, dict):
        blockers.append("toolchain_lock_missing_idf")
    else:
        if idf.get("tag") != EXPECTED_IDF_TAG:
            blockers.append("toolchain_lock_idf_tag_mismatch")
        if idf.get("commit") != EXPECTED_IDF_COMMIT:
            blockers.append("toolchain_lock_idf_commit_mismatch")

    try:
        dependencies_text = dependencies_lock.read_text(encoding="utf-8")
    except OSError:
        blockers.append("missing_firmware_dependencies_lock")
    else:
        if not re.search(
            r"(?m)^\s+idf:\n(?:.*\n)*?\s+version:\s+['\"]?5\.5\.4['\"]?\s*$",
            dependencies_text,
        ):
            blockers.append("firmware_dependencies_idf_mismatch")
        if not re.search(r"(?m)^target:\s+esp32s3\s*$", dependencies_text):
            blockers.append("firmware_dependencies_target_mismatch")

    completed = command_runner(
        ["git", "-C", str(idf_repo), "rev-parse", "HEAD"]
    )
    if completed.returncode != 0:
        blockers.append("idf_checkout_unavailable")
    elif completed.stdout.strip() != EXPECTED_IDF_COMMIT:
        blockers.append("idf_checkout_commit_mismatch")

    return sorted(set(blockers))


def _validate_output(output: Path) -> list[str]:
    if output.exists():
        return ["output_directory_not_fresh"]
    return []


def _validate_duration(duration: int) -> list[str]:
    if duration != DURATION_SECONDS:
        return ["duration_not_1800"]
    return []


def _validate_preflight(
    args: argparse.Namespace,
    *,
    list_ports: Callable[[], Iterable[Any]] = _default_list_ports,
    command_runner: Callable[..., subprocess.CompletedProcess] = _run_command,
    ifconfig_runner: Callable[[str], subprocess.CompletedProcess] | None = None,
) -> tuple[list[str], ConcurrencyContext | None]:
    blockers: list[str] = []
    blockers.extend(_validate_duration(args.duration_seconds))
    blockers.extend(_validate_output(args.output))

    if not args.firmware_bin.is_file():
        blockers.append("missing_firmware_bin")
        firmware_sha = ""
        firmware_len = 0
    else:
        firmware_sha = _sha256_file(args.firmware_bin)
        firmware_len = args.firmware_bin.stat().st_size

    manifest, manifest_blockers = _read_hw_manifest(args.hardware_manifest)
    blockers.extend(manifest_blockers)

    port, port_blockers = _select_port(
        list_ports=list_ports, command_runner=command_runner
    )
    blockers.extend(port_blockers)

    blockers.extend(_validate_companion_probe(args.companion_probe, command_runner=command_runner))
    blockers.extend(
        _validate_interface(
            args.companion_interface,
            args.companion_address,
            args.companion_netmask,
            ifconfig_runner=ifconfig_runner or (lambda name: command_runner(["ifconfig", name])),
        )
    )
    blockers.extend(_validate_peripheral_id(args.companion_peripheral_id))
    blockers.extend(_validate_device_id_hex(args.companion_device_id_hex))
    blockers.extend(_validate_gatt_secret(args.companion_gatt_secret_file))

    if not args.companion_tls_identity_label.strip():
        blockers.append("missing_companion_tls_identity_label")

    if args.attacker_interface != "auto":
        blockers.append("invalid_attacker_interface")

    blockers.extend(_validate_git_tree(command_runner=command_runner))
    blockers.extend(_validate_idf_locks(command_runner=command_runner))

    blockers = sorted(set(blockers))
    if blockers or port is None or not manifest:
        return blockers, None

    return blockers, ConcurrencyContext(
        run_id=uuid.uuid4().hex,
        firmware_bin=args.firmware_bin,
        firmware_sha256=firmware_sha,
        firmware_bytes=firmware_len,
        manifest=manifest,
        serial_port=port,
        companion_probe=args.companion_probe,
        companion_interface=args.companion_interface,
        companion_address=args.companion_address,
        companion_netmask=args.companion_netmask,
        companion_peripheral_id=args.companion_peripheral_id,
        companion_device_id_hex=args.companion_device_id_hex,
        companion_device_id_sha256=_sha256_bytes(bytes.fromhex(args.companion_device_id_hex)),
        companion_gatt_secret_file=args.companion_gatt_secret_file,
        companion_tls_identity_label=args.companion_tls_identity_label,
        duration_seconds=args.duration_seconds,
        output=args.output,
    )


def _build_incomplete_report(run_id: str, blockers: list[str]) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "capture_complete": False,
        "run": {"run_id": run_id},
        "blockers": blockers,
        "consistency_errors": [],
    }


def _write_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps(report, indent=2, sort_keys=True) + "\n"
    with path.open("w", encoding="utf-8") as fp:
        fp.write(data)
        fp.flush()
        os.fsync(fp.fileno())


def _write_jsonl_lines(
    path: Path,
    lines: Iterable[str],
    *,
    clock: Callable[[], int] = monotonic_ns,
) -> tuple[int, int]:
    path.parent.mkdir(parents=True, exist_ok=True)
    first: int | None = None
    last: int = 0

    with path.open("w", encoding="utf-8") as fp:
        for line in lines:
            now = clock()
            fp.write(line)
            if not line.endswith("\n"):
                fp.write("\n")
            fp.flush()
            os.fsync(fp.fileno())
            if first is None:
                first = now
            last = now

    if first is None:
        first = clock()
        last = first
    return first, last


def _artifact_reference(
    path: Path, first_ns: int, last_ns: int, *, output_root: Path
) -> ArtifactMetadata:
    digest = _sha256_file(path)
    try:
        relative_path = path.resolve().relative_to(output_root.resolve())
    except ValueError as exc:
        raise RuntimeError("artifact_outside_output_root") from exc
    return ArtifactMetadata(
        path=str(relative_path),
        sha256=digest,
        byte_length=path.stat().st_size,
        first_runner_receipt_ns=first_ns,
        last_runner_receipt_ns=last_ns,
    )


def _canonical_json_line(line: str) -> dict[str, Any]:
    stripped = line.strip("\r\n")
    if stripped != line.strip():
        raise ValueError("non_canonical_json_whitespace")
    event = json.loads(stripped)
    if not isinstance(event, dict):
        raise ValueError("companion_event_not_object")
    canonical = json.dumps(event, separators=(",", ":"), sort_keys=True)
    if canonical != stripped:
        raise ValueError("non_canonical_json")
    return event


def _extract_app_elf_sha256(output: str) -> str:
    patterns = (
        re.compile(r"elf\s+file\s+sha256\s*[:=]\s*([0-9a-f]{64})", re.IGNORECASE),
        re.compile(r"app[_-]?elf[_-]?sha(?:256)?\s*[:=]\s*([0-9a-f]{64})", re.IGNORECASE),
        re.compile(r"app(?:lication)?\s+sha(?:256)?\s*[:=]\s*([0-9a-f]{64})", re.IGNORECASE),
        re.compile(r"sha256\s*[:=]\s*([0-9a-f]{64})", re.IGNORECASE),
    )

    for pattern in patterns:
        matched = pattern.search(output)
        if matched:
            return matched.group(1)
    raise RuntimeError("app_elf_sha256_missing")


def _backup_flash(
    *,
    serial_port: str,
    output: Path,
    command_runner: Callable[..., subprocess.CompletedProcess] = _run_command,
) -> tuple[Path, str]:
    backup_path = output / "raw" / "flash-backup.bin"
    backup_path.parent.mkdir(parents=True, exist_ok=True)
    completed = command_runner(
        [
            "esptool.py",
            "--chip",
            "esp32s3",
            "--port",
            serial_port,
            "read_flash",
            "0x0",
            f"0x{FLASH_SIZE_BYTES:x}",
            str(backup_path),
        ],
    )
    if completed.returncode != 0:
        raise RuntimeError("flash_backup_command_failed")

    if not backup_path.is_file():
        raise RuntimeError("flash_backup_missing")

    with backup_path.open("rb") as fp:
        os.fsync(fp.fileno())

    length = backup_path.stat().st_size
    if length != FLASH_SIZE_BYTES:
        raise RuntimeError("flash_backup_length_mismatch")

    return backup_path, _sha256_file(backup_path)


def _read_app_sha256(
    firmware_bin: Path,
    *,
    command_runner: Callable[..., subprocess.CompletedProcess] = _run_command,
) -> str:
    completed = command_runner(["esptool.py", "image_info", str(firmware_bin)])
    if completed.returncode != 0:
        raise RuntimeError("esptool_image_info_failed")
    return _extract_app_elf_sha256((completed.stdout or "") + (completed.stderr or ""))


def _flash_firmware(
    *,
    firmware_bin: Path,
    serial_port: str,
    command_runner: Callable[..., subprocess.CompletedProcess] = _run_command,
) -> subprocess.CompletedProcess:
    return command_runner(
        [
            "esptool.py",
            "--chip",
            "esp32s3",
            "--port",
            serial_port,
            "write_flash",
            f"0x{APP_PARTITION_OFFSET:x}",
            str(firmware_bin),
        ]
    )


def _parse_device_id_sha256(device_id_hex: str) -> str:
    return _sha256_bytes(bytes.fromhex(device_id_hex))


class ConcurrencyCompanionSession:
    def __init__(
        self,
        *,
        probe: Path,
        interface: str,
        interface_address: str,
        interface_netmask: str,
        peripheral_id: str,
        device_id_hex: str,
        run_id: str,
        boot_id: str,
        app_elf_sha256: str,
        firmware_image_sha256: str,
        duration_seconds: int,
        gatt_secret_file: Path,
        tls_identity_label: str,
        raw_event_path: Path,
        clock: Callable[[], int] = monotonic_ns,
        popen_factory: Callable[..., subprocess.Popen[str]] = subprocess.Popen,
        stderr_sink: Callable[[str], None] | None = None,
    ) -> None:
        self._probe = probe
        self._interface = interface
        self._interface_address = interface_address
        self._interface_netmask = interface_netmask
        self._peripheral_id = peripheral_id
        self._device_id_hex = device_id_hex
        self._device_id_sha256 = _parse_device_id_sha256(device_id_hex)
        self._run_id = run_id
        self._boot_id = boot_id
        self._app_elf_sha256 = app_elf_sha256
        self._firmware_image_sha256 = firmware_image_sha256
        self._duration_seconds = duration_seconds
        self._gatt_secret_file = gatt_secret_file
        self._tls_identity_label = tls_identity_label
        self._clock = clock
        self._popen_factory = popen_factory
        self._stderr_sink = stderr_sink or (lambda _: None)
        self._raw_event_path = raw_event_path
        self._process: subprocess.Popen[str] | None = None

    @property
    def command(self) -> list[str]:
        return [
            str(self._probe),
            "concurrency-hil-agent",
            "--interface",
            self._interface,
            "--interface-address",
            self._interface_address,
            "--interface-netmask",
            self._interface_netmask,
            "--peripheral-id",
            self._peripheral_id,
            "--device-id-hex",
            self._device_id_hex,
            "--gatt-secret-file",
            str(self._gatt_secret_file),
            "--tls-identity-label",
            self._tls_identity_label,
            "--run-id",
            self._run_id,
            "--boot-id",
            self._boot_id,
            "--app-elf-sha256",
            self._app_elf_sha256,
            "--probe-firmware-sha256",
            self._firmware_image_sha256,
            "--duration-seconds",
            str(self._duration_seconds),
        ]

    @property
    def expected(self) -> dict[str, str]:
        return {
            "run_id": self._run_id,
            "boot_id": self._boot_id,
            "app_elf_sha256": self._app_elf_sha256,
            "firmware_image_sha256": self._firmware_image_sha256,
            "device_id_sha256": self._device_id_sha256,
        }

    def _redact(self, text: str) -> str:
        redacted = text
        redacted = PAIRING_CODE_RE.sub("[REDACTED]", redacted)
        for marker in (b"cp_admin=", b"x-csrf-token", b"wifi_credential", b"wifi_password"):
            redacted = redacted.replace(marker.decode("ascii"), "[REDACTED]")
            redacted = redacted.replace(marker.decode("ascii").lower(), "[REDACTED]")
        return redacted

    def run(self) -> tuple[list[dict[str, Any]], list[str], ArtifactMetadata | None]:
        events: list[dict[str, Any]] = []
        errors: list[str] = []
        process = self._popen_factory(
            self.command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self._process = process

        ready_seen = False
        stopped_seen = False
        heartbeat_seen = False
        first_event_ns: int | None = None
        last_event_ns: int | None = None
        last_heartbeat_ns: int | None = None
        last_seen_boot = self._boot_id
        raw_lines: list[str] = []

        try:
            if process.stdout is None or process.stderr is None:
                raise RuntimeError("companion pipes unavailable")

            previous_event_ns: int | None = None
            ready_ns: int | None = None

            for raw_line in iter(process.stdout.readline, ""):
                observed_ns = self._clock()
                first_event_ns = observed_ns if first_event_ns is None else first_event_ns

                if previous_event_ns is not None and observed_ns - previous_event_ns > 5_000_000_000:
                    errors.append("event_gap_exceeded")
                previous_event_ns = observed_ns

                try:
                    event = _canonical_json_line(raw_line)
                    if raw_line and raw_line[-1] != "\n":
                        raise ValueError("noncanonical_missing_newline")

                    for key, expected_value in self.expected.items():
                        if event.get(key) != expected_value:
                            errors.append(f"identity_mismatch:{key}")

                    if event.get("producer") != "macos_companion":
                        errors.append("wrong_producer")

                    if event.get("kind") == "ready":
                        if ready_seen:
                            errors.append("duplicate_ready")
                        ready_seen = True
                        heartbeat_seen = False
                        last_heartbeat_ns = observed_ns
                        ready_ns = observed_ns
                        if event.get("boot_id") != self._boot_id:
                            errors.append("boot_id_changed")
                    elif event.get("kind") == "stopped":
                        if not ready_seen:
                            errors.append("stopped_before_ready")
                        if stopped_seen:
                            errors.append("duplicate_stopped")
                        stopped_seen = True
                    elif event.get("kind") == "heartbeat":
                        if not ready_seen:
                            errors.append("heartbeat_before_ready")
                        if last_heartbeat_ns is not None and observed_ns - last_heartbeat_ns > 5_000_000_000:
                            errors.append("heartbeat_gap_exceeded")
                        heartbeat_seen = True
                        last_heartbeat_ns = observed_ns
                    elif event.get("kind") == "interface_changed":
                        errors.append("interface_changed_detected")
                    else:
                        if not ready_seen:
                            errors.append("event_before_ready")

                    if event.get("boot_id") != self._boot_id:
                        errors.append("boot_id_changed")

                    if event.get("kind") not in {"ready", "heartbeat", "stopped"} and event.get(
                        "boot_id"
                    ) != self._boot_id:
                        errors.append("boot_id_changed")

                    if "observed_at_ns" in event:
                        errors.append("child_supplied_observed_at_ns")
                    event["observed_at_ns"] = observed_ns
                    validation_errors = [
                        issue.message
                        for issue in sorted(
                            CompanionValidator.iter_errors(event), key=lambda item: list(item.path)
                        )
                    ]
                    errors.extend(validation_errors)

                    if validation_errors:
                        raise ValueError(validation_errors[0])

                    events.append(event)
                    raw_lines.append(
                        json.dumps(event, separators=(",", ":"), sort_keys=True)
                    )

                except (json.JSONDecodeError, ValueError) as exc:
                    errors.append(str(exc))

                if ready_ns is not None and not heartbeat_seen and observed_ns - ready_ns > 5_000_000_000:
                    errors.append("heartbeat_timeout")

                if ready_seen and last_heartbeat_ns is not None:
                    if observed_ns - last_heartbeat_ns > 5_000_000_000:
                        errors.append("heartbeat_gap_exceeded")

            while True:
                raw = process.stderr.readline()
                if not raw:
                    break
                self._stderr_sink(self._redact(raw))

            rc = process.wait(timeout=1)
            if rc != 0:
                errors.append("companion_exit_non_zero")
            if not ready_seen:
                errors.append("missing_ready")
            if not heartbeat_seen:
                errors.append("missing_heartbeat")
            if not stopped_seen:
                errors.append("missing_stopped")

        finally:
            if process is not None and process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    process.kill()

        if not raw_lines:
            return events, errors, None

        first_ns, last_ns = _write_jsonl_lines(
            self._raw_event_path, raw_lines, clock=self._clock
        )
        artifact = _artifact_reference(
            self._raw_event_path,
            first_ns,
            last_ns,
            output_root=self._raw_event_path.parents[1],
        )
        return events, errors, artifact


def _run_main_flow(
    args: argparse.Namespace,
    *,
    list_ports: Callable[[], Iterable[Any]] = _default_list_ports,
    command_runner: Callable[..., subprocess.CompletedProcess] = _run_command,
    ifconfig_runner: Callable[[str], subprocess.CompletedProcess] | None = None,
    report_writer: Callable[[Path, dict[str, Any]], None] = _write_report,
    is_tty: Callable[[], bool] = sys.stdin.isatty,
) -> tuple[int, dict[str, Any], list[str]]:
    run_id = uuid.uuid4().hex
    report_path = args.output / "report.json"

    preflight_blockers, context = _validate_preflight(
        args,
        list_ports=list_ports,
        command_runner=command_runner,
        ifconfig_runner=ifconfig_runner,
    )

    if context is None:
        report = _build_incomplete_report(run_id, preflight_blockers)
        report_writer(report_path, report)
        return 1, report, preflight_blockers

    # The approved CLI has no Cardputer Web endpoint parameter. Do not guess a
    # host, prompt for identity or request a code that cannot be consumed. This
    # committed version is intentionally preflight-only and cannot reach any
    # flash helper until mDNS/TLS pairing and live evidence aggregation exist.
    blockers = [
        "web_pairing_tls_flow_unavailable"
        if is_tty()
        else "pairing_code_noninteractive"
    ]
    report = _build_incomplete_report(context.run_id, blockers)
    report_writer(report_path, report)
    return 1, report, blockers


def _safe_unlink_secret(secret_path: Path) -> None:
    if secret_path.exists():
        secret_path.unlink()


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)

    if not args.auto_port:
        print("--auto-port is required")
        return 2

    try:
        return_code, _report, _blockers = _run_main_flow(args)
        return return_code
    except KeyboardInterrupt:
        report = _build_incomplete_report(
            uuid.uuid4().hex,
            ["keyboard_interrupt"],
        )
        (args.output / "report.json").parent.mkdir(parents=True, exist_ok=True)
        _write_report(args.output / "report.json", report)
        return 1
    except Exception as exc:
        report = _build_incomplete_report(
            uuid.uuid4().hex,
            [f"runner_exception:{exc.__class__.__name__}"],
        )
        _write_report(args.output / "report.json", report)
        return 1
    finally:
        _safe_unlink_secret(args.companion_gatt_secret_file)


__all__ = [
    "EvidenceClock",
    "validate_continuous_window",
    "validate_hid_measurement",
]


if __name__ == "__main__":
    raise SystemExit(main())
