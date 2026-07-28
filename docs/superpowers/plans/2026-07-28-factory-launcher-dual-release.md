# Factory and Launcher Dual-Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and publish a complete Cardputer Codex Companion `1.3.0`
factory release and a same-source `1.3.0l` M5Launcher-compatible firmware,
while diagnosing incompatible partitions explicitly instead of reporting a
valid PIN as incorrect.

**Architecture:** The firmware accepts a build-time product-version definition,
so two isolated ESP-IDF build directories produce the factory and Launcher
runtime versions from one source tree. A pure storage-compatibility model is
shared by startup, the Cardputer UI, and the Web API; the ESP platform adapter
only translates the discovered `esp_partition_t`. Packaging produces a normal
factory image and an exact-boundary Launcher image, while the browser request
layer preserves HTTP status and structured error codes.

**Tech Stack:** ESP-IDF 5.5.4, C++20, M5Unified, Python 3.11/pytest, POSIX shell,
Node.js built-in test runner, ESP Web Tools 10, Swift Package Manager, Go,
esptool, GitHub Pages and GitHub Releases.

## Global Constraints

- Factory firmware and all Machine Agents use version `1.3.0`.
- Launcher-compatible firmware uses runtime and artifact version `1.3.0l`.
- Both firmware variants are built from the same commit and source tree.
- No Launcher branch, patch, binary modification, or maintained Launcher fork.
- M5Launcher `2.8.0` is the minimum supported Launcher version.
- Compatible storage is data/SPIFFS, labelled `storage`, and at least
  `0x1e0000` bytes.
- The Launcher image length is exactly `0x620000` bytes and contains no storage
  payload or Wi-Fi credentials.
- The official factory image is written at offset `0x0` and replaces Launcher.
- Public artifacts contain no Wi-Fi credentials, PINs, BLE keys, local paths,
  cached pets, or user configuration.
- Tests are written and observed failing before each production change.
- The attached Cardputer finishes on the official factory `1.3.0` build.

---

### Task 1: Versioned same-source firmware builds

**Files:**
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/test/host/test_product_types.cpp`
- Create: `firmware/test/host/test_product_types_launcher.cpp`
- Modify: `release/product-release.json`
- Modify: `companion/AppBundle/Info.plist`
- Modify: `companion/AudioDriver/Info.plist`
- Modify: `companion/Sources/CodexAppServer/JSONRPCProcess.swift`
- Modify: `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Modify: `scripts/build_windows_agent.sh`
- Modify: `scripts/package_windows_agent.sh`
- Modify: `scripts/mac_installer.py`
- Modify: `windows-agent/README.txt`
- Modify: `windows-agent/internal/codex/process.go`
- Modify: existing version assertions under `tools/product/tests/`

**Interfaces:**
- Consumes: `PROJECT_VER` supplied by ESP-IDF/CMake.
- Produces: `CARDPUTER_PRODUCT_VERSION`, default factory runtime `1.3.0`, and
  override runtime `1.3.0l`.
- Produces: release metadata keys
  `firmware.factory_version`, `firmware.launcher_version`, and
  `firmware.minimum_launcher_version`.

- [ ] **Step 1: Write failing runtime-version tests**

Add the factory assertion to `test_product_types.cpp`:

```cpp
static_assert(kProductVersion == "1.3.0");
```

Create `test_product_types_launcher.cpp`:

```cpp
#include <cassert>
#include "product/product_types.hpp"

int main() {
  static_assert(kProductVersion == "1.3.0l");
  assert(kProductVersion == "1.3.0l");
  return 0;
}
```

Register the second executable with:

```cmake
add_executable(test_product_types_launcher test_product_types_launcher.cpp)
target_include_directories(test_product_types_launcher PRIVATE ../../main)
target_compile_definitions(
  test_product_types_launcher
  PRIVATE CARDPUTER_PRODUCT_VERSION="1.3.0l"
)
add_test(NAME product_types_launcher COMMAND test_product_types_launcher)
```

