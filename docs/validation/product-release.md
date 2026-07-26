# Product release validation

## Automated scope

The release check covers all Python tests, firmware host tests, an
AddressSanitizer/UndefinedBehaviorSanitizer host build, a clean ESP32-S3 target
build, the embedded Web asset, Swift ProductAudio/ProductGATT/configuration
executables, the C17 shared ring, the AudioServerPlugIn test build and bundle
signature, Companion resources, generic/private image assembly, partition
offsets, audio-content exclusion, and secret exclusion from Git-tracked files
and the generic image.

The target build runs `idf.py set-target esp32s3` on every release check so an
ignored or stale local `sdkconfig` cannot select an old partition table. After
the build, the generated binary partition table is decoded and compared
field-for-field with `firmware/partitions_product.csv`, including partition
names, order, types, offsets, sizes and flags.

The private image includes a dedicated NVS image at `0x12000`; the firmware
application begins at `0x20000`. Both full images begin with the bootloader at
`0x0`.

## Runtime memory gate

Firmware 1.0.1 replaces the eager per-key allocation of sixteen sequence steps
with sparse sequence storage. The previous layout reserved 152,348 bytes for
the active Profile and left only 8,909 bytes of target DIRAM, causing NimBLE and
Wi-Fi initialization failures followed by an allocation panic and reboot.

The host test now limits `sizeof(Profile)` to 24 KiB. The release check also
parses the ESP-IDF target size report and refuses packaging unless at least
96 KiB of DIRAM remains for display, BLE, Wi-Fi, HTTPS and runtime allocations.
The fixed target image leaves 149,581 bytes before those runtime allocations.

## Product transport boundary

The delivered LAN status/control path uses device-hosted HTTPS plus the
screen pairing code. UTF-8 text uses the encrypted, authenticated, bonded BLE
connection. The earlier Phase 0 P-256/SAS, pinned WSS and cross-channel
challenge components remain in the repository as probe/security foundations,
but this product release does not claim that full WSS/SAS binding as completed.
It exposes no off-LAN relay or cloud control path.

## Hardware boundary

No automated result is recorded as a hardware pass unless a unique Cardputer
serial device is connected and explicitly selected. Without that device the
following remain unverified:

- actual LCD visibility and orientation;
- all 56 physical switch positions;
- BLE pairing/HID delivery on the target Mac;
- Wi-Fi association and HTTPS behavior on the target LAN;
- UTF-8 injection into real foreground applications;
- sustained 30-minute concurrent HIL measurements.

The 30-minute HIL is a real Hardware-in-the-Loop soak: the compiled firmware
runs on Cardputer while the Mac Companion, BLE HID/GATT, Wi-Fi, HTTPS Web
control, Codex status synchronization, reconnect cases, latency and resource
telemetry are exercised together.

For release 1.1.0, that soak additionally requires the installed
`Cardputer Codex Microphone`, physical G0 start/stop, continuous Core Audio
consumption, Core Audio/Companion restart recovery, and proof that disconnect
or reboot never resumes capture.
