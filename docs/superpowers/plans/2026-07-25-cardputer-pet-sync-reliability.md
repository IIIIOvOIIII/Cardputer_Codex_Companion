# Cardputer Pet Synchronization Reliability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a valid Codex pet selection reach the Cardputer within 30 seconds under normal LAN operation even when unrelated action or snapshot requests fail.

**Architecture:** Keep one serialized Companion loop for all Cardputer HTTPS requests. Add a deterministic monotonic-clock cadence value, service due pet synchronization before the action boundary, schedule healthy checks at 30 seconds, and retry failed checks after 5 seconds.

**Tech Stack:** Swift 6, Foundation `ContinuousClock`, Swift Package Manager executable test harness, Python pytest contract tests, ESP-IDF 5.5.4 release gate, launchd, Cardputer HTTPS API.

## Global Constraints

- Do not change the CCPT wire format, firmware storage layout, Web API, animation renderer, or BLE keyboard behavior.
- Do not issue concurrent Cardputer HTTPS requests.
- First-run pet synchronization is immediate.
- Successful synchronization schedules the next check 30 seconds later.
- Failed synchronization schedules the next attempt 5 seconds later.
- Action/snapshot failure must not suppress a due pet synchronization attempt.
- Pet synchronization failure must not suppress action polling in the same loop.
- Logs must not expose PINs, pairing headers, configuration contents, or credentials.
- Hardware acceptance requires the device pet ID and digest to change within 30 seconds.

---

## File Structure

- Create `companion/Sources/ProductPet/PetSyncCadence.swift`: pure monotonic scheduling state with no I/O.
- Modify `companion/Tests/ProductPetExecutableTests/main.swift`: deterministic cadence RED/GREEN coverage.
- Modify `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`: independent pet and action error boundaries, serial ordering, and safe result logging.
- Modify `tools/product/tests/test_companion_packaging.py`: source-level integration contract protecting call ordering and error-boundary separation.
- Modify `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`: implementation, release, and hardware evidence.

### Task 1: Deterministic Pet Synchronization Cadence

**Files:**
- Create: `companion/Sources/ProductPet/PetSyncCadence.swift`
- Modify: `companion/Tests/ProductPetExecutableTests/main.swift`

**Interfaces:**
- Consumes: `PetSyncResult.errorCode: String?` from `ProductPet`.
- Produces: `PetSyncCadence.init()`, `isDue(at: ContinuousClock.Instant) -> Bool`, and mutating `record(result: PetSyncResult, at: ContinuousClock.Instant)`.

- [ ] **Step 1: Add the failing cadence test**

Add this function to the executable harness and call it from
`ProductPetHarness.main()`:

```swift
func testPetSyncCadence() throws {
    let clock = ContinuousClock()
    let started = clock.now
    var cadence = PetSyncCadence()

    try expect(cadence.isDue(at: started), "first pet sync is immediate")

    cadence.record(
        result: PetSyncResult(
            petID: "rocky",
            digest: String(repeating: "a", count: 64),
            errorCode: nil
        ),
        at: started
    )
    try expect(
        !cadence.isDue(at: started.advanced(by: .seconds(29))),
        "successful pet sync waits 30 seconds"
    )
    try expect(
        cadence.isDue(at: started.advanced(by: .seconds(30))),
        "successful pet sync is due at 30 seconds"
    )

    let failedAt = started.advanced(by: .seconds(30))
    cadence.record(
        result: PetSyncResult(
            petID: "rocky",
            digest: String(repeating: "a", count: 64),
            errorCode: "sync_failed"
        ),
        at: failedAt
    )
    try expect(
        !cadence.isDue(at: failedAt.advanced(by: .seconds(4))),
        "failed pet sync waits five seconds"
    )
    try expect(
        cadence.isDue(at: failedAt.advanced(by: .seconds(5))),
        "failed pet sync retries at five seconds"
    )
}
```

- [ ] **Step 2: Run the harness and verify RED**

Run:

```bash
swift run --package-path companion product-pet-tests
```

Expected: compilation fails because `PetSyncCadence` is not defined.

- [ ] **Step 3: Implement the minimal cadence**

Create `PetSyncCadence.swift`:

