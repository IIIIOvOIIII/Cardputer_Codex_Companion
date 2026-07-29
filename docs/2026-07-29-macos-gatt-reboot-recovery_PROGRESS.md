# macOS GATT Reboot Recovery Progress

## 2026-07-29 09:50 HKT

- Current work: Root-cause diagnosis and implementation planning for microphone
  GATT recovery after Cardputer reboot.
- Expected result: A reviewed design and executable plan that restores
  `MIC READY` without restarting the Agent.
- Result: Achieved for diagnosis and planning. Runtime evidence showed
  `BLE/Wi-Fi/Agent=OK` with `MIC=UNAVAILABLE`; a controlled Agent restart
  restored READY within one second and remained stable for 30 seconds. Source
  review confirmed missing `didFailToConnect` handling and no connection-phase
  watchdog.
- Next step: Implement the pure recovery policy with failing tests, integrate
  CoreBluetooth callbacks and timers, then run the fixed-PID five-cycle HIL
  gate.