- [ ] **Step 2: Run the tests and verify the expected failure**

Run:

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
ctest --test-dir build/product-host -R 'product_types' --output-on-failure
```

Expected: compilation fails because the existing default is `1.2.3` and
`CARDPUTER_PRODUCT_VERSION` cannot yet override it.

- [ ] **Step 3: Add the build-time version boundary**

Use this definition in `product_types.hpp`:

```cpp
#ifndef CARDPUTER_PRODUCT_VERSION
#define CARDPUTER_PRODUCT_VERSION "1.3.0"
#endif
inline constexpr std::string_view kProductVersion =
    CARDPUTER_PRODUCT_VERSION;
```

Allow ESP-IDF callers to override `PROJECT_VER`:

```cmake
if(NOT DEFINED PROJECT_VER)
  set(PROJECT_VER "1.3.0")
endif()
```

Pass it to the firmware component:

```cmake
target_compile_definitions(
    ${COMPONENT_LIB}
    PRIVATE CARDPUTER_PRODUCT_VERSION="${PROJECT_VER}")
```

Update product release metadata to:

```json
"version": "1.3.0",
"firmware": {
  "factory_version": "1.3.0",
  "launcher_version": "1.3.0l",
  "minimum_launcher_version": "2.8.0",
  "storage_label": "storage",
  "storage_minimum_bytes": 1966080
}
```

Update all Agent and installer runtime version outputs from `1.2.3` to
`1.3.0`; historical documents remain unchanged.

- [ ] **Step 4: Run focused version verification**

Run:

```bash
cmake --build build/product-host -j
ctest --test-dir build/product-host -R 'product_types' --output-on-failure
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_driver_bundle.py \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_mac_installer.py \
  tools/product/tests/test_windows_agent_packaging.py \
  tools/product/tests/test_public_release_artifacts.py
```

Expected: both host version executables and all product version assertions pass.

- [ ] **Step 5: Commit the version boundary**

```bash
git add firmware release companion scripts tools/product/tests \
  windows-agent
git commit -m "chore: advance dual release to 1.3.0"
```

---

### Task 2: Factory and Launcher firmware packaging

**Files:**
- Create: `tools/product/package_launcher_image.py`
- Create: `tools/product/verify_launcher_firmware.py`
- Create: `tools/product/tests/test_launcher_firmware.py`
- Modify: `tools/product/merge_product_image.py`
- Modify: `scripts/package_product_firmware.sh`
- Modify: `tools/product/verify_public_artifacts.py`
- Modify: `tools/product/tests/test_public_release_artifacts.py`
- Modify: `release/product-release.json`

**Interfaces:**
- Consumes: factory build directory `firmware/build` and Launcher build
  directory `firmware/build-launcher`.
- Produces:
  `dist/Cardputer-Codex-Companion-1.3.0-factory.bin`,
  `dist/Cardputer-Codex-Companion-1.3.0-app.bin`, and
  `dist/Cardputer-Codex-Companion-1.3.0l-launcher.bin`.
- Preserves aliases `dist/cardputer_codex_companion-full.bin` and
  `dist/cardputer_codex_companion.bin`.
- Produces: `pad_launcher_image(source: Path, output: Path,
  boundary: int = 0x620000) -> None`.

- [ ] **Step 1: Write failing Launcher image behavior tests**

Create tests with literal expected bytes:

```python
def test_launcher_image_is_padded_with_erased_flash_to_storage_boundary(tmp_path):
    source = tmp_path / "merged.bin"
    source.write_bytes(b"\xe9\x01" + b"\xff" * 30)
    output = tmp_path / "launcher.bin"
    pad_launcher_image(source, output)
    assert output.stat().st_size == 0x620000
    assert output.read_bytes()[:32] == source.read_bytes()
    assert output.read_bytes()[-4096:] == b"\xff" * 4096