```swift
import Foundation

public struct PetSyncCadence: Sendable {
    private var nextAttempt: ContinuousClock.Instant?

    public init() {}

    public func isDue(at now: ContinuousClock.Instant) -> Bool {
        guard let nextAttempt else { return true }
        return now >= nextAttempt
    }

    public mutating func record(
        result: PetSyncResult,
        at now: ContinuousClock.Instant
    ) {
        let interval: Duration = result.errorCode == nil
            ? .seconds(30)
            : .seconds(5)
        nextAttempt = now.advanced(by: interval)
    }
}
```

- [ ] **Step 4: Run the harness and verify GREEN**

Run:

```bash
swift run --package-path companion product-pet-tests
```

Expected: `product-pet-tests: PASS`.

- [ ] **Step 5: Commit the cadence**

```bash
git add companion/Sources/ProductPet/PetSyncCadence.swift \
  companion/Tests/ProductPetExecutableTests/main.swift
git commit -m "fix: schedule reliable pet synchronization"
```

### Task 2: Isolate Pet Synchronization From Action Failures

**Files:**
- Modify: `tools/product/tests/test_companion_packaging.py`
- Modify: `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`

**Interfaces:**
- Consumes: `PetSyncCadence` from Task 1 and the existing `PetSyncCoordinator.synchronize(client:) async -> PetSyncResult`.
- Produces: one serial loop that services the pet deadline before entering the action/snapshot `do` block.

- [ ] **Step 1: Replace the old rate-limit test with a failing integration contract**

Replace `test_companion_rate_limits_pet_synchronization` with:

```python
def test_companion_pet_sync_has_independent_serial_error_boundary():
    main = (
        ROOT / "companion/Sources/cardputer-companion/CardputerCompanionMain.swift"
    ).read_text()
    due = main.index("if petSyncCadence.isDue(at: now)")
    synchronize = main.index(
        "await petSync.synchronize(client: bridge)",
        due,
    )
    action_boundary = main.index("do {", synchronize)
    action = main.index("let action = try await bridge.pollAction()", action_boundary)

    assert due < synchronize < action_boundary < action
    assert "petSyncCadence.record(" in main[due:action_boundary]
    assert "nextPetSynchronization" not in main
    assert "retry in 5 seconds" in main
    assert "next check in 30 seconds" in main
```

- [ ] **Step 2: Run the targeted pytest and verify RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py::test_companion_pet_sync_has_independent_serial_error_boundary
```

Expected: FAIL because the current source still uses
`nextPetSynchronization` inside the action `do` block.

- [ ] **Step 3: Move the due pet stage before the action boundary**

In `CardputerCompanionMain.run`, replace
`nextPetSynchronization` with:

```swift
var petSyncCadence = PetSyncCadence()
```

At the top of each loop, before the action `do` block, add:

```swift
let now = clock.now
if petSyncCadence.isDue(at: now) {
    synchronizedPet = await petSync.synchronize(client: bridge)
    petSyncCadence.record(result: synchronizedPet, at: now)
    if let errorCode = synchronizedPet.errorCode {
        FileHandle.standardError.write(
            Data(
                "pet sync warning: \(errorCode); retry in 5 seconds\n".utf8
            )
        )
    } else {
        FileHandle.standardOutput.write(
            Data(
                "pet sync: \(synchronizedPet.petID); " +
                    "next check in 30 seconds\n".utf8
            )
        )
    }
}
```

Delete the old pet synchronization block from inside the action/snapshot
`do` block. Leave action polling, action execution, snapshot publication, and
the two-second sleep serialized on the same task.

- [ ] **Step 4: Run targeted Swift and Python tests**

Run:

```bash
swift run --package-path companion product-pet-tests
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py
```

Expected: both commands pass; the Swift harness prints
`product-pet-tests: PASS`.

- [ ] **Step 5: Inspect the diff for credential-safe logs and serial I/O**

Run:

```bash
git diff --check
git diff -- companion/Sources/cardputer-companion/CardputerCompanionMain.swift \
  companion/Sources/ProductPet/PetSyncCadence.swift \
  tools/product/tests/test_companion_packaging.py
```

Expected: no PIN, pairing code, configuration payload, `Task.detached`,
`async let`, or new network task appears.

- [ ] **Step 6: Commit the loop correction**

```bash
git add companion/Sources/cardputer-companion/CardputerCompanionMain.swift \
  tools/product/tests/test_companion_packaging.py
