# Cardputer Pet Cycle Normalization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Release Cardputer Codex Companion `1.1.6` with conservative,
pixel-exact pet-cycle normalization so a proven partial repetition such as
`A,B,C,D,A,B` expands to `A,B,C,D,A,B,C,D` without making assumptions about
other atlas layouts.

**Architecture:** `PetTranscoder` continues to render and remove only fully
transparent source cells. A small pure helper finds the shortest period whose
required complete RGB565 frame comparisons all match and whose repeated suffix
contains at least two frames; otherwise it preserves the existing full visible
sequence. The unchanged CCPT v1 encoder still receives exactly eight frames,
so firmware decoding and rendering remain unchanged.

**Tech Stack:** Swift 6, CoreGraphics, ImageIO, CryptoKit, C++20, ESP-IDF
5.5.4, Python 3.11, pytest, Bash, esptool.

## Global Constraints

- Compare complete rendered `[UInt16]` RGB565 arrays exactly; do not use hashes,
  approximate similarity, bounding boxes, source coordinates, or pet IDs.
- A candidate period must have at least two observed repeated tail frames.
- Choose the smallest proven period; if none exists, cycle the complete visible
  sequence exactly as the current implementation does.
- Remove only fully transparent source cells.
- Continue emitting exactly eight frames per state in unchanged CCPT v1.
- Do not modify the firmware frame decoder, pet state mapping, LCD submission,
  animation rate, microphone policy, BLE HID, or Wi-Fi control behavior.
- All current live release-version surfaces become `1.1.6`; historical
  validation records keep their historical versions.
- Update configured hardware only by app-only flash at `0x20000`.

---

## File Structure

- `companion/Sources/ProductPet/PetTranscoder.swift`: strict period proof and
  eight-frame expansion.
- `companion/Tests/ProductPetExecutableTests/main.swift`: RGB565 transcode
  regression fixtures and fallback coverage.
- `firmware/CMakeLists.txt`,
  `firmware/main/product/product_types.hpp`,
  `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`,
  `companion/Sources/CodexAppServer/JSONRPCProcess.swift`,
  `companion/AppBundle/Info.plist`,
  `companion/AudioDriver/Info.plist`, and `scripts/mac_installer.py`: live
  `1.1.6` version surfaces.
- `firmware/test/host/test_product_types.cpp`,
  `firmware/test/host/test_ui_model.cpp`,
  `tools/product/tests/test_audio_release.py`,
  `tools/product/tests/test_audio_driver_bundle.py`, and
  `tools/product/tests/test_mac_installer.py`: version and installer gates.
- `docs/2026-07-27-cardputer-pet-cycle-normalization_PROGRESS.md`: milestone
  evidence.
- `docs/validation/cardputer-pet-cycle-normalization-release.md`: final
  automated, installation, flash, sync, and physical validation evidence.
- `dist/1.1.6-SHA256SUMS`: checksums for final release artifacts.

---

### Task 1: Normalize Only Strictly Proven Pixel Periods

**Files:**

- Modify: `companion/Tests/ProductPetExecutableTests/main.swift`
- Modify: `companion/Sources/ProductPet/PetTranscoder.swift`

**Interfaces:**

- Consumes: ordered non-empty `[RenderedFrame]` values after transparent cells
  have been removed.
- Produces:

```swift
private func expandedFrames(
    from visibleFrames: [RenderedFrame]
) -> [[UInt16]]
```

The result always contains eight complete 96x104 RGB565 pixel arrays.

- [ ] **Step 1: Add the periodic and conservative fixtures**

Extend the ProductPet executable fixture coverage with these state sequences:

```text
idle:    A B C D A B -- --  -> A B C D A B C D
working: A B C D A X -- --  -> A B C D A X A B
waiting: A B C D E A -- --  -> A B C D E A A B
review:  A B C D E F -- --  -> A B C D E F A B
failed:  A B C D A B C D     -> A B C D A B C D
```

`X` must equal B except for one RGB565 output pixel. Assert equality using
`bundle.framePayload(state:frame:)`, and assert the distinguishing payloads are
not equal so a fixture mistake cannot produce a false pass.

