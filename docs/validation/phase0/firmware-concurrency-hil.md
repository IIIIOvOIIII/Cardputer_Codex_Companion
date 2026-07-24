# Phase 0 Firmware Concurrency HIL Evidence

- Test date: 2026-07-24 15:04 HKT
- Runner commit: `4158f4b528a6586eb01acfbcc44dd58947ee6663`
- Firmware image: `firmware/build/cardputer_codex_phase0.bin`
- Firmware image SHA-256: `62f7bf560d249ac4f86d2d3b1e81bc498c886f4dfac61d06c10e998e80657df9`
- Raw report: `build/phase0/firmware-concurrency/report.json`
- Raw report SHA-256: `a8d19d3bf332cfea7487abe8c9918ea447232f0193d826c347fe81c506ef660d`
- Capture complete: `false`
- Measurement window: not started
- Flash read/write performed: no

## Validation result

The committed runner was invoked from a clean Git tree with the fixed
`1800`-second duration. It emitted a schema-valid, verdict-free incomplete
report and exited non-zero. The report was then accepted by
`tools/phase0/validate_concurrency_report.py` as an incomplete capture.

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
