# macOS GATT Reboot Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the running macOS Agent automatically restore the Cardputer custom-GATT microphone subscription after every Cardputer reboot without restarting the Agent.

**Architecture:** Add a pure `ProductGATTRecoveryPolicy` that owns phase, timeout, retry-backoff, and generation decisions. Integrate it into `ProductGATTConnection` through one cleanup-and-retry path used by CoreBluetooth failures, timeouts, and unintentional disconnects; then verify it with unit tests and a five-cycle, fixed-Agent-PID hardware gate.

**Tech Stack:** Swift 6, CoreBluetooth, Dispatch timers, XCTest, Python 3.11/pytest, ESP-IDF 5.5.4, esptool, HTTPS device status API

## Global Constraints

- Target product version is `1.3.3`; Launcher-compatible firmware version is `1.3.3l`.
- Connection, discovery, and subscription watchdog duration is 8 seconds.
- Retry delays are 500, 1000, 2000, and 5000 milliseconds, capped at 5000 milliseconds.
- A successful sink-ready handshake resets retry backoff.
- Intentional Agent stop must cancel recovery and schedule no retry.
- Recovery must keep the same Agent PID and must not erase BLE bonds or rotate the device PIN.
- HID, Unicode GATT, LAN sync, HAL/XPC protocol, and firmware microphone behavior must remain unchanged.
- Logs and HIL reports must not contain PINs, Wi-Fi credentials, device identifiers, or audio content.
- The XPC doctor producer-lease false negative is out of scope.

---

### Task 1: Specify the deterministic GATT recovery policy

**Files:**
- Create: `companion/Sources/ProductGATT/ProductGATTRecoveryPolicy.swift`
- Create: `companion/Tests/ProductGATTTests/ProductGATTRecoveryPolicyTests.swift`

**Interfaces:**
- Produces: `ProductGATTRecoveryPhase`, `ProductGATTRecoveryEvent`, `ProductGATTRecoveryDecision`, and `ProductGATTRecoveryPolicy.apply(_:)`.
- Consumed by: Task 2's `ProductGATTConnection`.

- [ ] **Step 1: Write failing policy tests**

Create `ProductGATTRecoveryPolicyTests.swift` with these cases:

```swift
import XCTest
@testable import ProductGATT

final class ProductGATTRecoveryPolicyTests: XCTestCase {
    func testFailureBackoffIsBoundedAndReadyResetsIt() {
        var policy = ProductGATTRecoveryPolicy()
        XCTAssertEqual(policy.apply(.start).retryAfterMilliseconds, 0)
        XCTAssertEqual(policy.apply(.candidateSelected).watchdogMilliseconds, 8_000)
        XCTAssertEqual(policy.apply(.failed).retryAfterMilliseconds, 500)
        XCTAssertEqual(policy.apply(.failed).retryAfterMilliseconds, 1_000)
        XCTAssertEqual(policy.apply(.failed).retryAfterMilliseconds, 2_000)
        XCTAssertEqual(policy.apply(.failed).retryAfterMilliseconds, 5_000)
        XCTAssertEqual(policy.apply(.failed).retryAfterMilliseconds, 5_000)
        XCTAssertEqual(policy.apply(.ready).phase, .ready)
        XCTAssertEqual(policy.apply(.failed).retryAfterMilliseconds, 500)
    }

    func testTimeoutUsesFailurePathAndAdvancesGeneration() {
        var policy = ProductGATTRecoveryPolicy()
        _ = policy.apply(.start)
        let connecting = policy.apply(.candidateSelected)
        let timedOut = policy.apply(.timedOut)
        XCTAssertTrue(timedOut.cancelPeripheral)
        XCTAssertEqual(timedOut.retryAfterMilliseconds, 500)
        XCTAssertGreaterThan(timedOut.generation, connecting.generation)
    }

    func testIntentionalStopNeverRetries() {
        var policy = ProductGATTRecoveryPolicy()
        _ = policy.apply(.start)
        let stopped = policy.apply(.stop)
        XCTAssertEqual(stopped.phase, .stopped)
        XCTAssertNil(stopped.retryAfterMilliseconds)
        XCTAssertNil(policy.apply(.failed).retryAfterMilliseconds)
        XCTAssertNil(policy.apply(.timedOut).retryAfterMilliseconds)
    }

    func testBluetoothUnavailableWaitsForPoweredOn() {
        var policy = ProductGATTRecoveryPolicy()
        _ = policy.apply(.start)
        let unavailable = policy.apply(.bluetoothUnavailable)
        XCTAssertEqual(unavailable.phase, .idle)
        XCTAssertNil(unavailable.retryAfterMilliseconds)
        XCTAssertEqual(policy.apply(.bluetoothPoweredOn).retryAfterMilliseconds, 0)
    }

    func testConnectionPhasesArmEightSecondWatchdog() {
        var policy = ProductGATTRecoveryPolicy()
        _ = policy.apply(.start)
        XCTAssertEqual(policy.apply(.candidateSelected).phase, .connecting)
        XCTAssertEqual(policy.apply(.connected).phase, .discovering)
        XCTAssertEqual(policy.apply(.subscribing).phase, .subscribing)
        XCTAssertEqual(policy.apply(.subscribing).watchdogMilliseconds, 8_000)
    }
}
```

