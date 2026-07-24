Repair and complete the in-progress Firmware Task 7 implementation in
`/Users/nicholasliao/clawd/Cardputer_Codex_Companion`.

Read only these instructions plus `.superpowers/sdd/task-firmware-7-brief.md`.
The working tree contains an incomplete prior attempt. Do not discard useful
work and do not alter unrelated files. Do not print full files or a full diff
to output; use focused reads and concise test output so you do not exhaust the
context window. Do not commit or push.

Known defects that must be fixed:

- Host CMake does not compile/link `resource_metrics.cpp` or provide its
  include directory.
- `keyboard_probe.hpp` exposes `TaskHandle_t` outside `ESP_PLATFORM`, declares
  unused/missing methods, still uses a heap-allocating vector in the HID hot
  path, and its target lifecycle/storage must compile cleanly.
- `web_handlers.hpp` uses `kNetworkQueueDepth` without including the metrics
  contract. The 16-slot static request queue and overflow counter must remain
  zero-wait and thread-safe.
- The JSONL encoder must be allocation-free, valid JSON, include all five
  evidence identity fields (label the fifth as a digest), detect every
  truncation, never pass a `uint64_t` to a `%u` variadic format, and never
  emit the forbidden payload/secret fields. It currently has unchecked hex
  writes and mismatched object braces.
- Add host tests for histogram edge buckets/overflow, all threshold failures,
  stack unavailable/threshold, all burst errors, valid encoder shape and
  truncation. Keep the original required assertions.
- Finish Task 7 target instrumentation: failed-allocation callback registered
  before BLE startup; internal 8-bit heap and largest-block snapshot;
  HTTPS admission occupancy; stack high-water sampling in bytes; fixed
  4096-byte evidence line; explicit encode-failure counter.
- The current probe lacks real scanner/NimBLE/HTTPS/WSS/display runtime handles
  and lacks a runner-supplied complete `ProbeIdentity`. Represent these inputs
  as unavailable and do not emit a sample until a complete identity is
  supplied; do not invent handles, hashes, workloads, or success values.
- Keep WSS client buffer 4096 and config import buffer 1024. Add counter hooks
  at existing real Web/WSS boundaries only when semantically observable; do
  not infer WebSocket frames from raw TLS reads.
- Fix all whitespace and compile warnings introduced by Task 7.

Verification:

1. `cmake -S firmware/test/host -B build/phase0/firmware-host`
2. `cmake --build build/phase0/firmware-host -j4`
3. `ctest --test-dir build/phase0/firmware-host -R resource_metrics --output-on-failure`
4. `if rg -l 'esp_tls|esp_http|esp_wifi|M5Display|nvs_' firmware/main/probe/keyboard_probe.cpp firmware/main/probe/hid_engine.cpp; then exit 1; fi`
5. `scripts/phase0/idf.sh -C firmware build`
6. `git diff --check`

Write only a concise result summary to the runner result file: changed files,
RED/GREEN evidence, exact tests, unavailable runtime inputs, and concerns.
