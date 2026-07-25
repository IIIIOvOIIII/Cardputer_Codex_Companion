# Web Key Result Feedback and Login Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an in-page success/failure dialog for key changes, mask the login PIN, correct login form spacing and apply the approved login copy.

**Architecture:** Keep the existing dependency-free single-page Web UI and generated firmware asset pipeline. Add one reusable result dialog above the key editor, route both save and restore-to-passthrough through the existing Profile PUT, and preserve the current rollback behavior when publication fails.

**Tech Stack:** Static HTML/CSS/JavaScript, Python pytest, generated C++ header, ESP-IDF 5.5.4, esptool.

## Global Constraints

- Login title is exactly `Codex Companion Login`.
- Login explanation is exactly `请输入设备PIN码进行鉴权`.
- The login PIN must be an eight-digit masked password input.
- Successful key changes close the editor and show a result dialog that auto-closes after 1.5 seconds.
- Failed key changes roll back the browser-side binding, keep the editor open and show a result dialog until manual confirmation.
- Restore-to-passthrough must publish through Profile PUT rather than changing browser memory only.
- Result error content must be assigned through `textContent`, never `innerHTML`.
- Do not change Profile JSON, API routes, NVS formats, BLE HID or Mac Companion behavior.
- Deployment must use app-only flashing at `0x20000` to preserve PIN, Wi-Fi, Profile and BLE bonds.

---

### Task 1: Login Copy, PIN Mask and Result Dialog Structure

**Files:**
- Modify: `tools/product/tests/test_web_assets.py:17-29`
- Modify: `web/src/index.html:15-26,147`
- Modify: `web/src/style.css:1`

**Interfaces:**
- Consumes: Existing `auth-screen`, `pin-form`, `login-pin`, `.modal` and `.modal-card` elements.
- Produces: DOM elements `result-modal`, `result-title`, `result-message`, `result-close`; CSS classes `.result-modal`, `.result-card`, `.result-success`, `.result-error`.

- [ ] **Step 1: Write failing asset tests**

Replace the old login-copy assertion and add focused structure/style tests:

```python
def test_web_ui_starts_with_masked_pin_authentication() -> None:
    html = Path("web/src/index.html").read_text()
    script = Path("web/src/app.js").read_text()

    assert 'id="auth-screen"' in html
    assert 'id="app-shell"' in html
    assert 'id="pin-form"' in html
    assert 'id="login-pin" type="password"' in html
    assert 'inputmode="numeric"' in html
    assert 'maxlength="8"' in html
    assert 'pattern="[0-9]{8}"' in html
    assert "Codex Companion Login" in html
    assert "请输入设备PIN码进行鉴权" in html
    assert "先输入设备 PIN" not in html
    assert "PIN 显示在 Cardputer 屏幕上" not in html
    assert "authenticate" in script
    assert "showApp" in script


def test_login_spacing_and_result_dialog_structure() -> None:
    html = Path("web/src/index.html").read_text()
    css = Path("web/src/style.css").read_text()

    assert "#pin-form" in css
    assert "gap:16px" in css
    assert "margin-top:18px" in css
    assert 'id="result-modal"' in html
    assert 'role="alertdialog"' in html
    assert 'id="result-title"' in html
    assert 'id="result-message"' in html
    assert 'id="result-close"' in html
    assert ".result-modal" in css
    assert "z-index:20" in css
```

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```bash
python3 -m pytest tools/product/tests/test_web_assets.py \
  -k 'masked_pin_authentication or login_spacing_and_result_dialog_structure' -q
```

Expected: both tests fail because the old copy, unmasked input, missing spacing and missing result dialog are still present.

- [ ] **Step 3: Implement the approved HTML and CSS**

Change the authentication card to:

```html
<section class="panel auth-card">
  <h2>Codex Companion Login</h2>
  <p class="hint">请输入设备PIN码进行鉴权</p>
  <form id="pin-form">
    <label>设备 PIN
      <input id="login-pin" type="password" inputmode="numeric" autocomplete="current-password" maxlength="8" pattern="[0-9]{8}" placeholder="8 位数字">
    </label>
    <button class="primary" type="submit">进入控制台</button>
  </form>
  <p id="auth-error" class="error" aria-live="polite"></p>
</section>
```

Add the result dialog after `key-modal`:

