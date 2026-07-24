# Web Profile Sparse Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Web-configured UTF-8 string and other key mappings publish reliably, persist across reload/reboot, and save directly from the key editor.

**Architecture:** Keep the existing `/api/v1/profile` envelope and 224-entry binding array, but encode passthrough bindings as JSON `null` and accept both sparse and legacy representations. Treat NVS persistence as part of the publish transaction, then make the Web modal call that publish transaction directly.

**Tech Stack:** ESP-IDF 5.5.4, C++20, cJSON, NVS, dependency-free HTML/CSS/JavaScript, Python 3/pytest, CMake host tests, Chrome extension control, esptool.

## Global Constraints

- The `bindings` array remains exactly 224 entries.
- JSON `null` means the existing safe passthrough action.
- The decoder accepts both `null` and legacy `{"kind":"passthrough"}` entries.
- Non-passthrough action enum values and fields remain unchanged.
- The active in-memory Profile changes only after NVS set and commit succeed.
- Persistence failure returns HTTP 500 `profile_persist_failed`.
- The key modal remains open and retains its values when publication fails.
- Pairing PIN, Wi-Fi password and Profile contents must not enter logs.
- Bump the firmware patch version from `1.0.21` to `1.0.22`.
- Upgrade the configured device by flashing only the application partition at `0x20000`.

---

## File Structure

- `firmware/main/product/product_web.hpp`: host-testable sparse-binding and activation policy.
- `firmware/main/product/product_web.cpp`: Profile JSON encode/decode, NVS persistence and HTTP transaction ordering.
- `firmware/test/host/test_product_web.cpp`: host regression tests for sparse and activation policy.
- `tools/product/tests/test_product_web_sparse_profile.py`: embedded-source regression guard and exact sparse-size fixture.
- `web/src/index.html`: modal save label and inline error surface.
- `web/src/app.js`: editor action construction, save/publish transaction and failure restoration.
- `tools/product/tests/test_web_assets.py`: Web source contract tests.
- `firmware/CMakeLists.txt`, `firmware/main/product/product_types.hpp`, `firmware/test/host/test_product_types.cpp`: version `1.0.22`.
- `firmware/main/product/web_assets.hpp`: generated embedded Web bundle.
- `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`: RED/GREEN, build, flash and HIL evidence.

---

### Task 1: Sparse Profile JSON and Atomic Persistence

**Files:**

- Modify: `firmware/main/product/product_web.hpp`
- Modify: `firmware/main/product/product_web.cpp`
- Modify: `firmware/test/host/test_product_web.cpp`
- Create: `tools/product/tests/test_product_web_sparse_profile.py`

**Interfaces:**

- Consumes: `ActionKind::passthrough`, `Profile`, `validate_profile()`, cJSON, NVS namespace `product`, key `profile`.
- Produces: `product_web_binding_uses_sparse_null(ActionKind)`, `ProductWebProfileActivation`, `product_web_profile_activation(bool)`, sparse Profile GET/PUT/load behavior.

- [ ] **Step 1: Add failing host policy tests**

Append the following assertions to `firmware/test/host/test_product_web.cpp`:

```cpp
  assert(product_web_binding_uses_sparse_null(ActionKind::passthrough));
  assert(!product_web_binding_uses_sparse_null(ActionKind::text_utf8));
  assert(!product_web_binding_uses_sparse_null(ActionKind::hid_chord));
  assert(product_web_profile_activation(true) ==
         ProductWebProfileActivation::replace_active);
  assert(product_web_profile_activation(false) ==
         ProductWebProfileActivation::keep_active);
```

- [ ] **Step 2: Add the failing embedded-source regression test**

Create `tools/product/tests/test_product_web_sparse_profile.py`:

```python
import json
from pathlib import Path


SOURCE = Path("firmware/main/product/product_web.cpp")


def test_sparse_default_profile_fits_nvs_string_limit() -> None:
    payload = {
        "name": "SAFE",
        "revision": 1,
        "bindings": [None] * 224,
    }
    encoded = json.dumps(
        payload,
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode()
    assert len(encoded) == 1161
    assert len(encoded) < 4000


def test_product_web_uses_sparse_null_and_atomic_persistence() -> None:
    source = SOURCE.read_text()
    assert "cJSON_CreateNull()" in source
    assert "cJSON_IsNull(item)" in source
    assert "esp_err_t persist_profile(" in source
    assert '"{\\"error\\":\\"profile_persist_failed\\"}"' in source
    persist = source.index("persist_profile(json)")
    activate = source.index("g_profile = std::move(candidate)")
    assert persist < activate
```

- [ ] **Step 3: Run the targeted tests and verify RED**

Run:

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_product_web
build/product-host/test_product_web
uv run pytest tools/product/tests/test_product_web_sparse_profile.py -q
```

Expected:

- C++ compilation fails because the sparse policy symbols do not exist.
- pytest reports the missing `cJSON_CreateNull()` and atomic persistence markers.

- [ ] **Step 4: Add the host-testable policy**

Add to `firmware/main/product/product_web.hpp` after `product_web_pin_load_action()`:

```cpp
constexpr bool product_web_binding_uses_sparse_null(ActionKind kind) {
  return kind == ActionKind::passthrough;
}

enum class ProductWebProfileActivation : uint8_t {
  keep_active,
  replace_active,
};

constexpr ProductWebProfileActivation product_web_profile_activation(
    bool persistence_succeeded) {
  return persistence_succeeded
             ? ProductWebProfileActivation::replace_active
             : ProductWebProfileActivation::keep_active;
}
```

- [ ] **Step 5: Emit sparse nulls**

In `profile_json()` inside `firmware/main/product/product_web.cpp`, replace the
binding loop with:

```cpp
  for (const KeyBinding& binding : profile.bindings) {
    const KeyAction& action = binding.action;
    if (product_web_binding_uses_sparse_null(action.kind)) {
      cJSON_AddItemToArray(bindings, cJSON_CreateNull());
      continue;
    }
    cJSON_AddItemToArray(
        bindings, action_json(action.kind, action.modifiers, action.usages,
                              action.usage_count, action.text,
                              &action.sequence,
                              action.device, action.codex));
  }
```

- [ ] **Step 6: Decode sparse nulls while preserving legacy input**

In the `parse_profile()` binding loop, initialize each action and accept `null`
before calling `parse_leaf()`:

```cpp
    const cJSON* item = cJSON_GetArrayItem(bindings, index);
    KeyAction& action = output.bindings[index].action;
    if (cJSON_IsNull(item)) {
      action = KeyAction{};
      continue;
    }
    if (!parse_leaf(item, action.kind, action.modifiers, action.usages,
                    action.usage_count, action.text, &action.device,
                    &action.codex)) {
      return false;
    }
```

Do not remove support for the legacy object form; it continues through
`parse_leaf()`.

- [ ] **Step 7: Return persistence errors**

Replace `persist_profile()` with:

```cpp
esp_err_t persist_profile(const char* json) {
  nvs_handle_t handle;
  esp_err_t result =
      nvs_open(kProductNvsNamespace, NVS_READWRITE, &handle);
  if (result != ESP_OK) return result;
  result = nvs_set_str(handle, kProfileNvsKey, json);
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result;
}
```

- [ ] **Step 8: Make PUT activation atomic**

In `put_profile_handler()`, replace the current activation and persistence block
with:

```cpp
  const esp_err_t persisted = persist_profile(json);
  if (product_web_profile_activation(persisted == ESP_OK) ==
      ProductWebProfileActivation::keep_active) {
    cJSON_free(json);
    cJSON_Delete(encoded);
    return json_response(
        request,
        "{\"error\":\"profile_persist_failed\"}",
        "500 Internal Server Error");
  }
  g_profile = std::move(candidate);
  const esp_err_t result = json_response(request, json);
```

Keep `cJSON_free(json)` and `cJSON_Delete(encoded)` after the response. Do not
log the encoded Profile or persistence input.

- [ ] **Step 9: Run targeted tests and verify GREEN**

Run:

```bash
cmake --build build/product-host --target test_product_web
build/product-host/test_product_web
uv run pytest tools/product/tests/test_product_web_sparse_profile.py -q
```

Expected: all commands exit 0 and pytest reports `2 passed`.

- [ ] **Step 10: Commit Task 1**

```bash
git add \
  firmware/main/product/product_web.hpp \
  firmware/main/product/product_web.cpp \
  firmware/test/host/test_product_web.cpp \
  tools/product/tests/test_product_web_sparse_profile.py