- [ ] **Step 2: Run the focused tests and confirm RED**

Run:

```bash
swift test --package-path companion \
  --filter ProductGATTRecoveryPolicyTests
```

Expected: compilation fails because `ProductGATTRecoveryPolicy` and related
types do not exist.

- [ ] **Step 3: Implement the minimal policy**

Create `ProductGATTRecoveryPolicy.swift` with public value types matching:

```swift
public enum ProductGATTRecoveryPhase: Equatable, Sendable {
    case idle, scanning, connecting, discovering, subscribing, ready, stopped
}

public enum ProductGATTRecoveryEvent: Equatable, Sendable {
    case start
    case bluetoothPoweredOn
    case bluetoothUnavailable
    case candidateSelected
    case connected
    case subscribing
    case ready
    case failed
    case timedOut
    case disconnected(intentional: Bool)
    case stop
}

public struct ProductGATTRecoveryDecision: Equatable, Sendable {
    public let phase: ProductGATTRecoveryPhase
    public let generation: UInt64
    public let cancelPeripheral: Bool
    public let retryAfterMilliseconds: Int?
    public let watchdogMilliseconds: Int?
}

public struct ProductGATTRecoveryPolicy: Sendable {
    public static let watchdogMilliseconds = 8_000
    private static let retryMilliseconds = [500, 1_000, 2_000, 5_000]
    public private(set) var phase: ProductGATTRecoveryPhase = .idle
    public private(set) var generation: UInt64 = 0
    private var failureCount = 0

    public init() {}
    public mutating func apply(
        _ event: ProductGATTRecoveryEvent
    ) -> ProductGATTRecoveryDecision {
        if phase == .stopped, event != .start {
            return decision()
        }
        switch event {
        case .start, .bluetoothPoweredOn:
            failureCount = 0
            phase = .scanning
            generation &+= 1
            return decision(retryAfterMilliseconds: 0)
        case .bluetoothUnavailable:
            phase = .idle
            generation &+= 1
            return decision()
        case .candidateSelected:
            phase = .connecting
            generation &+= 1
            return decision(
                watchdogMilliseconds: Self.watchdogMilliseconds
            )
        case .connected:
            phase = .discovering
            generation &+= 1
            return decision(
                watchdogMilliseconds: Self.watchdogMilliseconds
            )
        case .subscribing:
            phase = .subscribing
            generation &+= 1
            return decision(
                watchdogMilliseconds: Self.watchdogMilliseconds
            )
        case .ready:
            phase = .ready
            failureCount = 0
            generation &+= 1
            return decision()
        case .failed, .timedOut, .disconnected(intentional: false):
            phase = .scanning
            generation &+= 1
            let index = min(
                failureCount,
                Self.retryMilliseconds.count - 1
            )
            failureCount &+= 1
            return decision(
                cancelPeripheral: true,
                retryAfterMilliseconds: Self.retryMilliseconds[index]
            )
        case .disconnected(intentional: true), .stop:
            phase = .stopped
            generation &+= 1
            return decision(cancelPeripheral: true)
        }
    }

    private func decision(
        cancelPeripheral: Bool = false,
        retryAfterMilliseconds: Int? = nil,
        watchdogMilliseconds: Int? = nil
    ) -> ProductGATTRecoveryDecision {
        ProductGATTRecoveryDecision(
            phase: phase,
            generation: generation,
            cancelPeripheral: cancelPeripheral,
            retryAfterMilliseconds: retryAfterMilliseconds,
            watchdogMilliseconds: watchdogMilliseconds
        )
    }
}
```

Keep this file free of CoreBluetooth dependencies.

- [ ] **Step 4: Run policy and existing GATT tests**

Run:

```bash
swift test --package-path companion \
  --filter ProductGATTRecoveryPolicyTests
swift run --package-path companion -c release product-gatt-tests
```

Expected: all policy tests pass and output ends with `ProductGATT tests passed`.

- [ ] **Step 5: Commit the policy**

```bash
git add \
  companion/Sources/ProductGATT/ProductGATTRecoveryPolicy.swift \
  companion/Tests/ProductGATTTests/ProductGATTRecoveryPolicyTests.swift
git commit -m "test: specify bounded GATT reboot recovery"
```

---

### Task 2: Integrate failure callbacks and phase watchdogs

**Files:**
- Modify: `companion/Sources/ProductGATT/ProductGATTConnection.swift`
- Modify: `companion/Tests/ProductGATTTests/ProductGATTReceiverTests.swift`
- Modify: `tools/product/tests/test_companion_packaging.py`

**Interfaces:**
- Consumes: `ProductGATTRecoveryPolicy.apply(_:)` from Task 1.
- Produces: in-process recovery for callback failure, callback timeout, and unintentional disconnect.

- [ ] **Step 1: Add failing source-contract tests**

Extend `tools/product/tests/test_companion_packaging.py` with:

```python
def test_product_gatt_reconnects_failed_and_timed_out_attempts():
    source = (
        ROOT
        / "companion/Sources/ProductGATT/ProductGATTConnection.swift"
    ).read_text()
    assert "didFailToConnect peripheral" in source
    assert "ProductGATTRecoveryPolicy" in source
    assert "scheduleRecoveryTimer" in source
    assert "recoverConnection(reason:" in source
    assert "decision.generation == recovery.generation" in source
```

The callback signature may span lines, but retain the
`didFailToConnect peripheral` token in the implementation so this contract
checks the real callback.

- [ ] **Step 2: Confirm the regression test fails**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py \
  -k gatt_reconnects_failed_and_timed_out_attempts
```

Expected: failure because the current connection has no recovery policy,
watchdog, or `didFailToConnect` callback.

- [ ] **Step 3: Add recovery state and timer ownership**

In `ProductGATTConnection`, add:

```swift
private var recovery = ProductGATTRecoveryPolicy()
private var recoveryTimer: DispatchSourceTimer?

private func cancelRecoveryTimer() {
    recoveryTimer?.setEventHandler {}
    recoveryTimer?.cancel()
    recoveryTimer = nil
}

private func scheduleRecoveryTimer(
    milliseconds: Int,
    generation: UInt64,
    event: ProductGATTRecoveryEvent
) {
    cancelRecoveryTimer()
    let timer = DispatchSource.makeTimerSource(queue: queue)
    timer.schedule(deadline: .now() + .milliseconds(milliseconds))
    timer.setEventHandler { [weak self] in
        guard let self,
              generation == recovery.generation else { return }
        applyRecovery(event, reason: "deadline")
    }
    recoveryTimer = timer
    timer.resume()
}
```

Implement `applyRecovery(_:reason:)` so it cancels the prior timer, applies
the event, verifies `decision.generation == recovery.generation`, cancels the
current peripheral when requested, and schedules either the watchdog or the
next call to `beginConnection()`.

- [ ] **Step 4: Route every broken phase through one cleanup path**

Add:

```swift
private func recoverConnection(reason: String) {
    let current = peripheral
    peripheral = nil
    characteristics.removeAll(keepingCapacity: true)
    pendingWrite = nil
    session.didDisconnect()
    publishSession()
    if let current {
        central.cancelPeripheralConnection(current)
    }
    applyRecovery(.failed, reason: reason)
}
```

Call the recovery path from:

- `centralManager(_:didFailToConnect:error:)`;
- service discovery error or missing product service;
- characteristic discovery error or missing a required characteristic;
- failed bind, hello, or sink-ready write;
- failed/disabled audio notification setup;
- the 8-second watchdog for connecting, discovering, and subscribing.

Map lifecycle events as follows:

```swift
// poweredOn
applyRecovery(.bluetoothPoweredOn, reason: "bluetooth_powered_on")

// selecting a retrieved or scanned peripheral
applyRecovery(.candidateSelected, reason: "candidate_selected")

// didConnect
applyRecovery(.connected, reason: "connected")

// required characteristics found, before bind/notifications
applyRecovery(.subscribing, reason: "subscribing")

// sink-ready write succeeds
applyRecovery(.ready, reason: "audio_ready")