def test_launcher_image_rejects_content_that_reaches_storage(tmp_path):
    source = tmp_path / "oversized.bin"
    source.write_bytes(b"\xff" * (0x620000 + 1))
    with pytest.raises(ValueError, match="storage boundary"):
        pad_launcher_image(source, tmp_path / "launcher.bin")
```

Add an integration assertion that reads the product partition CSV and checks
the `storage` offset and size before packaging.

- [ ] **Step 2: Run the tests and verify the expected import failure**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_launcher_firmware.py
```

Expected: collection fails because `package_launcher_image.py` does not exist.

- [ ] **Step 3: Implement exact-boundary packaging**

Implement the public function as:

```python
STORAGE_BOUNDARY = 0x620000


def pad_launcher_image(
    source: Path,
    output: Path,
    boundary: int = STORAGE_BOUNDARY,
) -> None:
    size = source.stat().st_size
    if size >= boundary:
        raise ValueError("merged image reaches storage boundary")
    output.parent.mkdir(parents=True, exist_ok=True)
    with source.open("rb") as reader, output.open("wb") as writer:
        shutil.copyfileobj(reader, writer)
        writer.write(b"\xff" * (boundary - size))
    output.chmod(0o600)
```

The CLI first calls the existing merged-image helper on
`firmware/build-launcher`, validates the product partition declaration, and
then pads the merged image. It must reject a non-erased byte in the `wifi_cfg`
range.

Update `package_product_firmware.sh` to:

1. package and version-copy the factory build;
2. configure and build `firmware/build-launcher` with
   `-DPROJECT_VER=1.3.0l`;
3. generate the exact-boundary Launcher artifact;
4. refresh the two compatibility aliases.

The Launcher verifier checks file length, erased `wifi_cfg`, embedded product
partition table, declared `storage`, and the application description version.
It extracts the app bytes beginning at the embedded table's first app offset
and runs:

```bash
"${idf_python}" -m esptool image_info --version 2 extracted-app.bin
```

The verifier requires `Detected image type: ESP32-S3` and
`App version: 1.3.0l` in that output.

- [ ] **Step 4: Run focused packaging tests**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_launcher_firmware.py \
  tools/product/tests/test_public_release_artifacts.py
python3 tools/product/verify_partition_layout.py
```

Expected: padding, oversize rejection, erased-region, artifact allowlist, and
partition declaration tests pass.

- [ ] **Step 5: Commit the dual firmware packager**

```bash
git add tools/product scripts/package_product_firmware.sh \
  release/product-release.json
git commit -m "feat: package factory and launcher firmware variants"
```

---

### Task 3: Runtime storage compatibility model and Cardputer diagnostics

**Files:**
- Create: `firmware/main/product/storage_compatibility.hpp`
- Create: `firmware/main/product/storage_compatibility.cpp`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/main/product/ui_model.hpp`
- Modify: `firmware/main/product/ui_model.cpp`
- Modify: `firmware/main/product/display.cpp`
- Create: `firmware/test/host/test_storage_compatibility.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/test/host/test_ui_model.cpp`
- Modify: `tools/product/tests/test_companion_packaging.py`

**Interfaces:**
- Produces: `StorageCompatibilityState { ready, missing, wrong_type,
  too_small }`.
- Produces:
  `constexpr StorageCompatibility evaluate_storage_compatibility(bool found,
  bool data_type, bool spiffs_subtype, uint32_t size_bytes)`.
- Produces:
  `constexpr std::string_view storage_compatibility_name(state)`.
- Produces: ESP-only
  `StorageCompatibility inspect_storage_compatibility()`.
- Produces: `UiModel::set_storage_compatibility(StorageCompatibility)`.

- [ ] **Step 1: Write failing pure storage-contract tests**

Create:

```cpp
static_assert(
    evaluate_storage_compatibility(false, false, false, 0).state ==
    StorageCompatibilityState::missing);
static_assert(
    evaluate_storage_compatibility(true, false, true, 0x1e0000).state ==
    StorageCompatibilityState::wrong_type);
static_assert(
    evaluate_storage_compatibility(true, true, false, 0x1e0000).state ==
    StorageCompatibilityState::wrong_type);
static_assert(
    evaluate_storage_compatibility(true, true, true, 0x1dffff).state ==
    StorageCompatibilityState::too_small);
static_assert(
    evaluate_storage_compatibility(true, true, true, 0x1e0000).state ==
    StorageCompatibilityState::ready);
```

Add UI assertions that a missing partition creates `STORAGE:MISSING` on the
DEVICE page and marks the pet surface as incompatible.

- [ ] **Step 2: Run the tests and verify the expected compile failure**

Run:

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
```

Expected: compilation fails because the storage model and UI setter are absent.

- [ ] **Step 3: Implement the pure contract and platform adapter**

Use these constants and result shape:

```cpp
inline constexpr uint32_t kRequiredStorageBytes = 0x1e0000;

enum class StorageCompatibilityState : uint8_t {
  ready,
  missing,
  wrong_type,
  too_small,
};

struct StorageCompatibility {
  StorageCompatibilityState state = StorageCompatibilityState::missing;
  uint32_t size_bytes = 0;
  [[nodiscard]] constexpr bool ready() const {
    return state == StorageCompatibilityState::ready;
  }
};
```

In `EspProductStartup::config()`, inspect `storage` once using
`esp_partition_find_first`. Translate its type, subtype, and size through the
pure evaluator. Publish the result to the UI and Web status before starting
pet/profile storage.

When storage is incompatible:

- skip `PetStore::start()` and profile-catalog task creation;
- mark profile initialization complete so Web startup does not wait;
- keep `config()` successful so BLE, Wi-Fi, Web, and navigation start;
- log exactly one incompatibility line with reason and discovered size.

The DEVICE page adds `STORAGE:<state>`. On the pet page, the display renders:

```text
PARTITION ERROR
<MISSING|WRONG TYPE|TOO SMALL>
USE FACTORY 1.3.0
OR LAUNCHER 2.8+
```

Page navigation and DEVICE PIN display remain accessible.

- [ ] **Step 4: Run host and structural verification**

Run:

```bash
cmake --build build/product-host -j
ctest --test-dir build/product-host \
  -R 'storage_compatibility|ui_model|product_controller' \
  --output-on-failure
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py
```

Expected: four storage states, DEVICE text, pet diagnostic, and non-blocking
startup assertions pass.

- [ ] **Step 5: Commit runtime compatibility handling**

```bash
git add firmware tools/product/tests/test_companion_packaging.py
git commit -m "fix: diagnose incompatible storage layouts"
```

---

### Task 4: Structured Web API errors and correct login feedback

**Files:**
- Modify: `firmware/main/product/product_web.hpp`
- Modify: `firmware/main/product/product_web.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/test/host/test_product_web.cpp`
- Create: `web/src/device_api.js`
- Modify: `web/src/app.js`
- Modify: `scripts/build_web_assets.py`
- Create: `web/tests/device_api.test.js`
- Modify: `tools/product/tests/test_web_assets.py`
- Regenerate: `firmware/main/product/web_assets.hpp`
- Modify: `scripts/verify_product_release.sh`

**Interfaces:**
- Produces:
  `product_web_set_storage_compatibility(StorageCompatibility status)`.
- Adds status JSON:
  `"storage":{"state":"READY","size_bytes":1966080}`.
- Produces profile/pet HTTP 503:
  `{"error":"partition_incompatible","reason":"missing"}`.
- Produces browser `DeviceApiError(status, code, responseText)` and
  `loginErrorMessage(error) -> string`.

- [ ] **Step 1: Write failing firmware status/error tests**

Add host assertions for:

```cpp
const StorageCompatibility missing{
    .state = StorageCompatibilityState::missing,
    .size_bytes = 0,
};
assert(product_web_storage_json(missing) ==
       "{\"state\":\"MISSING\",\"size_bytes\":0}");
