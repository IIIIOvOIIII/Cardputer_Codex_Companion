# Cardputer dedicated G0 task 1.3.5 validation

## Release identity

- Factory version: `1.3.5`
- M5Launcher-compatible version: `1.3.5l`
- Target: M5Stack Cardputer / ESP32-S3 revision 0.2
- Attached-device application offset: `0x170000`
- Observation date: 2026-07-30 HKT

## Root cause and repair

The 1.3.4 firmware ran the optional G0 HID chord and Mic toggle on the shared
`product-macro` task. A captured reproduction left only 24 bytes at that
task's stack high-water mark and the next G0 activation entered the FreeRTOS
stack-overflow hook, producing an `RTC_SW_CPU_RST`.

Version 1.3.5 moves G0 dual actions to a dedicated `product-g0` static task
with a 3,072-byte stack and eight-entry queue. A shared execution mutex now
serializes complete Profile and G0 MacroEngine operations. Queue or mutex
failure sends no partial chord but preserves the Mic toggle fallback.

## Automated evidence

The complete release gate passed:

- Python product suite: 303/303;
- audio and installer Python suite: 38/38;
- normal firmware host suite: 42/42;
- ASan/UBSan firmware host suite: 42/42;
- Node Web suite: 7/7;
- clean ESP-IDF Factory 1.3.5 target build: passed;
- target DIRAM headroom: 130,625 bytes;
- clean ESP-IDF Launcher 1.3.5l target build: passed;
- Launcher application partition fit: 1,610,976/1,638,400 bytes;
- macOS Swift, audio driver, bridge, and packaging gates: passed;
- Windows Go, race, archive, and installer gates: passed;
- all entries in `dist/1.3.5-SHA256SUMS`: passed;
- public-artifact allowlist: 15 approved top-level entries;
- current tree, refs, reflogs, and retained unreachable-object credential
  audit: zero findings.

## Attached-device deployment

Only the Launcher application's `0x170000` slot was written. NVS, Wi-Fi,
pairing, profiles, Pet data, and storage were not erased. Esptool verified the
write hash and an independent `verify_flash` operation matched all 1,610,976
application bytes.

After deployment, the authenticated status endpoint reported:

- version `1.3.5l`;
- BLE `OK`;
- Wi-Fi `OK`;
- Companion `OK`;
- microphone `READY`.

The test restored the device's pre-existing enabled G0 dual-action setting.

## Repeated G0 hardware gate

The enabled path ran 20 sequential synthetic G0 activations through the same
firmware handler used by the physical button:

- acknowledgements: 20/20;
- completed dual actions: 20/20;
- completed Mic state transitions: 20/20;
- device resets: 0;
- HID queue-failure delta: 0;
- minimum `product-g0` free stack: 868 bytes;
- required minimum free stack: 768 bytes;
- elapsed time: 51,106 ms.

The disabled control also passed:

- command path: Mic only;
- Mic state transition: completed;
- dual action executed: no;
- device resets: 0;
- HID queue-failure delta: 0.

The metrics-only HIL records contain no device PIN, network address, HID chord,
or audio content.

## Artifacts

- Factory full image:
  `c979f64541b94e4962d60111a5da514bc21ea2c11a334f4c2f2a79d66e394ebb`
  (`1,771,648` bytes)
- application image:
  `9caeb41636f7a0dc716bf44e50abaf23b0e69d0c9fe24af12f1cea3d792db2e9`
  (`1,640,576` bytes)
- M5Launcher-compatible image:
  `de9c428205f5fc258b6c41cefe993c37a5ad0283c715675988b65a70587525e8`
  (`6,426,624` bytes)

## Result

Passed for root-cause repair, complete release gate, state-preserving
deployment, repeated enabled G0 stress, disabled Mic-only behavior, runtime
stack headroom, and online device health. Public release and Pages publication
are recorded separately after remote verification.