git commit -m "fix: persist sparse web profiles"
```

---

### Task 2: Save and Publish Directly from the Key Modal

**Files:**

- Modify: `tools/product/tests/test_web_assets.py`
- Modify: `web/src/index.html`
- Modify: `web/src/app.js`
- Generate: `firmware/main/product/web_assets.hpp`

**Interfaces:**

- Consumes: existing `publishProfile()`, `profile.bindings`, `bindingIndex()`, `api()`.
- Produces: `readEditorAction()`, async `applyEditor()`, `key-save-error`, modal save transaction.

- [ ] **Step 1: Add failing Web contract tests**

Append to `tools/product/tests/test_web_assets.py`:

```python
def test_key_editor_saves_and_publishes_with_inline_errors() -> None:
    html = Path("web/src/index.html").read_text()
    script = Path("web/src/app.js").read_text()

    assert 'id="key-save-error"' in html
    assert ">保存并发布</button>" in html
    assert "function readEditorAction()" in script
    assert "async function applyEditor(event)" in script
    assert "await publishProfile(false)" in script
    assert '$("key-save-error").textContent=error.message' in script
```

- [ ] **Step 2: Verify Web RED**

Run:

```bash
uv run pytest \
  tools/product/tests/test_web_assets.py::test_key_editor_saves_and_publishes_with_inline_errors \
  -q
```

Expected: FAIL because the modal still says `应用到按键` and has no inline error.

- [ ] **Step 3: Add the modal error surface and final label**

In `web/src/index.html`, replace the modal hint/actions tail with:

```html
      <p class="hint">动作摘要：保存后会显示在当前键帽上。</p>
      <p id="key-save-error" class="error" aria-live="polite"></p>
      <div class="modal-actions">
        <button id="delete-mapping" type="button">恢复直通</button>
        <button id="apply-key" class="primary" type="submit">保存并发布</button>
      </div>
```

- [ ] **Step 4: Separate editor parsing from publication**

Replace the existing synchronous `applyEditor()` in `web/src/app.js` with:

```javascript
function readEditorAction(){
  const kind=$("action-kind").value;
  let action={kind};
  if(kind==="hid_chord"){
    action.modifiers=chordDraft.modifiers;
    action.usages=chordDraft.usages.slice(0,6);
  }else if(kind==="text_utf8"){
    action.text=$("text").value;
  }else if(kind==="input_sequence"){
    action.sequence=$("sequence").value.trim()
      ?JSON.parse($("sequence").value):[];
  }else if(kind==="device_action"){
    action.device=$("device-action").value;
  }else if(kind==="codex_action"){
    action.codex=$("codex-action").value;
  }
  return action;
}
```

- [ ] **Step 5: Make Profile publication reusable**

Replace `publishProfile()` with:

```javascript
async function publishProfile(announce=true){
  profile.name=$("profile-name").value;
  profile=await api("/api/v1/profile",{
    method:"PUT",
    body:JSON.stringify(profile)
  });
  $("profile-name").value=profile.name;
  draw();
  if(announce)alert("已发布");
  return profile;
}
```

- [ ] **Step 6: Publish inside the modal transaction**

Add the async editor handler:

```javascript
async function applyEditor(event){
  event.preventDefault();
  const index=bindingIndex();
  const previous=profile.bindings[index];
  $("key-save-error").textContent="";
  $("apply-key").disabled=true;
  profile.bindings[index]=readEditorAction();
  try{
    await publishProfile(false);
    closeKeyModal();
  }catch(error){
    profile.bindings[index]=previous;
    $("key-save-error").textContent=error.message;
  }finally{
    $("apply-key").disabled=false;
  }
}
```

Update `openKeyModal()` to clear the prior error:

```javascript
function openKeyModal(index){
  selected=index;
  $("key-save-error").textContent="";
  loadEditor();
  $("key-modal").classList.remove("hidden");
  $("modal-title").textContent=`修改按键 ${labels[index]}`;
}
```

Keep `$ ("key-form").onsubmit=applyEditor` bound to the async function. The
handler catches its own publication errors.

- [ ] **Step 7: Generate the embedded assets**

Run:

```bash
python3 scripts/build_web_assets.py
```

Expected: `firmware/main/product/web_assets.hpp` is regenerated.

- [ ] **Step 8: Verify Web GREEN**

Run:

```bash
uv run pytest tools/product/tests/test_web_assets.py -q
python3 scripts/build_web_assets.py --check
```

Expected: all Web tests pass and the generated asset check exits 0.

- [ ] **Step 9: Commit Task 2**

```bash
git add \
  web/src/index.html \
  web/src/app.js \
  firmware/main/product/web_assets.hpp \
  tools/product/tests/test_web_assets.py