assert(product_web_partition_error_json(missing) ==
       "{\"error\":\"partition_incompatible\",\"reason\":\"missing\"}");
```

Cover `wrong_type`, `too_small`, and `ready` with hand-written literal JSON.

- [ ] **Step 2: Write failing browser error tests**

Create Node tests:

```javascript
test("maps rejected fetch to device unreachable", async () => {
  const fetchFailure = async () => { throw new TypeError("Failed to fetch"); };
  await assert.rejects(
    requestDevice(fetchFailure, "/api/v1/status", {}, "12345678"),
    error => error.status === 0 &&
      loginErrorMessage(error) === "设备不可达，请检查 IP、Wi-Fi 和证书访问"
  );
});

test("maps authenticated partition 503 without blaming the PIN", () => {
  const error = new DeviceApiError(
    503,
    "partition_incompatible",
    "{\"reason\":\"missing\"}"
  );
  assert.equal(
    loginErrorMessage(error),
    "设备分区不兼容，请重新安装官方 Factory 固件或 Launcher 2.8+ 兼容固件"
  );
});
```

Also cover HTTP 401, HTTP 403, and an unrelated HTTP 500.

- [ ] **Step 3: Run both failing test groups**

Run:

```bash
cmake --build build/product-host -j
ctest --test-dir build/product-host -R product_web --output-on-failure
node --test web/tests/device_api.test.js
```

Expected: C++ compilation and Node module loading fail because the new
interfaces do not exist.

- [ ] **Step 4: Implement firmware error semantics**

Store compatibility state atomically in `product_web.cpp`. Extend status JSON
through `product_web_storage_json`.

After PIN authorization and setup-complete validation, every profile handler
and mutating pet handler calls a common guard:

```cpp
bool compatible_storage_available(httpd_req_t* request) {
  const StorageCompatibility status = current_storage_compatibility();
  if (status.ready()) return true;
  const std::string json = product_web_partition_error_json(status);
  json_response(request, json.c_str(), "503 Service Unavailable");
  return false;
}
```

The status endpoint always remains available to a valid PIN, including on an
incompatible partition layout.

- [ ] **Step 5: Implement browser error preservation**

`device_api.js` exports:

```javascript
class DeviceApiError extends Error {
  constructor(status, code, responseText) {
    super(code || `HTTP ${status}`);
    this.status = status;
    this.code = code || "";
    this.responseText = responseText || "";
  }
}

async function requestDevice(fetchImpl, path, options, pairing) {
  const request = {...options};
  request.headers = {
    ...(request.headers || {}),
    "X-Cardputer-Pairing": pairing,
    "Content-Type": "application/json",
  };
  let response;
  try {
    response = await fetchImpl(path, request);
  } catch (error) {
    throw new DeviceApiError(0, "network_unreachable", String(error));
  }
  const text = await response.text();
  let payload = {};
  try { payload = text ? JSON.parse(text) : {}; } catch (_) {}
  if (!response.ok) {
    throw new DeviceApiError(response.status, payload.error || "", text);
  }
  return payload;
}
```

`loginErrorMessage` maps the exact messages from the approved design.
`authenticate()` displays that function's result. The status call remains
first, followed by the profile catalog call.

Update `build_web_assets.py` to concatenate `device_api.js` before `app.js`,
regenerate the header, and add the Node test command to the release gate.

- [ ] **Step 6: Run focused Web verification**

Run:

```bash
cmake --build build/product-host -j
ctest --test-dir build/product-host -R product_web --output-on-failure
node --test web/tests/device_api.test.js
python3 scripts/build_web_assets.py
python3 scripts/build_web_assets.py --check
PYTHONPATH=. uv run pytest -q tools/product/tests/test_web_assets.py
```

Expected: firmware JSON, all four browser message branches, and generated asset
consistency pass.

- [ ] **Step 7: Commit Web diagnostics**

```bash
git add firmware web scripts/build_web_assets.py \
  scripts/verify_product_release.sh tools/product/tests/test_web_assets.py