- [ ] **Step 2: Run the focused RED test**

Run:

```bash
swift run --package-path companion product-pet-tests
```

Expected: FAIL because the current periodic `idle` output maps frame 6 to A
instead of C and frame 7 to B instead of D. The distinct, one-tail-repeat, and
one-pixel-difference fallback assertions must already describe current
conservative behavior.

- [ ] **Step 3: Implement the minimal strict-period helper**

Add this behavior inside `PetTranscoder`:

```swift
private func expandedFrames(
    from visibleFrames: [RenderedFrame]
) -> [[UInt16]] {
    let period: Int?
    if visibleFrames.count >= 3 {
        period = (1...(visibleFrames.count - 2)).first { candidate in
            (candidate..<visibleFrames.count).allSatisfy { index in
                visibleFrames[index].pixels ==
                    visibleFrames[index % candidate].pixels
            }
        }
    } else {
        period = nil
    }
    let source = Array(visibleFrames.prefix(period ?? visibleFrames.count))
    return (0..<8).map { source[$0 % source.count].pixels }
}
```

Replace only the current direct `(0..<8).map` expansion with this helper.
Retain the existing non-empty guard and transparent-cell filtering.

- [ ] **Step 4: Run focused GREEN and release-mode tests**

Run:

```bash
swift run --package-path companion product-pet-tests
swift run --package-path companion -c release product-pet-tests
```

Expected: both commands print `product-pet-tests: PASS`.

- [ ] **Step 5: Commit the normalization**

```bash
git add companion/Sources/ProductPet/PetTranscoder.swift \
  companion/Tests/ProductPetExecutableTests/main.swift
git commit -m "fix: normalize proven pet animation cycles"
```

---

### Task 2: Advance Every Live Release Surface to 1.1.6

**Files:**

- Modify: `tools/product/tests/test_audio_release.py`
- Modify: `tools/product/tests/test_audio_driver_bundle.py`
- Modify: `tools/product/tests/test_mac_installer.py`
- Modify: `firmware/test/host/test_product_types.cpp`
- Modify: `firmware/test/host/test_ui_model.cpp`
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify:
  `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Modify: `companion/Sources/CodexAppServer/JSONRPCProcess.swift`
- Modify: `companion/AppBundle/Info.plist`
- Modify: `companion/AudioDriver/Info.plist`
- Modify: `scripts/mac_installer.py`

**Interfaces:**

- Consumes: all current live `1.1.5` release strings.
- Produces: one release identity, `1.1.6`, enforced by Python and host tests.

- [ ] **Step 1: Change only version expectations to 1.1.6**

Set `VERSION = "1.1.6"` in `test_audio_release.py`; update current driver,
installer fixture, product type, and UI expectations to `1.1.6`. Do not change
the production version strings yet.

- [ ] **Step 2: Run the RED version gates**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_audio_driver_bundle.py \
  tools/product/tests/test_mac_installer.py
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
ctest --test-dir build/product-host -R 'product_types|ui_model' \
  --output-on-failure
```

Expected: failures name the remaining `1.1.5` firmware, Companion, Codex client,
app plist, HAL plist, and installer values.

- [ ] **Step 3: Update every live production version**

Change the seven live production files listed above from `1.1.5` to `1.1.6`,
including
both installer message and `EXPECTED_VERSION`. Confirm scope with:

```bash
rg -n '1\.1\.5' firmware companion scripts tools \
  -g '!firmware/managed_components/**' \
  -g '!firmware/build/**' \
  -g '!companion/.build/**'
```

Expected: no live source or test match remains.

- [ ] **Step 4: Run the GREEN version gates**

Run:

```bash
scripts/build_audio_driver.sh
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_audio_driver_bundle.py \
  tools/product/tests/test_mac_installer.py
cmake --build build/product-host -j
ctest --test-dir build/product-host -R 'product_types|ui_model' \
  --output-on-failure
```

Expected: all selected Python and host tests pass.

- [ ] **Step 5: Commit the release identity**