git commit -m "fix: publish mappings from web editor"
```

---

### Task 3: Version and Full Release Gate

**Files:**

- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify: `firmware/test/host/test_product_types.cpp`
- Modify: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`
- Build: `firmware/build/cardputer_codex_companion.bin`
- Build: `dist/private/cardputer_codex_companion-private-full.bin`

**Interfaces:**

- Consumes: completed sparse firmware and Web assets.
- Produces: firmware `1.0.22`, verified application image and private full image.

- [ ] **Step 1: Add the failing version expectation**

Change `firmware/test/host/test_product_types.cpp` to:

```cpp
  static_assert(kProductVersion == std::string_view{"1.0.22"});
  static_assert(kProductBootTitle == std::string_view{"CARDPUTER CODEX 1.0.22"});
```

- [ ] **Step 2: Verify version RED**

Run:

```bash
cmake --build build/product-host --target test_product_types
```

Expected: compilation fails because production constants still contain
`1.0.21`.

- [ ] **Step 3: Bump production version**

Set:

```cmake
set(PROJECT_VER "1.0.22")
```

in `firmware/CMakeLists.txt`, and set:

```cpp
inline constexpr std::string_view kProductVersion = "1.0.22";
inline constexpr std::string_view kProductBootTitle =
    "CARDPUTER CODEX 1.0.22";
```

in `firmware/main/product/product_types.hpp`.

- [ ] **Step 4: Verify targeted version GREEN**

Run:

```bash
cmake --build build/product-host --target test_product_types
build/product-host/test_product_types
```

Expected: both commands exit 0.

- [ ] **Step 5: Run the full release gate**

Run:

```bash
scripts/verify_product_release.sh
```

Expected:

- Python tests pass with zero failures.
- Normal and ASan/UBSan CTest runs pass with zero failures.
- Web asset check passes.
- ESP-IDF 5.5.4 target build passes.
- Product partition and memory gates pass.
- Swift release build and Companion doctor run.
- Generic/private firmware packaging and secret exclusion pass.

- [ ] **Step 6: Record build evidence**

Append a timestamped milestone to
`docs/2026-07-24-cardputer-codex-companion_PROGRESS.md` with:

- exact Python and CTest pass counts;
- target DIRAM headroom;
- application and private full image byte sizes;
- SHA-256 hashes from the release gate;
- current status `Ready for app-only flash`, not yet hardware-verified.

- [ ] **Step 7: Commit Task 3**

```bash
git add \
  firmware/CMakeLists.txt \
  firmware/main/product/product_types.hpp \
  firmware/test/host/test_product_types.cpp \
  docs/2026-07-24-cardputer-codex-companion_PROGRESS.md
git commit -m "chore: release firmware 1.0.22"
```

---

### Task 4: App-Only Flash and Real-Device Verification

**Files:**