git commit -m "fix: distinguish pin network and partition errors"
```

---

### Task 5: Project-owned factory Web Serial installer

**Files:**
- Create: `web-installer/index.html`
- Create: `web-installer/manifest.json`
- Create: `tools/product/package_web_installer.py`
- Create: `tools/product/tests/test_web_installer.py`
- Create: `.github/workflows/pages.yml`
- Modify: `release/product-release.json`
- Modify: `tools/product/verify_public_artifacts.py`

**Interfaces:**
- Produces: HTTPS page at the repository's GitHub Pages
  `/Cardputer_Codex_Companion/web-installer/`.
- Produces:
  `dist/CardputerCompanion-1.3.0-web-installer.zip` with deterministic entry
  timestamps and modes.
- Consumes GitHub Release asset:
  `Cardputer-Codex-Companion-1.3.0-factory.bin`.
- Manifest build: chip family `ESP32-S3`, one part, path to the factory
  release asset, offset `0`.

- [ ] **Step 1: Write failing installer manifest tests**

Create:

```python
def test_factory_web_installer_targets_esp32s3_offset_zero():
    manifest = json.loads((ROOT / "web-installer/manifest.json").read_text())
    assert manifest["name"] == "Cardputer Codex Companion"
    assert manifest["version"] == "1.3.0"
    assert manifest["new_install_prompt_erase"] is False
    assert manifest["new_install_improv_wait_time"] == 0
    assert manifest["builds"] == [{
        "chipFamily": "ESP32-S3",
        "parts": [{
            "path": (
                "https://github.com/IIIIOvOIIII/"
                "Cardputer_Codex_Companion/releases/download/v1.3.0/"
                "Cardputer-Codex-Companion-1.3.0-factory.bin"
            ),
            "offset": 0,
        }],
    }]
```

Add HTML assertions for an HTTPS/Web Serial warning, factory-reset warning,
the `esp-web-install-button`, relative `manifest.json`, and ESP Web Tools major
version 10. Add a packaging test that opens the ZIP and asserts its complete
entry set is exactly `index.html` and `manifest.json`.

- [ ] **Step 2: Run the installer test and verify missing-file failure**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_web_installer.py
```

Expected: failure because `web-installer/manifest.json` is absent.

- [ ] **Step 3: Implement the static installer and Pages workflow**

The page imports:

```html
<script
  type="module"
  src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module">
</script>
```

The activation element is:

```html
<esp-web-install-button manifest="manifest.json">
  <button slot="activate">Install Factory Firmware 1.3.0</button>
  <span slot="unsupported">Use desktop Chrome or Edge with Web Serial.</span>
  <span slot="not-allowed">Open this installer over HTTPS.</span>
</esp-web-install-button>
```

The page states that the operation removes Launcher and resets Wi-Fi, PIN,
profiles, pets, and BLE pairing. It links to the Launcher-compatible release
asset for users who want to retain Launcher.

The Pages workflow checks out `main`, uploads only `web-installer/`, and deploys
through `actions/deploy-pages`; it has `pages: write` and `id-token: write`
permissions and one production concurrency group.

`package_web_installer.py` writes both files to
`CardputerCompanion-1.3.0-web-installer.zip` in sorted order, uses
`SOURCE_DATE_EPOCH` for each ZIP timestamp, and stores `index.html` as `0644`
and `manifest.json` as `0644`.

- [ ] **Step 4: Run installer and workflow validation**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_web_installer.py \
  tools/product/tests/test_public_release_artifacts.py
