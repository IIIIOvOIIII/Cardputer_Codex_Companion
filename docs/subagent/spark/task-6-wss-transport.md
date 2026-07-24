You are implementing Firmware Task 6 in
`/Users/nicholasliao/clawd/Cardputer_Codex_Companion`.

Read this first; it is the exact task contract:
`.superpowers/sdd/task-firmware-6-brief.md`

This task follows completed firmware Tasks 1-5. Reuse the generated
`phase0_protocol_vectors.hpp` WSS contract and existing protocol codec. Use
only public APIs from the pinned ESP-IDF 5.5.4 checkout in `.tools/esp-idf`.

Scope only:

- `firmware/main/probe/pinned_wss_transport.hpp`
- `firmware/main/probe/pinned_wss_transport.cpp`
- `firmware/main/probe/probe_controller.cpp`
- `firmware/main/CMakeLists.txt`
- `firmware/test/host/CMakeLists.txt`
- `firmware/test/host/test_wss_contract.cpp`

Requirements:

- Follow TDD: add the WSS contract test first and capture the expected RED
  failure before implementation.
- Implement exactly Task 6, including constant-time SPKI comparison, public
  peer-certificate/SPKI/exporter APIs, external WebSocket transport ownership,
  canonical generated auth encoding/signature verification, and
  connection-generation authentication state.
- Do not use private ESP transport types, private headers, `MBEDTLS_PRIVATE`,
  skip-verification flags, hand-authored exporter labels, or secret logging.
- Keep exporter and signatures volatile; do not persist or log them.
- Preserve existing Task 1-5 behavior and avoid adjacent cleanup.
- If a required public API cannot compile, stop and report BLOCKED with the
  exact compiler/API evidence rather than using a private workaround.
- Do not commit or push. The main session owns Git history and review.

Verification:

1. `cmake -S firmware/test/host -B build/phase0/firmware-host`
2. `cmake --build build/phase0/firmware-host -j4`
3. `ctest --test-dir build/phase0/firmware-host -R wss_contract --output-on-failure`
4. `scripts/phase0/idf.sh -C firmware build`
5. `if rg -n 'transport_esp_tls_t|priv_include|MBEDTLS_PRIVATE' firmware/main/probe/pinned_wss_transport.cpp; then exit 1; fi`
6. `if rg -n 'EXPORTER-|pair-root|pairing-root|gatt-auth|pairing-sas' firmware/main/probe/pinned_wss_transport.cpp; then exit 1; fi`

Write a concise implementation summary, RED/GREEN evidence, files changed,
verification results, and concerns to the result file supplied by the runner.