- Read: `firmware/build/cardputer_codex_companion.bin`
- Read: `dist/private/cardputer_codex_companion-private-full.bin`
- Modify: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`
- Modify: `/Users/nicholasliao/clawd/memory/2026-07-24.md`

**Interfaces:**

- Consumes: release-gated `1.0.22` application image and connected
  `/dev/cu.usbmodem21201`.
- Produces: flashed Cardputer, persisted UTF-8 test mapping, Chrome/API/serial
  evidence, final artifact path.

- [ ] **Step 1: Resolve and verify the exact serial target**

Run:

```bash
ls -l /dev/cu.usbmodem*
.tools/espressif/python_env/idf5.5_py3.14_env/bin/python \
  -m esptool --chip esp32s3 \
  -p /dev/cu.usbmodem21201 chip_id
```

Expected: exactly the connected Cardputer ESP32-S3 responds. Stop without
flashing if the target is absent or ambiguous.

- [ ] **Step 2: Flash only the application partition**

Run:

```bash
.tools/espressif/python_env/idf5.5_py3.14_env/bin/python \
  -m esptool --chip esp32s3 \
  -p /dev/cu.usbmodem21201 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x20000 \
  firmware/build/cardputer_codex_companion.bin
```

Expected: esptool reports `Hash of data verified`; NVS-backed PIN, Wi-Fi and
BLE bond state are preserved.

- [ ] **Step 3: Capture clean serial boot evidence**

Open `/dev/cu.usbmodem21201` at 115200 baud for at least 45 seconds and verify:

- `App version:      1.0.22`;
- HTTPS server listening and product runtime started;
- no Guru Meditation, abort, stack overflow or reboot loop.

- [ ] **Step 4: Reconnect Chrome and publish the UTF-8 test mapping**

Using the existing Chrome Cardputer tab:

1. authenticate with the existing PIN already held by the page;
2. select Layer `0 - Keyboard`;
3. open key `F`;
4. choose `中文字符串 / UTF-8 文本`;
5. enter `字符串调试测试`;
6. click `保存并发布`;
7. click `重新载入`;
8. verify the F keycap still reads `文本 字符串调试测试`.

Expected: no alert error and no Chrome console `Failed to fetch`.

- [ ] **Step 5: Verify persistence through authenticated API and reboot**

Use the local Companion config only as an input and print no PIN:

```python
import json
import pathlib
import ssl
import urllib.request

config = json.loads(
    (
        pathlib.Path.home()
        / "Library/Application Support/CardputerCodexCompanion/config.json"
    ).read_text()
)
request = urllib.request.Request(
    config["device"].rstrip("/") + "/api/v1/profile",
    headers={"X-Cardputer-Pairing": config["pairing"]},
)
with urllib.request.urlopen(
    request,
    context=ssl._create_unverified_context(),
    timeout=10,
) as response:
    profile = json.loads(response.read())
assert profile["bindings"][33] == {
    "kind": "text_utf8",
    "text": "字符串调试测试",
}
```

Then hard reset the Cardputer without reflashing, repeat the same safe summary
check, and confirm revision/mapping remain present.

- [ ] **Step 6: Run concurrent Profile reliability sampling**

With the Mac LaunchAgent running, perform at least 20 authenticated Profile GET
requests and two no-op revision-correct PUT requests. Record only status,
duration, response size, revision and binding 33 summary.

Expected:

- zero disconnects, TLS resets, HTTP failures or timeouts;
- sparse Profile response remains below 4,000 bytes for the test mapping;
- serial contains no `uri handler execution failed`.

- [ ] **Step 7: Verify the physical string action**

Focus a Mac text input, press physical Cardputer key `F` once, and confirm the
active Mac application receives exactly:

```text
字符串调试测试
```

If direct UI observation requires the user, pause only at this physical-action
step and ask them to press `F`; continue automatically after confirmation.

- [ ] **Step 8: Record final evidence and memory**

Append to the project progress document and
`/Users/nicholasliao/clawd/memory/2026-07-24.md`:

- root cause;
- commits;
- test/build counts;
- flashed offset and esptool result;
- Chrome/API/serial/physical-key results;
- final artifact paths and SHA-256 hashes;
- no secrets.

- [ ] **Step 9: Commit final evidence**

```bash
git add docs/2026-07-24-cardputer-codex-companion_PROGRESS.md
git commit -m "docs: record sparse profile hardware verification"
```

Run:

```bash
git status --short
git remote -v
```

Expected: project worktree clean. Push only if a remote exists.

