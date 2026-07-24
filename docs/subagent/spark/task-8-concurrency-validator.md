You are implementing only the validation/schema half of Firmware Task 8 in:
/Users/nicholasliao/clawd/Cardputer_Codex_Companion

Read first:
- docs/superpowers/plans/2026-07-24-phase0-firmware-concurrency.md
  (Task 8, especially Steps 1, 4, 7, 8, and 10)
- tools/phase0/validate_concurrency_report.py
- tools/phase0/tests/test_concurrency_report.py
- protocol/phase0/firmware-concurrency-report.schema.json
- protocol/phase0/companion-probe-event.schema.json

Scope only:
- tools/phase0/validate_concurrency_report.py
- tools/phase0/tests/test_concurrency_report.py
- protocol/phase0/firmware-concurrency-report.schema.json

Do not modify the runner in this task. A separate Spark task will consume the
validator contract. Do not modify unrelated tests or protocols. Do not commit
or push.

Requirements:
1. Preserve the existing public functions and their exact current behavior:
   `same_run_errors()` and `forbidden_verdict_fields()`.
2. Add frozen `EvidenceClock` and exact `validate_continuous_window()` behavior
   from Task 8 Step 4, including identity quintet equality and >=60-second
   receipt-clock overlap.
3. Add `validate_hid_measurement()` with deterministic ordered errors. It must
   require generated>=10000, generated==queued, queue_failures==0,
   overflow_samples==0, p95_upper_bound_us<=20000, and
   release_all_observed==true.
4. Add deterministic validation for Task 8 resource measurements:
   steady free internal >=65536, steady largest block >=32768,
   transient and attack free internal >=40960, allocation failures and metrics
   encode failures zero, all seven named task stack samples meeting
   max(configured/5,1024), HTTPS occupancy exactly established=4/pending=1,
   and the exact 5,000,000us burst counts.
5. Add report validation that:
   - applies Draft 2020-12 schema;
   - rejects all nested forbidden child-verdict fields;
   - checks evidence clock same identity and overlap;
   - verifies every referenced raw artifact path is relative, remains under the
     report directory, has exact byte length and SHA-256, and carries sane
     first/last runner receipt times;
   - validates redaction markers for an 8-digit pairing code, `cp_admin=`,
     `X-CSRF-Token`, PEM private key, Wi-Fi credential markers, exporter bytes,
     request bodies, and text payloads;
   - leaves threshold failures in `consistency_errors`; never invents a gate
     verdict.
6. Turn `validate_concurrency_report.py` into a real CLI accepting one report
   path. It should print `capture_complete=<true|false>` plus measurement and
   artifact counts. Exit 0 only for a schema-valid, capture-complete,
   blocker-free, consistency-error-free report; return nonzero otherwise.
7. Evolve the report schema to model the Task 8 raw measurements, attack
   matrix rows (numeric field must be `http_status_code`, never a verdict field
   named `status`), artifact metadata, and evidence clocks. It must permit a
   machine-readable, schema-valid incomplete preflight report with
   `capture_complete=false` and blockers, while requiring the strict fields
   when `capture_complete=true`.
8. Expand tests with both good and negative fixtures, including:
   - exactly 60 seconds overlap passes;
   - one digest/boot mismatch fails;
   - queue loss fails despite low p95;
   - threshold boundary inclusivity;
   - artifact hash/length/path traversal/redaction failures;
   - nested status-like verdict rejection;
   - schema accepts an incomplete blocker report;
   - CLI success and incomplete nonzero behavior.
9. Tests must be deterministic, use tmp_path for artifacts, and contain no real
   credentials.

Keep the implementation readable and fail closed. Do not weaken existing
schema tests merely to make fixtures pass.

After coding, run exactly:
uv run pytest tools/phase0/tests/test_concurrency_report.py -q

Then run:
git diff --check

Report tests and changed files. Do not commit or push.
