You are implementing Firmware Task 7 in
`/Users/nicholasliao/clawd/Cardputer_Codex_Companion`.

Read this first; it is the exact task contract:
`.superpowers/sdd/task-firmware-7-brief.md`

Base commit is `83563ed`. Tasks 1-6 are complete. Preserve their security and
concurrency boundaries, especially the scanner/HID dependency scan, bounded
HTTPS admission, streaming Web handlers, pinned WSS transport ownership, and
immutable `ProbeIdentity`.

Primary scope is exactly the files named in the Task 7 brief. You may modify a
matching header only when required to expose a Task 7 metric/counter already
implemented in its `.cpp`; name and justify every such adjacent header in the
report. Do not perform unrelated cleanup.

Requirements:

- Follow TDD: add `test_resource_metrics.cpp` and its CMake target first,
  capture the missing-type RED, then implement.
- Implement the 1002-bucket, allocation-free, 100-microsecond ceiling HID
  histogram and nearest-rank p95 exactly. Failed enqueue must not fabricate a
  latency sample.
- Enforce all inclusive resource, stack, HID, and exact five-second transient
  thresholds from the brief. Return typed errors; do not serialize gate
  verdicts.
- Use fixed queue depths HID 32, network 16, display 8. Every enqueue is
  zero-wait and increments its own overflow counter. Do not introduce network,
  display, NVS, HTTP, Wi-Fi, or TLS dependencies into keyboard/HID sources.
- Register the ESP failed-allocation callback before BLE startup. Sample only
  internal 8-bit heap. Treat `uxTaskGetStackHighWaterMark2()` as bytes on the
  pinned ESP-IDF port.
- Instrument existing real boundaries. When a named long-lived task does not
  yet exist in this probe, represent it honestly as unavailable; do not invent
  a task handle, fabricate measurements, or start a fake workload.
- WSS working memory is 4096 bytes, config import streaming is 1024 bytes, and
  transient counters must not retain payloads.
- JSONL uses one fixed 4096-byte buffer, includes the identity quintet and all
  required numeric measurements, excludes raw device ID, source addresses,
  pairing/session credentials, request bodies, exporter/signatures, and text
  payloads. Truncation increments `metrics_encode_failure`.
- Preserve the fixed 8 MiB/no-PSRAM target and avoid heap allocation in the HID
  hot path and evidence encoder.
- Do not commit or push. The main session owns Git history and review.

Verification:

1. `cmake -S firmware/test/host -B build/phase0/firmware-host`
2. `cmake --build build/phase0/firmware-host -j4`
3. `ctest --test-dir build/phase0/firmware-host -R resource_metrics --output-on-failure`
4. `if rg -l 'esp_tls|esp_http|esp_wifi|M5Display|nvs_' firmware/main/probe/keyboard_probe.cpp firmware/main/probe/hid_engine.cpp; then exit 1; fi`
5. `scripts/phase0/idf.sh -C firmware build`
6. `git diff --check`

Write a concise implementation summary, exact RED/GREEN evidence, files
changed, verification results, unavailable runtime inputs, and concerns to the
result file supplied by the runner.
