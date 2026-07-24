You are implementing only the runner half of Firmware Task 8 in:
/Users/nicholasliao/clawd/Cardputer_Codex_Companion

Read first:
- docs/superpowers/plans/2026-07-24-phase0-firmware-concurrency.md
  (all of Task 8)
- scripts/phase0/capture_hardware_manifest.py
- tools/phase0/validate_hardware_manifest.py
- tools/phase0/validate_concurrency_report.py
- tools/phase0/tests/test_concurrency_report.py
- protocol/phase0/firmware-concurrency-report.schema.json
- protocol/phase0/companion-probe-event.schema.json

Scope only:
- Create scripts/phase0/run_concurrency_hil.py
- Create tools/phase0/tests/test_run_concurrency_hil.py

Do not modify validators, schemas, firmware, docs, dependencies, or unrelated
tests in this task. Do not commit or push.

Requirements:
1. Implement the exact Task 8 CLI options from Step 3. Do not add interface
   aliases, guess Companion parameters, invoke `pair-gatt-hil`, or import prior
   output.
2. Re-export `EvidenceClock`, `validate_continuous_window`, and
   `validate_hid_measurement` from the validator so the Task 8 Step 1 import
   works.
3. Model the exact 1800-second schedule as immutable data:
   warmup 0-119, steady 120-599, transient 600-899, attack 900-1679,
   recovery 1680-1799. Reject gate duration other than 1800.
4. Implement fail-closed preflight before any flash:
   - output directory must be fresh (never merge prior evidence);
   - firmware image exists and is hashed;
   - hardware manifest validates and says ESP32-S3, exactly 8MiB, no PSRAM;
   - exactly one ESP32-S3 USB serial candidate is identified for --auto-port;
   - Companion executable exists and its help advertises the exact
     concurrency-hil-agent option set;
   - explicit interface/address/netmask are valid; parse only already assigned
     addresses and require at least 17 distinct routable sources without
     changing aliases;
   - peripheral UUID and 16-byte device ID hex are valid;
   - GATT secret is a regular mode-0600 file;
   - TLS identity label is nonempty;
   - Git tree is clean.
5. When preflight cannot proceed (the expected current-machine path), write a
   schema-valid `report.json` with `capture_complete=false`, a generated run_id,
   only known run identity fields, deterministic machine-readable blockers,
   no invented boot/device values, and return nonzero. Use the schema/validator
   contract now committed in 5aea871.
6. Destructive-action barrier helpers must:
   - display only SHA-256 of the explicit device ID and require exact typeback;
   - read all 8MiB flash to a fresh backup, fsync it, hash it, and verify length
     before calling flash;
   - verify image SHA and embedded app ELF SHA from public esptool output;
   - flash exactly once; do not reset/reflash during capture.
   Tests must prove flash is never called if any prior barrier fails.
7. Implement the Companion child lifecycle as a testable class:
   - invoke only `concurrency-hil-agent` with runner-owned run/boot/app/image/
     device identity and all explicit Companion parameters;
   - stdout only is canonical JSONL measurement input; attach
     `time.monotonic_ns()` receipt time and validate each event against the
     Companion schema;
   - require exactly one ready, heartbeat within five seconds and no gap over
     five seconds, one stopped, exit 0;
   - reject malformed/non-canonical JSON, identity mismatch, repeat
     ready/stopped, early exit, and instance/boot change;
   - stderr is diagnostic only and must be redacted.
8. Pairing code input must use `getpass.getpass()` and be passed to the live
   TLS operation only in process memory. It must never appear in argv,
   environment, output files, report, exceptions, or logs. Noninteractive
   operation becomes a blocker.
9. In a finally block, terminate a still-running child and unlink the GATT
   secret on success, failure, blocker, exception, or KeyboardInterrupt.
10. Implement raw JSONL artifact writer/hash metadata helpers using local
    monotonic receipt times and fsync. Redaction scanning must happen before a
    complete report can be written.
11. The full live orchestration may fail closed with a precise blocker when a
    required real protocol event or Companion capability is unavailable, but
    it must never mark capture_complete true from synthetic/default values.
12. Tests must be deterministic and mock serial ports, esptool/subprocess,
    getpass, monotonic clocks, ifconfig, and Companion stdout. Cover:
    - the exact Step 1 mismatch and HID error examples;
    - no/one/two ports;
    - duration and 17-address boundary;
    - manifest/secret/executable/help failures;
    - typeback and backup barriers before flash;
    - companion ready/heartbeat/stopped happy path and every fail-closed case;
    - secret unlink on success/failure/blocker/interrupt;
    - incomplete report schema-valid and contains no fake boot/device values;
    - pairing code absent from argv/env/files/errors.

Keep OS/process side effects behind small injectable functions/classes. Never
touch a real serial port from unit tests.

After coding, run exactly:
uv run pytest tools/phase0/tests/test_run_concurrency_hil.py -q

Then run:
git diff --check

Report tests and changed files. Do not commit or push.