python3 - <<'PY'
import json
from pathlib import Path
json.loads(Path("web-installer/manifest.json").read_text())
print("manifest json valid")
PY
```

Expected: manifest, page contract, public artifact allowlist, and JSON parsing
pass.

- [ ] **Step 5: Commit the installer**

```bash
git add web-installer .github/workflows/pages.yml \
  release/product-release.json tools/product
git commit -m "feat: add factory web serial installer"
```

---

### Task 6: Documentation and complete release gate

**Files:**
- Modify: `README.md`
- Modify: `README.zh-CN.md`
- Modify: `scripts/verify_product_release.sh`
- Modify: `tools/product/verify_public_artifacts.py`
- Modify: `tools/product/audit_public_release.py`
- Modify: `tools/product/tests/test_public_release_artifacts.py`
- Modify: `release/product-release.json`
- Update: `docs/2026-07-28-web-login-pin-auth_PROGRESS.md`

**Interfaces:**
- Produces: one `dist/1.3.0-SHA256SUMS` covering Factory `1.3.0`, Launcher
  `1.3.0l`, macOS `1.3.0`, Windows `1.3.0`, and
  `CardputerCompanion-1.3.0-web-installer.zip`.
- Produces: bilingual operator guidance for both installation channels.

- [ ] **Step 1: Write failing documentation/release assertions**

Extend the public-release test to require:

```python
for document in (readme, chinese):
    assert "1.3.0" in document
    assert "1.3.0l" in document
    assert "2.8.0" in document
    assert "Cardputer-Codex-Companion-1.3.0-factory.bin" in document
    assert "Cardputer-Codex-Companion-1.3.0l-launcher.bin" in document
```

The complete-set test requires all new artifacts and rejects all `1.2.3`
release artifacts.

- [ ] **Step 2: Run the tests and verify stale-release failure**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_public_release_artifacts.py
```

Expected: failure because the README and allowlist still describe `1.2.3`.

- [ ] **Step 3: Update documentation and release checks**

README installation guidance starts with a two-channel choice:

- **Factory 1.3.0 (recommended):** use the Web Serial installer or flash the
  versioned image at `0x0`; Launcher and all device setup are removed.
- **Launcher 1.3.0l:** update Launcher to `2.8.0` or later and install the
  Launcher artifact through Launcher; use Launcher for subsequent updates.

Document the `PARTITION ERROR` recovery, the Web distinction between PIN and
partition failure, and app-only fixed-offset limitations.

Update `verify_product_release.sh` to:

1. run Python, host, sanitizer, Node, Swift, Go, installer, and credential
   audit gates;
2. build factory and Launcher firmware from clean build directories;
3. verify both embedded runtime versions;
4. verify exact partition and image boundaries;
5. hash every approved public artifact into `1.3.0-SHA256SUMS`;
6. verify every checksum immediately.

- [ ] **Step 4: Run all non-hardware release gates**

Run:

```bash
scripts/verify_product_release.sh
```

Expected: zero failing Python, C++ host, sanitizer, Node, Swift, Go,
packaging, signing, checksum, and credential-audit checks.

- [ ] **Step 5: Record the software-gate milestone and commit**

Update the progress document with test counts, artifact paths, version
evidence, checksums, and the next hardware step.

```bash
git add README.md README.zh-CN.md scripts tools release \
  docs/2026-07-28-web-login-pin-auth_PROGRESS.md
git commit -m "docs: publish 1.3.0 dual install guidance"
```

---

### Task 7: Launcher HIL, factory HIL, publication, and closeout

**Files:**
- Update: `docs/2026-07-28-web-login-pin-auth_PROGRESS.md`
- Update: `/Users/nicholasliao/clawd/memory/2026-07-28.md`
- Generated but not committed: `dist/*`

**Interfaces:**
- Consumes: attached Cardputer serial device resolved by USB VID/PID and chip
  identity, never by an unvalidated wildcard.
