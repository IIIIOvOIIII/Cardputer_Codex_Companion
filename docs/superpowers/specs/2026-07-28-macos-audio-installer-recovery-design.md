# macOS Audio Installer Recovery Design

## Goal

Make macOS microphone installation and removal deterministic on current macOS,
without leaving a loaded Cardputer HAL or a busy `coreaudiod` after either a
successful operation or a rollback.

## Confirmed failure

The failed 1.2.2 installation did not fail to load the microphone. Unified logs
showed the Cardputer HAL activated, the input device became available, and
macOS clients selected it. The installer nevertheless reported that Core Audio
did not enumerate it.

The false failure came from polling `system_profiler SPAudioDataType` while
Core Audio was restarting. That process emitted repeated
`HALC_Object_PropertyListener: not initialized` messages, timed out, and
caused the installer to roll back a healthy HAL. Rollback removed the bridge
while clients still held the old device, and the old installer did not require
or verify a `coreaudiod` PID transition. A subsequent uninstall skipped Core
Audio cleanup entirely once the component files were already absent.

## Selected repair

1. Add a narrow `audio-device-status` Companion command backed by the existing
   `CoreAudioDeviceCatalog` API. It returns only `PRESENT` or `ABSENT`.
2. Remove `system_profiler` from install, status, rollback, and uninstall
   checks.
3. Restart Core Audio with an operation-specific `sudo -p` prompt, fail on a
   non-zero restart command, and require the old `coreaudiod` PID to disappear
   before accepting a probe result.
4. Treat probe execution failure as unknown, never as proof that a microphone
   is absent.
5. During uninstall, remove exact system files when present and also restart
   Core Audio when the lightweight probe finds a stale loaded microphone.
6. Keep configuration and logs unless `--purge` is explicitly supplied.

## Password wording

The terminal must explain why elevation is required:

- install: `macOS administrator password (required to install the microphone driver):`
- uninstall: `macOS administrator password (required to remove the microphone driver):`
- recovery restart: `macOS administrator password (required to restart Core Audio):`

PIN input remains separate and masked.

## Compatibility boundary

The public commands remain:

```text
./install.sh install
./install.sh status
./install.sh uninstall
./install.sh uninstall --purge
```

The change does not alter firmware audio transport, BLE behavior, microphone
capture policy, PIN storage, or the Windows Agent.
