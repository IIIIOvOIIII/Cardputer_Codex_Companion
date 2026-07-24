# Phase 0 Firmware Concurrency HIL Evidence

- Test date: 2026-07-24 15:04 HKT
- Runner commit: `2415e96cce007eb293229e283ddb05cba696a2f5`
- Firmware image: `firmware/build/cardputer_codex_phase0.bin`
- Firmware image SHA-256: `62f7bf560d249ac4f86d2d3b1e81bc498c886f4dfac61d06c10e998e80657df9`
- Raw report: `build/phase0/firmware-concurrency-preflight-2415e96/report.json`
- Raw report SHA-256: `4eb9c223af5b67bb948966e8187778d275dc3777afd0ec9789d2df4997e4d616`
- Capture complete: `false`
- Measurement window: not started
- Flash read/write performed: no

## Validation result

The committed runner was invoked from a clean Git tree with the fixed
`1800`-second duration. It emitted a schema-valid, verdict-free incomplete
report and exited non-zero. The report was then accepted by
`tools/phase0/validate_concurrency_report.py` as an incomplete capture.

The reviewed runner is deliberately preflight-only. It cannot prompt for
device identity or pairing data, back up flash, flash firmware, or invent a
runtime `boot_id` until the mDNS/TLS Web pairing flow and full live evidence
aggregator are implemented and reviewed.

The only files under the output directory are the report itself. No flash
backup or raw runtime evidence exists because preflight stopped before any
destructive action.

## Blockers

- `no_esp32s3_serial_candidates`
- `missing_hardware_manifest`
- `missing_companion_probe`
- `missing_gatt_secret_file`
- `insufficient_assigned_source_addresses`
- `insufficient_routable_source_addresses`

The machine currently exposes no Cardputer/ESP32-S3 serial device, and the
selected LAN interface has one assigned IPv4 address rather than the required
17. The macOS concurrency agent and its run-scoped secret are also unavailable.
Consequently, there is no 30-minute measurement, hardware manifest digest,
runtime application ELF digest, threshold extrema, HID sample, attack matrix,
or artifact set to report.

This incomplete capture cannot be combined with evidence from a later run. A
new output directory and one continuous live run are required after every
blocker above is resolved.
