# Cardputer microphone diagnostics

These two probes isolate Cardputer microphone input from the product BLE,
encoding, and Mac audio paths:

- `cardputer_mic_probe` uses ESP-IDF 5.5.4 and M5Unified 0.2.17. It restores
  GPIO46 to input-without-pulls before starting `M5.Mic`, then prints
  per-frame PCM level and variation metrics.
- `cardputer_mic_probe_arduino` uses Arduino ESP32 2.0.x through PlatformIO
  with the same M5Unified version as a legacy-driver cross-check.

Build the ESP-IDF probe from the repository root:

```bash
scripts/phase0/idf.sh -C tools/diagnostics/cardputer_mic_probe build
```

When testing a configured product device, write only
`cardputer_mic_probe.bin` to the existing product app partition at `0x20000`.
Do not flash the probe bootloader or partition table because that would replace
the product layout and persisted configuration.

Build and upload the Arduino comparison probe with:

```bash
pio run -d tools/diagnostics/cardputer_mic_probe_arduino
```

Neither probe records or stores audio content; serial output contains aggregate
sample metrics only.
