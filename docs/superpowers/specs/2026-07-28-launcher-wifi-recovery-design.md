# Launcher Wi-Fi Recovery Design

## Context

Cardputer Codex Companion `1.3.1l` can enter an infinite reboot loop after
onboarding saves Wi-Fi credentials. Removing and reinstalling the application
through M5Launcher does not recover the device because application-owned NVS
data survives the reinstall.

Live serial evidence from the affected device shows:

- the saved station associates and receives `192.168.1.195`;
- `IP_EVENT_STA_GOT_IP` then calls the product Wi-Fi status callback;
- the ESP-IDF `sys_evt` task reports a stack overflow;
- the decoded application frame is
  `wifi_manager.cpp:event_handler()` at the synchronous online notification;
- the device reboots with `RTC_SW_CPU_RST`;
- startup error `E294` is `ESP_ERR_WIFI_STATE` from an overlapping Wi-Fi scan,
  not the reboot's primary cause.

## Goals

- Prevent Wi-Fi/IP system callbacks from running product NVS, UI, Web, or
  onboarding work.
- Make onboarding scans tolerate temporary Wi-Fi driver state transitions
  without surfacing `E294`.
- Provide a local, Launcher-safe recovery path by holding Backspace during
  boot and confirming deletion with Y/N.
- Build Factory `1.3.2` and Launcher-compatible `1.3.2l`.

## Non-goals

- Do not change the Launcher itself or maintain a Launcher fork.
- Do not change the partition offsets or sizes.
- Do not automatically delete data after an ordinary Wi-Fi failure.
- Do not use a larger `sys_evt` stack as the primary fix.

## Wi-Fi event boundary

The ESP-IDF default event-loop callback becomes a bounded event producer. It
may only copy fixed-size event data and publish atomic flags. It must not:

- mutate the Wi-Fi state machine;
- access NVS;
- acquire product/UI mutexes;
- invoke product status handlers;
- reconnect or start scans.

The existing `wifi-state` task becomes the sole consumer and owner of those
operations. It processes got-IP, disconnect, scan-complete, timeout, and scan
request events in deterministic order. Online notification and candidate
credential persistence therefore execute on the dedicated task stack rather
than on `sys_evt`.

## Deferred scan behavior

`product_wifi_scan()` records the completion handler and queues a scan request.
The `wifi-state` task starts the scan only after station mode has started and no
scan is already active. `ESP_ERR_WIFI_STATE` is treated as a temporary state:
the request remains queued and is retried with bounded backoff. Permanent
errors are reported once to onboarding. A completed scan clears the in-flight
state before its results are published.

This removes the startup race in which onboarding requests a scan while the
station driver is still changing state.

## Boot recovery interaction

After display and NVS initialization, but before product storage, Wi-Fi, Web,
or BLE startup, firmware samples the physical keyboard matrix directly.
Backspace is physical key 13. It must remain pressed for approximately 600 ms
to enter recovery, preventing an incidental key press from deleting data.

Recovery presents a dedicated local screen:

```text
DELETE ALL COMPANION DATA?
Y = DELETE
N = CANCEL
```

Only Y and N are accepted. The keyboard scanner and HID service have not
started, so recovery keystrokes cannot reach the paired computer. N continues
the normal boot. Y performs the deletion, reports success or the exact failed
stage, and restarts only after all selected stores were handled.

## Deletion boundary

The user selected a full Companion reset. The recovery operation deletes:

- NVS namespace `wifi`;
- NVS namespace `product`;
- NVS namespace `product_tls`;
- NVS namespace `phase0_id`;
- NVS namespace `nimble_bond`;
- all Companion-owned bytes in the 1,920 KiB `storage` partition for Factory
  builds or the layout-compatible `assets` partition for Launcher builds.

Some devices upgraded through older M5Launcher layouts have neither dedicated
partition and expose only Launcher's shared `spiffs` partition. On that layout,
recovery clears the Companion NVS namespaces and treats the absent dedicated
storage as already empty. It must not erase the shared `spiffs` partition.

The recovery operation must not erase:

- the complete NVS partition;
- OTA metadata;
- bootloader or partition table;
- application partitions;
- a Launcher-owned shared `spiffs` partition;
- M5Launcher itself.

Erasing namespaces rather than the complete NVS partition prevents removal of
unrelated Launcher state.

## Failure handling

- A failed Y operation stays on the recovery screen and shows a short stage
  code; it does not silently continue with partially reset state.
- N never mutates storage and continues the normal boot.
- Scan retry is bounded in frequency, not in total onboarding usability:
  users can request another scan without rebooting.
- Wi-Fi disconnect/got-IP event flags are coalesced safely because the state
  task consumes them every 250 ms and the current product supports one station
  connection.

## Verification

Implementation is accepted only when:

- host tests prove Y/N recovery decisions and the exact deletion set;
- source/host tests prove the ESP event callback contains no NVS, notification,
  mutex, reconnect, or scan work;
- scan tests prove `ESP_ERR_WIFI_STATE` is retried instead of exposed as a
  startup failure;
- all existing host and Python product tests pass;
- Factory `1.3.2` and Launcher `1.3.2l` build and pass artifact checks;
- attached-device serial testing proves saved Wi-Fi reaches got-IP, advances
  to BLE onboarding, and remains stable without `sys_evt` overflow or E294;
- the Backspace recovery N path is verified without data loss, and the Y path
  is verified only with explicit approval before deleting device data.