```html
<div id="result-modal" class="modal result-modal hidden" role="alertdialog" aria-modal="true" aria-labelledby="result-title" aria-describedby="result-message">
  <section id="result-card" class="modal-card result-card">
    <h2 id="result-title">修改成功</h2>
    <p id="result-message"></p>
    <div class="modal-actions">
      <button id="result-close" class="primary" type="button">确定</button>
    </div>
  </section>
</div>
```

Add scoped CSS:

```css
#pin-form{display:flex;flex-direction:column;gap:16px;margin-top:18px}
#pin-form .primary{width:100%}
.result-modal{z-index:20}
.result-card{width:min(380px,100%)}
.result-card h2{margin:0}
.result-card.result-success{border-color:#37afa4}
.result-card.result-error{border-color:#d76666}
.result-card.result-success h2{color:#70e1d5}
.result-card.result-error h2{color:#ff9b9b}
```

- [ ] **Step 4: Run the focused tests and verify GREEN**

Run:

```bash
python3 -m pytest tools/product/tests/test_web_assets.py \
  -k 'masked_pin_authentication or login_spacing_and_result_dialog_structure' -q
```

Expected: 2 passed.

- [ ] **Step 5: Commit Task 1**

```bash
git add web/src/index.html web/src/style.css tools/product/tests/test_web_assets.py
git commit -m "feat: polish web login and result dialog"
```

### Task 2: Publish Key Changes Through the Result Dialog

**Files:**
- Modify: `tools/product/tests/test_web_assets.py:60-70`
- Modify: `web/src/app.js:8-38`

**Interfaces:**
- Consumes: `publishProfile(false)`, `profile.bindings`, `bindingIndex()`, `closeKeyModal()`.
- Produces: `showResult(kind, message)`, `closeResult()`, `restorePassthrough()`, and a shared 1500 ms success timer.

- [ ] **Step 1: Write failing JavaScript contract tests**

Replace the old inline-error test with:

```python
def test_key_editor_publishes_with_result_dialog_and_rollback() -> None:
    html = Path("web/src/index.html").read_text()
    script = Path("web/src/app.js").read_text()

    assert ">保存并发布</button>" in html
    assert "let resultTimer=0" in script
    assert "function showResult(kind,message)" in script
    assert '$("result-message").textContent=message' in script
    assert "resultTimer=setTimeout(closeResult,1500)" in script
    assert 'showResult("success","键位配置已发布到设备")' in script
    assert 'showResult("error",`修改失败：${error.message}`)' in script
    assert "profile.bindings[index]=previous" in script
    assert "async function restorePassthrough()" in script
    assert "await publishProfile(false)" in script
    assert '$("delete-mapping").onclick=restorePassthrough' in script
```

- [ ] **Step 2: Run the contract test and verify RED**

Run:

```bash
python3 -m pytest tools/product/tests/test_web_assets.py \
  -k key_editor_publishes_with_result_dialog_and_rollback -q
```

Expected: failure because the result state functions and asynchronous restore publication do not exist.

- [ ] **Step 3: Implement result state management**

Add state and functions:

```javascript
let resultTimer=0;

function closeResult(){
  if(resultTimer){clearTimeout(resultTimer);resultTimer=0}
  $("result-modal").classList.add("hidden")
}

function showResult(kind,message){
  closeResult();
  const success=kind==="success";
  $("result-title").textContent=success?"修改成功":"修改失败";
  $("result-message").textContent=message;
  $("result-card").classList.toggle("result-success",success);
  $("result-card").classList.toggle("result-error",!success);
  $("result-modal").classList.remove("hidden");
  if(success)resultTimer=setTimeout(closeResult,1500);
  else $("result-close").focus()
}
```

Update save behavior:

```javascript
async function applyEditor(event){
  event.preventDefault();
  const index=bindingIndex();
  const previous=profile.bindings[index];
  $("key-save-error").textContent="";
  $("apply-key").disabled=true;
  $("delete-mapping").disabled=true;
  try{
    profile.bindings[index]=readEditorAction();
    await publishProfile(false);
    closeKeyModal();
    showResult("success","键位配置已发布到设备")
  }catch(error){
    profile.bindings[index]=previous;
    $("key-save-error").textContent=error.message;
    showResult("error",`修改失败：${error.message}`)
  }finally{
    $("apply-key").disabled=false;
    $("delete-mapping").disabled=false
  }
}
```

Implement restore publication:

