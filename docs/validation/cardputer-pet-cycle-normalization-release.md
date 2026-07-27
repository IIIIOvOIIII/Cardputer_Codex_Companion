# Cardputer pet cycle normalization release validation

## Release identity

- Version: `1.1.8`
- Target: M5Stack Cardputer / ESP32-S3
- Pet format: CCPT v1, unchanged
- Installed Mac Companion: `1.1.8`
- Installed Core Audio HAL: `1.1.8`
- Device deployment boundary: app partition only at `0x20000`

## Root cause and correction

Rocky's stored payload is internally consistent. Its IDLE row has a genuine
two-frame source period, while WORKING and WAITING each have a genuine
four-frame source period. The device bundle digest matches the locally
generated bundle, so neither synchronization nor firmware decoding discarded
frames.

When the Mac Companion was online but Codex had not loaded a session, the
app-server repeatedly reported `notLoaded`. The old state resolver treated that
unrecognized value as IDLE, selecting Rocky's legitimate two-frame row. The
offline fallback selected WAITING, which explained why the full animation
appeared only with `M-`.

Version 1.1.8 maps only `notLoaded` to WAITING. ACTIVE continues to select
WORKING, and truly unknown future values still fall back to IDLE. The
transcoder retains the generalized rule approved for arbitrary pet assets: it
re-expands a shorter cycle only when complete rendered RGB565 pixel sequences
strictly prove that period. It does not assume an `ABCDAB--` layout or invent
frames absent from the source.

Pet animation is no longer frozen merely because the microphone state is
starting, live, or stopping. The full-frame decode followed by one LCD
`pushImage` remains unchanged, preserving the previously validated tear-free
display boundary.

## Automated evidence

The complete product release gate passed:

- Python: 206/206
- audio-specific Python: 29/29
- normal firmware host: 37/37
- sanitizer firmware host: 37/37
- ESP-IDF clean target build: passed
- application size: `0x187770` (1,603,440 bytes)
- application partition free space: 49%
- DIRAM headroom: 144,545 bytes
- ProductAudio, ProductGATT, ProductConfiguration and C audio tests: passed
- Swift application, HAL, signing, installer and private packaging: passed
- installer tests: 20/20

The installed Mac application and HAL both report `1.1.8`; the LaunchAgent is
running. The firmware was written only at `0x20000`, and the independent
`verify_flash` operation reported a matching digest.

After Core Audio and the Companion completed their normal post-install
recovery, HTTPS-only observation produced 20/20 consecutive samples with:

- firmware `1.1.8`
- BLE `OK`
- Wi-Fi `OK`
- Companion `OK`
- microphone `READY`

The protected pet-status endpoint reported Rocky, CCPT format version 1,
472,264 bytes used, no active transaction, and `last_result=cached`. Its digest
before and after the stability sample was unchanged:

`53ad97058ec2507c28698e9dc7f23593a0945a8eeaf7dd3a02747283c603433d`

## Physical gate

The user observed the real Cardputer and confirmed both cases:

- with top status `B+W+M+`, WAITING no longer repeats only the first two frames;
- after G0 activates `MIC 16K`, the pet continues its complete animation.

## Artifact checksums

- app-only firmware:
  `b0312dd21fa9c3af204923ed8ce4be18a270bad9bb185359feec03e087b7034a`
- generic full image:
  `ac7cccce9202a35c61ea3ff44159290549f9dd4e30b1ce4e6d505781e6d920c8`
- private full image:
  `60f0a4473331130b7d805ad37ac4cabcbe7f528310c6791573970e9f511fd54d`
- Companion executable:
  `efd49973c835a68f63b7f48bc713476936a6a4d77128fa44953a2978033aa52e`
- installer entry point:
  `ee012bf73cb502c2feba29260649eeaabb621920288926939ea7e859e0b2cbfd`
- AudioBridge:
  `b2effde04edab161ee67f048ad43fbec73b2038180a5c898291d699e8e7ee1eb`
- HAL executable:
  `44634939f11d3be265ef4e6ee41abdb7c18105d87d1fc31ca1f798b3d429fd9b`

`dist/1.1.8-SHA256SUMS` independently verifies the five delivered release
artifacts.