// intentional stop
applyRecovery(.stop, reason: "intentional_stop")
```

For `didDisconnectPeripheral`, use
`.disconnected(intentional: intentionalStop)` and retain the existing delegate
notification. Do not call both `beginConnection()` and the recovery decision.

- [ ] **Step 5: Add privacy-safe recovery logging**

Use `Logger` with subsystem `com.lynx.cardputer-companion` and category
`gatt-recovery`. Log only phase, reason, retry delay, and generation:

```swift
logger.notice(
    "phase=\(String(describing: decision.phase), privacy: .public) " +
    "reason=\(reason, privacy: .public) " +
    "retry_ms=\(decision.retryAfterMilliseconds ?? -1, privacy: .public) " +
    "generation=\(decision.generation, privacy: .public)"
)
```

Do not log peripheral names, UUID instances, PINs, or payloads.

- [ ] **Step 6: Run focused and complete Swift gates**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py \
  -k gatt_reconnects_failed_and_timed_out_attempts
swift test --package-path companion
swift run --package-path companion -c release product-gatt-tests
swift run --package-path companion -c release product-audio-tests
```

Expected: all commands exit 0.

- [ ] **Step 7: Commit the runtime fix**

```bash
git add \
  companion/Sources/ProductGATT/ProductGATTConnection.swift \
  companion/Tests/ProductGATTTests/ProductGATTReceiverTests.swift \
  tools/product/tests/test_companion_packaging.py
git commit -m "fix: recover GATT audio after Cardputer reboot"
```

---

### Task 3: Add a repeatable fixed-PID reboot HIL gate

**Files:**
- Create: `scripts/product/run_gatt_reboot_recovery_hil.py`
- Create: `tools/product/tests/test_gatt_reboot_recovery_hil.py`
- Modify: `docs/validation/cardputer-ble-microphone-release.md`

**Interfaces:**
- Consumes: installed Agent config, LaunchAgent label, ESP32-S3 serial port, and `/api/v1/status`.
- Produces: `build/hil/gatt-reboot-recovery.json` containing no credentials or audio.

- [ ] **Step 1: Write failing helper tests**

Create tests for these exact functions:

```python
from scripts.product.run_gatt_reboot_recovery_hil import (
    parse_launchctl_pid,
    ready_snapshot,
    sanitized_cycle,
)

def test_parse_launchctl_pid():
    assert parse_launchctl_pid("\\n\\tpid = 48123\\n") == 48123

def test_ready_snapshot_requires_all_links():
    assert ready_snapshot({
        "ble": "OK",
        "wifi": "OK",
        "companion": "OK",
        "microphone": {"state": "READY", "last_error": "NONE"},
    })
    assert not ready_snapshot({
        "ble": "OK",
        "wifi": "OK",
        "companion": "OK",
        "microphone": {"state": "UNAVAILABLE", "last_error": "NONE"},
    })

def test_cycle_report_has_no_credentials():
    report = sanitized_cycle(1, 48123, 2.4, "READY")
    assert set(report) == {
        "cycle", "agent_pid", "ready_seconds", "microphone_state"
    }
```

- [ ] **Step 2: Confirm RED**

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_gatt_reboot_recovery_hil.py
```

Expected: import failure because the HIL module does not exist.

- [ ] **Step 3: Implement the HIL runner**

The CLI must accept:

```text
--port /dev/cu.usbmodem21101
--cycles 5
--ready-timeout 15
--config ~/Library/Application Support/CardputerCodexCompanion/config.json
--output build/hil/gatt-reboot-recovery.json
```

For each cycle:

1. Read the current Agent PID from
   `launchctl print gui/<uid>/com.lynx.cardputer-companion`.
2. Require the authenticated device status to be READY.
3. Reset without writing flash:

```bash
<idf-python> -m esptool \
  --chip esp32s3 \
  --port <port> \
  --after hard_reset \
  flash_id
```

4. Poll authenticated `/api/v1/status` every 250 ms for at most 15 seconds.
5. Require the same Agent PID and
   `BLE=OK/Wi-Fi=OK/companion=OK/microphone=READY`.
6. Append only the four fields asserted by `sanitized_cycle`.

Use a mode-0600 temporary curl config and delete it in `finally`; never place
the PIN on curl's command line or in the output report.

- [ ] **Step 4: Run unit tests**

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_gatt_reboot_recovery_hil.py
```

Expected: all tests pass.

- [ ] **Step 5: Document the hardware acceptance criteria**

Add to `docs/validation/cardputer-ble-microphone-release.md`:

```markdown
## Reboot recovery gate

- Five consecutive host-controlled Cardputer resets.
- `MIC READY` within 15 seconds after each reset.
- Agent PID unchanged for the full gate.
- BLE, Wi-Fi, and Agent remain `OK`.
- No panic, abort, allocation failure, or automatic Agent restart.
- Existing `HIL MIC START` and `HIL MIC STOP` succeed after cycle five.
```

- [ ] **Step 6: Commit the HIL gate**

```bash
git add \
  scripts/product/run_gatt_reboot_recovery_hil.py \
  tools/product/tests/test_gatt_reboot_recovery_hil.py \
  docs/validation/cardputer-ble-microphone-release.md
git commit -m "test: gate microphone reboot recovery"
```

---

### Task 4: Build and locally install the repaired macOS Agent

**Files:**
- Generated, ignored: `dist/CardputerCompanion.app`

**Interfaces:**
- Consumes: Tasks 1-2 source and current driver/helper resources.
- Produces: a locally installed Agent bundle used by Task 5.

- [ ] **Step 1: Build and verify the app**

```bash
scripts/build_companion.sh
codesign --verify --deep --strict dist/CardputerCompanion.app
dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion --version
```

Expected before the version-bump task: signature verification succeeds and the
binary reports the current source version.

- [ ] **Step 2: Install without changing pairing data**

```bash
./install.sh install \
  --app dist/CardputerCompanion.app \
  --config "$HOME/Library/Application Support/CardputerCodexCompanion/config.json"
./install.sh status
```

If the root installer does not accept both non-interactive arguments, use its
documented existing-config installation path; do not print or replace the
stored PIN.

Expected: App, config, Agent, HAL, bridge, audio, and LAN all report healthy.

- [ ] **Step 3: Record installed version and PID**

```bash
"$HOME/Applications/CardputerCompanion.app/Contents/MacOS/cardputer-companion" \
  --version
launchctl print "gui/$(id -u)/com.lynx.cardputer-companion" |
  awk '/^[[:space:]]+pid =/{print $3; exit}'
```

Keep the PID for the hardware gate; do not restart the Agent during Task 5.

---

### Task 5: Prove recovery on the attached Cardputer

**Files:**
- Generated, ignored: `build/hil/gatt-reboot-recovery.json`
- Generated, ignored: `build/hil/gatt-reboot-recovery-serial.log`

**Interfaces:**
- Consumes: Task 3 HIL runner and Task 4 installed Agent.
- Produces: five-cycle recovery evidence and a post-recovery audio-start result.

- [ ] **Step 1: Establish the healthy baseline**

Run `./install.sh status`, then query `/api/v1/status` through a mode-0600 curl
config. Require:

```json
{
  "ble": "OK",
  "wifi": "OK",
  "companion": "OK",
  "microphone": {
    "state": "READY",
    "last_error": "NONE"
  }
}
```

- [ ] **Step 2: Run five reboot cycles**

```bash
PYTHONPATH=. uv run python \
  scripts/product/run_gatt_reboot_recovery_hil.py \
  --port /dev/cu.usbmodem21101 \
  --cycles 5 \
  --ready-timeout 15 \
  --config "$HOME/Library/Application Support/CardputerCodexCompanion/config.json" \
  --output build/hil/gatt-reboot-recovery.json
```

Expected: five passing cycles, every cycle at or below 15 seconds, and one
unchanged Agent PID.

- [ ] **Step 3: Prove the recovered subscription carries audio**

Open the USB serial descriptor once, send `HIL MIC START\n`, wait for
`HIL MIC START ACCEPTED`, observe non-zero `captured_frames` and
`received_frames`, then send `HIL MIC STOP\n` and require
`HIL MIC STOP ACCEPTED`.

Do not record PCM, ADPCM, WAV, or other audio content.

- [ ] **Step 4: Check serial health**

Reject the run if the serial log contains:

```text
Guru Meditation
abort()
stack overflow
allocation_failures":1
HIL MIC START REJECTED
```

- [ ] **Step 5: Record the milestone**

Update `docs/2026-07-29-macos-gatt-reboot-recovery_PROGRESS.md` with the exact
five recovery times, the unchanged-PID result, audio start/stop result, and
serial health result.

---

### Task 6: Unify version 1.3.3 and build release artifacts