```javascript
async function restorePassthrough(){
  const index=bindingIndex();
  const previous=profile.bindings[index];
  $("key-save-error").textContent="";
  $("apply-key").disabled=true;
  $("delete-mapping").disabled=true;
  profile.bindings[index]={kind:"passthrough"};
  try{
    await publishProfile(false);
    closeKeyModal();
    showResult("success","键位已恢复直通并发布到设备")
  }catch(error){
    profile.bindings[index]=previous;
    $("key-save-error").textContent=error.message;
    showResult("error",`修改失败：${error.message}`)
  }finally{
    $("apply-key").disabled=false;
    $("delete-mapping").disabled=false
  }
}
```

Bind the handlers:

```javascript
$("delete-mapping").onclick=restorePassthrough;
$("result-close").onclick=closeResult;
```

- [ ] **Step 4: Verify JavaScript and Web tests**

Run:

```bash
node --check web/src/app.js
python3 -m pytest tools/product/tests/test_web_assets.py -q
```

Expected: JavaScript syntax check exits 0 and all Web asset tests pass.

- [ ] **Step 5: Commit Task 2**

```bash
git add web/src/app.js tools/product/tests/test_web_assets.py
git commit -m "feat: show key publication results in page"
```

### Task 3: Generate, Release, Flash and Verify Firmware 1.0.27

**Files:**
- Modify: `firmware/CMakeLists.txt:3`
- Modify: `firmware/main/product/product_types.hpp:8-10`
- Modify: `firmware/test/host/test_product_types.cpp:8-9`
- Regenerate: `firmware/main/product/web_assets.hpp`
- Modify: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`

**Interfaces:**
- Consumes: Approved Web sources and `scripts/build_web_assets.py`.
- Produces: Firmware 1.0.27 application image and private full image.

- [ ] **Step 1: Write the failing version test**

Change the product version expectations to:

```cpp
static_assert(kProductVersion == std::string_view{"1.0.27"});
static_assert(kProductBootTitle == std::string_view{"CARDPUTER CODEX 1.0.27"});
```

- [ ] **Step 2: Run the version test and verify RED**

Run:

```bash
cmake --build build/product-host --target test_product_types -j4
```

Expected: compilation fails on both static assertions because the current product version is 1.0.26.

- [ ] **Step 3: Update both firmware version sources and generate assets**

Set:

```cmake
set(PROJECT_VER "1.0.27")
```

and:

```cpp
inline constexpr std::string_view kProductVersion = "1.0.27";
inline constexpr std::string_view kProductBootTitle =
    "CARDPUTER CODEX 1.0.27";
```

Then run:

```bash
python3 scripts/build_web_assets.py
cmake --build build/product-host --target test_product_types -j4
build/product-host/test_product_types
```

Expected: asset header changes and product type test passes.

- [ ] **Step 4: Run the complete release gate**

Run:

```bash
scripts/verify_product_release.sh
```

Expected: Python tests, 21 host tests, 21 ASan/UBSan tests, Web asset check, ESP-IDF build, product partition and DIRAM checks, Swift build/doctor, generic/private packaging and secret-exclusion checks all pass.

- [ ] **Step 5: Flash the application partition**

Run:

```bash
.tools/espressif/python_env/idf5.5_py3.14_env/bin/python -m esptool \
  --chip esp32s3 --port /dev/cu.usbmodem21201 --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x20000 firmware/build/cardputer_codex_companion.bin
```

Expected: `Hash of data verified`.

- [ ] **Step 6: Verify flashed bytes and runtime**

Run:

```bash
.tools/espressif/python_env/idf5.5_py3.14_env/bin/python -m esptool \
  --chip esp32s3 --port /dev/cu.usbmodem21201 \
  --before default_reset --after hard_reset verify_flash \
  0x20000 firmware/build/cardputer_codex_companion.bin
curl -sk https://192.168.1.195/api/v1/status
curl -sk https://192.168.1.195/ | \
  grep -E 'Codex Companion Login|请输入设备PIN码进行鉴权|result-modal'
```

Expected: digest matches, status reports version 1.0.27 with Wi-Fi OK, and live HTML contains all three new UI markers.

- [ ] **Step 7: Record evidence and commit the release**

Update the progress document with test counts, flash verification, runtime status, artifact size and SHA-256. Then run:

```bash
git diff --check
git add firmware/CMakeLists.txt \
  firmware/main/product/product_types.hpp \
  firmware/test/host/test_product_types.cpp \
  firmware/main/product/web_assets.hpp \
  docs/2026-07-24-cardputer-codex-companion_PROGRESS.md
git commit -m "chore: release firmware 1.0.27"
git status --short
```

Expected: commit succeeds and the project worktree is clean. The repository has no configured remote, so no push is possible.