```bash
git add firmware/CMakeLists.txt firmware/main/product/product_types.hpp \
  firmware/test/host/test_product_types.cpp \
  firmware/test/host/test_ui_model.cpp \
  companion/AppBundle/Info.plist companion/AudioDriver/Info.plist \
  companion/Sources/cardputer-companion/CardputerCompanionMain.swift \
  companion/Sources/CodexAppServer/JSONRPCProcess.swift \
  scripts/mac_installer.py tools/product/tests/test_audio_release.py \
  tools/product/tests/test_audio_driver_bundle.py \
  tools/product/tests/test_mac_installer.py
git commit -m "chore: bump companion release to 1.1.6"
```

---

### Task 3: Build, Install, Flash, and Prove the Corrected Release

**Files:**

- Modify:
  `docs/2026-07-27-cardputer-pet-cycle-normalization_PROGRESS.md`
- Create: `docs/validation/cardputer-pet-cycle-normalization-release.md`
- Generate: `dist/1.1.6-SHA256SUMS`

**Interfaces:**

- Consumes: committed Task 1 behavior and Task 2 version identity.
- Produces: signed Mac artifacts, an app-only firmware image, installed Agent,
  synchronized pet digest, flash verification, and release evidence.

- [ ] **Step 1: Run the complete release gate**

Run:

```bash
scripts/verify_product_release.sh
```

Expected: Python, normal host, sanitizer host, ESP-IDF, Swift ProductPet,
ProductAudio, ProductGATT, ProductConfiguration, C audio ring, packaging,
codesign, partition, memory, and checksum checks all exit zero.

- [ ] **Step 2: Create and verify final checksums**

Run:

```bash
shasum -a 256 \
  firmware/build/cardputer_codex_companion.bin \
  dist/cardputer_codex_companion-full.bin \
  dist/private/cardputer_codex_companion-private-full.bin \
  dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  dist/CardputerCompanion-mac-installer/install.sh \
  > dist/1.1.6-SHA256SUMS
shasum -a 256 -c dist/1.1.6-SHA256SUMS
```

Expected: all five artifacts report `OK`.

- [ ] **Step 3: Upgrade the installed Mac Companion without exposing the PIN**

Run:

```bash
dist/CardputerCompanion-mac-installer/install.sh install \
  --config "$HOME/Library/Application Support/CardputerCodexCompanion/config.json"
dist/CardputerCompanion-mac-installer/install.sh status
```

Expected: APP, CONFIG, HAL, BRIDGE, AUDIO, and authenticated LAN are OK and the
LaunchAgent is RUNNING. Do not print the config or PIN.

- [ ] **Step 4: Verify corrected pet synchronization**

Wait for the installed Agent's next sync, then query
`/api/v1/companion/pet` using the protected config's existing pairing header.
Record only pet ID, content digest, format version, storage used, transaction
state, and last result. Require:

```bash
python3 - <<'PY'
import json
import subprocess
from pathlib import Path

config = json.loads(
    (
        Path.home()
        / "Library/Application Support/CardputerCodexCompanion/config.json"
    ).read_text()
)
curl_config = "\n".join(
    (
        "silent",
        "show-error",
        "insecure",
        "max-time = 5",
        f'header = "X-Cardputer-Pairing: {config["pairing"]}"',
        f'url = "{config["device"].rstrip("/")}/api/v1/companion/pet"',
        "",
    )
)
result = subprocess.run(
    ["/usr/bin/curl", "--config", "-"],
    input=curl_config,
    text=True,
    capture_output=True,
    check=True,
)
status = json.loads(result.stdout)
safe = {
    key: status.get(key)
    for key in (
        "pet_id", "digest", "format_version", "storage_used",
        "transaction", "last_result",
    )
}
print(json.dumps(safe, sort_keys=True))
PY
```

Require:

```text
pet_id=rocky
format_version=1
transaction.active=false
last_result=ok
```

The ProductPet regression proves the local corrected payload ordering; the
coordinator's commit-digest check plus the device's committed digest proves
that exact generated bundle reached the device.

- [ ] **Step 5: Perform the app-only flash and independent verification**

Resolve exactly one `/dev/cu.usbmodem*` target. Abort rather than guessing when
zero or multiple targets exist. With the ESP-IDF Python environment, run:

```bash
cardputer_ports="$(
  find /dev -maxdepth 1 -type c -name 'cu.usbmodem*' -print | sort
)"
test -n "${cardputer_ports}"
test "$(
  printf '%s\n' "${cardputer_ports}" | wc -l | tr -d ' '
)" -eq 1
cardputer_port="${cardputer_ports}"
idf_python="$(
  find "$PWD/.tools/espressif/python_env" \
    -path '*/bin/python' -type f -print | sort | tail -n 1
)"
test -x "${idf_python}"
"${idf_python}" -m esptool --chip esp32s3 --port "${cardputer_port}" \
  --before default_reset --after hard_reset \
  write_flash 0x20000 firmware/build/cardputer_codex_companion.bin
"${idf_python}" -m esptool --chip esp32s3 --port "${cardputer_port}" \
  --before default_reset --after hard_reset \
  verify_flash 0x20000 firmware/build/cardputer_codex_companion.bin
```

Expected: write completes and `verify_flash` reports matching content. Do not
write the full image or any offset other than `0x20000`.

- [ ] **Step 6: Verify post-flash state without opening serial**

Use HTTPS-only observation because opening this Cardputer's USB serial port
causes `USB_UART_CHIP_RESET`. Require 20 consecutive samples over at least 40
seconds with:

```text
version=1.1.6
ble=OK
wifi=OK
companion=OK
microphone.state=READY
```

Also require the committed pet digest to remain unchanged and no active pet
transaction.

Use this sampler, which reads only the device URL from the protected config and
does not open `/dev/cu.usbmodem*`:

```bash
python3 - <<'PY'
import json
import subprocess
import time
from pathlib import Path

config = json.loads(
    (
        Path.home()
        / "Library/Application Support/CardputerCodexCompanion/config.json"
    ).read_text()
)
url = config["device"].rstrip("/") + "/api/v1/status"
deadline = time.monotonic() + 90
while True:
    ready = subprocess.run(
        ["/usr/bin/curl", "-ksS", "--max-time", "4", url],
        text=True,
        capture_output=True,
    )
    if ready.returncode == 0:
        break
    if time.monotonic() >= deadline:
        raise SystemExit("device did not return after app-only flash")
    time.sleep(2)

expected = ("1.1.6", "OK", "OK", "OK", "READY")
samples = []
for index in range(20):
    result = subprocess.run(
        ["/usr/bin/curl", "-ksS", "--max-time", "4", url],
        text=True,
        capture_output=True,
        check=True,
    )
    payload = json.loads(result.stdout)
    sample = (
        payload.get("version"),
        payload.get("ble"),
        payload.get("wifi"),
        payload.get("companion"),
        payload.get("microphone", {}).get("state"),
    )
    if sample != expected:
        raise SystemExit(f"unexpected status sample {index}: {sample}")
    samples.append(sample)
    if index != 19:
        time.sleep(2)
print(f"HTTPS stability samples: {len(samples)}/20")
PY
```

Expected: `HTTPS stability samples: 20/20`. Re-run the authenticated pet-status
probe from Step 4 and require the digest to be unchanged with no active
transaction.

- [ ] **Step 7: Obtain the physical animation gate**

Observe the connected WAITING or WORKING state for at least three complete
eight-frame cycles. Require frames C and D to recur in the second half rather
than a second A/B-only tail. If direct observation is unavailable, stop before
claiming the symptom fixed and request one user confirmation.

- [ ] **Step 8: Record evidence and commit release records**

Update the progress document and create the validation record with exact test
counts, build sizes, checksum, installed status, device status, pet digest
prefix, app-only flash result, and physical gate result. Never record the PIN
or other credentials.

```bash
git add \
  docs/2026-07-27-cardputer-pet-cycle-normalization_PROGRESS.md \
  docs/validation/cardputer-pet-cycle-normalization-release.md
git commit -m "docs: record companion 1.1.6 release"
```

- [ ] **Step 9: Final repository and release audit**

Run:

```bash
git status --short --branch
git log --oneline -5
shasum -a 256 -c dist/1.1.6-SHA256SUMS
```

Expected: the worktree is clean, the normalization/version/release commits are
present, and every final artifact checksum reports `OK`.