**Files:**
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify: `firmware/main/product/ui_model.cpp`
- Modify: `firmware/main/product/onboarding.cpp`
- Modify: `companion/AppBundle/Info.plist`
- Modify: `companion/AudioDriver/Info.plist`
- Modify: `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Modify: `windows-agent/README.txt`
- Modify: `windows-agent/internal/codex/process.go`
- Modify: `scripts/verify_product_release.sh`
- Modify: `release/product-release.json`
- Modify: `README.md`
- Modify: `README.zh-CN.md`
- Modify: `web-installer/index.html`
- Modify: `web-installer/manifest.json`
- Modify tests that pin the current version.

**Interfaces:**
- Consumes: HIL-passed implementation from Tasks 1-5.
- Produces: Factory `1.3.3`, Launcher `1.3.3l`, macOS/Windows installers, Web installer, checksums, and release manifest.

- [ ] **Step 1: Add failing version-surface expectations**

Update existing release tests so the single expected product version is
`1.3.3` and Launcher version is `1.3.3l`, then run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_web_installer.py \
  tools/product/tests/test_windows_agent_packaging.py
```

Expected: failures listing the remaining `1.3.2` surfaces.

- [ ] **Step 2: Update active version surfaces**

Replace active product `1.3.2`/`1.3.2l` values with `1.3.3`/`1.3.3l` in the
files listed above. Do not rewrite historical progress, design, validation, or
release notes.

Run:

```bash
rg -n '1\\.3\\.2l?' \
  firmware/CMakeLists.txt \
  firmware/main/product \
  companion/AppBundle/Info.plist \
  companion/AudioDriver/Info.plist \
  companion/Sources/cardputer-companion \
  windows-agent/internal/version \
  scripts/verify_product_release.sh \
  release/product-release.json \
  README.md README.zh-CN.md \
  web-installer
```

Expected: no active `1.3.2` reference remains.

- [ ] **Step 3: Run the complete release gate**

```bash
scripts/verify_product_release.sh
```

Expected: Python, Swift, C/C++, sanitizer, firmware, web, Windows, signing,
packaging, checksum, allowlist, and credential-history gates all pass.

- [ ] **Step 4: Pin the verified Factory digest**

Compute:

```bash
shasum -a 256 \
  dist/Cardputer-Codex-Companion-1.3.3-factory.bin
```

Use `apply_patch` to set `release/product-release.json` field
`sha256.firmware_factory` to the exact emitted digest, regenerate
`dist/1.3.3-SHA256SUMS`, and rerun all checksum and release-manifest tests.

- [ ] **Step 5: Repeat the hardware reboot gate on release binaries**

Flash `Cardputer-Codex-Companion-1.3.3l-launcher.bin` through the existing
Launcher-compatible path, install the packaged macOS 1.3.3 Agent, and repeat
Task 5. Do not accept source-build-only HIL evidence.

- [ ] **Step 6: Commit the versioned release**

```bash
git add \
  firmware companion windows-agent scripts release \
  README.md README.zh-CN.md web-installer \
  tools/product/tests \
  docs/2026-07-29-macos-gatt-reboot-recovery_PROGRESS.md
git commit -m "fix: release automatic microphone reboot recovery"
```

---

### Task 7: Publish and verify the public release

**Files:**
- Generated, ignored: versioned release assets under `dist/`

**Interfaces:**
- Consumes: Task 6 verified artifacts and exact checksums.
- Produces: Git tag/Release `v1.3.3` and deployed GitHub Pages Web installer.

- [ ] **Step 1: Audit public assets and repository history**

Run the repository's public-artifact allowlist and current/history credential
audit. Require zero findings before tagging.

- [ ] **Step 2: Push the implementation commits**

```bash
git push origin main
```

Require local `HEAD` to equal `origin/main`.

- [ ] **Step 3: Publish `v1.3.3`**

Create annotated tag `v1.3.3`, publish the verified Factory, app, Launcher,
macOS, Windows, Web installer, and checksum assets, and confirm GitHub's Factory
asset digest equals `release/product-release.json`.

- [ ] **Step 4: Deploy Pages**

Trigger `Deploy Web Installer` only after the Release exists. Require the
workflow's fetch-and-stage digest check and Pages deployment to succeed.

- [ ] **Step 5: Verify live artifacts**

Require:

```text
Web installer HTTP 200
manifest version 1.3.3
Factory download HTTP 200
live Factory SHA-256 equals release/product-release.json
```

- [ ] **Step 6: Final closeout**

Record release/tag, commit, workflow run, live URL, checksums, five-cycle HIL
times, unchanged Agent PID, and zero-finding credential audit in the progress
document and daily workspace memory.