- Produces: Launcher `1.3.0l` and factory `1.3.0` HIL evidence.
- Produces: GitHub tag/release `v1.3.0`, release assets, pushed `origin/main`,
  and GitHub Pages installer.

- [ ] **Step 1: Resolve and back up the exact attached device**

Identify the serial port, read the ESP32-S3 MAC and flash ID, and record the
current partition table. Back up the current NVS and any readable product
storage to:

```text
~/Library/Application Support/CardputerCodexCompanion/device-backups/
```

Use directory mode `0700` and file mode `0600`. Record only paths and SHA-256
digests in the progress document; do not record credential contents.

- [ ] **Step 2: Verify the Launcher path**

Install official M5Launcher `2.8.x` for Cardputer, then install
`Cardputer-Codex-Companion-1.3.0l-launcher.bin` through Launcher.

Collect serial and API evidence proving:

```text
runtime version: 1.3.0l
Launcher retained: yes
storage: READY and size >= 0x1e0000
profile catalog ready: yes
Web valid PIN: /api/v1/status HTTP 200
Web profiles: /api/v1/profiles HTTP 200
pet sync: successful
BLE HID: authenticated input received
Agent: authenticated heartbeat
panic/watchdog/reboot loop: none
```

If Launcher reports insufficient space, use its displayed repartition choice;
do not shrink the required storage contract.

Observe the running Launcher-installed firmware for 10 uninterrupted minutes
while Agent heartbeat, pet animation, and BLE remain active.

- [ ] **Step 3: Verify the factory path**

Flash `Cardputer-Codex-Companion-1.3.0-factory.bin` at offset `0x0`. Confirm
the product partition table by reading flash address `0x8000`, then complete
the first-run setup.

Collect evidence proving:

```text
runtime version: 1.3.0
ota_0 offset: 0x20000
storage offset/size: 0x620000 / 0x1e0000
profile catalog ready: yes
Web valid PIN: /api/v1/status HTTP 200
Web profiles: /api/v1/profiles HTTP 200
pet sync: successful
BLE HID: authenticated input received
Agent: authenticated heartbeat
panic/watchdog/reboot loop: none
```

Leave the device on this factory build.

Observe the final factory firmware for 10 uninterrupted minutes while Agent
heartbeat, pet animation, and BLE remain active.

- [ ] **Step 4: Re-run the clean release gate**

Run:

```bash
scripts/verify_product_release.sh
git diff --check
git status --short
```

Expected: the complete gate passes, no secret/generated artifact is staged,
and only intended documentation evidence remains modified.

- [ ] **Step 5: Commit final evidence and push**

```bash
git add docs/2026-07-28-web-login-pin-auth_PROGRESS.md
git commit -m "test: record 1.3.0 dual path hardware verification"
GIT_SSH_COMMAND='ssh -i /Users/nicholasliao/.ssh/id_co_openclaw -o IdentitiesOnly=yes' \
  git push origin main
```

- [ ] **Step 6: Publish and verify the public release**

Create GitHub release `v1.3.0` from the pushed commit and upload only the
allowlisted artifacts plus `1.3.0-SHA256SUMS`. Verify each downloadable asset
hash against the local manifest.

Confirm the Pages workflow succeeds, open the HTTPS installer page, and verify
that it loads the `1.3.0` manifest and offers an ESP32-S3 installation without
starting a destructive flash.

- [ ] **Step 7: Update workspace memory and report paths**

Append the operation summary, release commit/tag, HIL results, public URLs,
root cause, and partition-layout lesson to
`/Users/nicholasliao/clawd/memory/2026-07-28.md`. Do not include PINs, Wi-Fi
credentials, BLE keys, or backup contents.

Report the exact local paths for:

```text
dist/Cardputer-Codex-Companion-1.3.0-factory.bin
dist/Cardputer-Codex-Companion-1.3.0-app.bin
dist/Cardputer-Codex-Companion-1.3.0l-launcher.bin
dist/1.3.0-SHA256SUMS
```