git commit -m "fix: isolate pet sync from action failures"
```

### Task 3: Release Gate, Companion Deployment, and HIL Pet Switch

**Files:**
- Modify: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`
- Generated: `dist/CardputerCompanion.app`
- Generated: `dist/private/cardputer_codex_companion-private-full.bin`

**Interfaces:**
- Consumes: the Task 1 cadence and Task 2 serial loop.
- Produces: an installed launchd Companion and device evidence that a changed pet reaches Cardputer within 30 seconds.

- [ ] **Step 1: Run the complete release gate**

Run:

```bash
scripts/verify_product_release.sh
```

Expected:

- all Python tests pass;
- all 24 host tests pass normally and under ASan/UBSan;
- ESP-IDF target build and partition validation pass;
- Swift release build and doctor pass;
- generic/private firmware packaging and Companion app signing pass;
- `git diff --check` passes.

- [ ] **Step 2: Install the rebuilt Companion**

Run:

```bash
python3 scripts/install_companion_launch_agent.py --load
launchctl print gui/$(id -u)/com.lynx.cardputer-companion
```

Expected: launchd reports `state = running`, and `program` points to the
main-repository `dist/CardputerCompanion.app`.

- [ ] **Step 3: Prepare an isolated HIL Codex home**

Stop the LaunchAgent so only one Companion accesses the Cardputer:

```bash
launchctl bootout \
  gui/$(id -u)/com.lynx.cardputer-companion
mkdir -p build/hil-codex-home/cache/tui-pets/v1
ln -s "$HOME/.codex/cache/tui-pets/v1/assets" \
  build/hil-codex-home/cache/tui-pets/v1/assets
```

Create `build/hil-codex-home/config.toml` with `apply_patch`:

```toml
[tui]
pet = "rocky"
```

Start the rebuilt Companion in a PTY with:

```bash
CODEX_HOME="$PWD/build/hil-codex-home" \
  dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  run --config \
  "$HOME/Library/Application Support/CardputerCodexCompanion/config.json"
```

Expected: the process remains running and reports a successful `rocky` pet
check without printing a PIN.

- [ ] **Step 4: Change the isolated selection and measure propagation**

Use `apply_patch` to change the HIL config:

```toml
[tui]
pet = "seedy"
```

Poll the PIN-authenticated `/api/v1/companion/pet` endpoint without printing
the PIN. Read the pairing value in memory from the existing Companion config,
send it only as the `X-Cardputer-Pairing` header, and print only:

```text
pet_id
digest prefix
last_result
elapsed seconds
```

Expected within 30 seconds:

- `pet_id` becomes `seedy`;
- digest differs from the recorded `rocky` digest;
- `last_result` is `ok` or `cached`;
- the Companion process remains alive.

- [ ] **Step 5: Restore the user's normal Companion**

Stop the foreground HIL Companion, then run:

```bash
python3 scripts/install_companion_launch_agent.py --load
launchctl print gui/$(id -u)/com.lynx.cardputer-companion
```

Poll `/api/v1/status` with retries.

Expected: version remains `1.0.28`; BLE, Wi-Fi, and Mac all report `OK`.
The user's actual selected pet is resynchronized by the restored LaunchAgent.

- [ ] **Step 6: Record evidence and commit**

Append a milestone to the progress document with:

- RED and GREEN command results;
- full release test counts;
- Companion executable SHA-256;
- private full-image SHA-256;
- HIL old/new pet IDs, digest prefixes, and elapsed time;
- final device and LaunchAgent states;
- CO status `Not required` because this is local development and USB/LAN
  device deployment.

Then run:

```bash
git add docs/2026-07-24-cardputer-codex-companion_PROGRESS.md
git commit -m "docs: record reliable pet synchronization deployment"
git status --short --branch
```

Expected: the feature branch is clean.

## Final Verification Checklist

- [ ] Cadence test was observed failing before production implementation.
- [ ] Integration contract was observed failing before loop restructuring.
- [ ] First run is immediately due.
- [ ] Success interval is exactly 30 seconds.
- [ ] Failure retry is exactly 5 seconds.
- [ ] Pet synchronization precedes and is independent of the action boundary.
- [ ] Cardputer HTTPS calls remain serialized.
- [ ] Complete release gate passes.
- [ ] HIL pet ID and digest change within 30 seconds.
- [ ] BLE, Wi-Fi, and Mac return `OK` after restoring launchd.
- [ ] No credential appears in logs, diffs, commits, or reported evidence.
