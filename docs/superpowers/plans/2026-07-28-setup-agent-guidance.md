# SETUP Agent Guidance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver firmware `1.2.2` with smaller SETUP-only text, visible IP and PIN during Agent onboarding, and an interactive macOS installer that asks for an RFC1918 IPv4 address.

**Architecture:** Keep the onboarding state machine hardware-independent by passing the current IP and PIN into its content formatter from the product controller. Select a smaller body scale only when the UI model is on the onboarding page. Normalize interactive installer IP input into the existing HTTPS device URL configuration contract so stored configuration remains backward compatible.

**Tech Stack:** C++20 host tests, ESP-IDF/M5Unified firmware, Python 3/pytest installer tests, Bash packaging, Swift/Go version surfaces.

## Global Constraints

- Only `UiPage::onboarding` uses the smaller body text size.
- DEVICE, CODEX, SYNC, and SETTINGS keep `kDisplayBodyTextSize = 2`.
- Agent setup displays the current IP, real eight-digit PIN, root installer command, and heartbeat wait state at the same time.
- Interactive installation accepts only RFC1918 IPv4 and persists `https://<address>`.
- Existing JSON configuration continues accepting validated HTTPS RFC1918 URLs and `.local` hostnames.
- Advance active product version surfaces from `1.2.1` to `1.2.2`; do not rewrite historical plans, progress logs, or validation records.
- Build both `dist/cardputer_codex_companion.bin` and `dist/cardputer_codex_companion-full.bin`.

---

### Task 1: Add Runtime IP and PIN to Agent Onboarding

**Files:**
- Modify: `firmware/test/host/test_onboarding.cpp`
- Modify: `firmware/main/product/onboarding.hpp`
- Modify: `firmware/main/product/onboarding.cpp`
- Modify: `firmware/main/product/product_controller.cpp`

**Interfaces:**
- Consumes: `product_wifi_ipv4() -> const char*` and `product_web_pairing_code() -> const char*`.
- Produces: `OnboardingController::content(std::string_view ipv4 = {}, std::string_view pairing_code = {}) const`.

- [ ] **Step 1: Write the failing Agent guidance test**

Add a state-machine path that reaches `agent_install_guide`, then assert exact
runtime guidance:

```cpp
OnboardingController guide_controller(after_wifi_reboot);
assert(
    after_wifi_reboot.on_ble_state(true, true) ==
    OnboardingResult::ok
);
const OnboardingContent guide = guide_controller.content(
    "192.168.1.195", "12345678"
);
assert(content_contains(guide, "IP:192.168.1.195"));
assert(content_contains(guide, "PIN:12345678"));
assert(content_contains(guide, "RUN ./install.sh"));
assert(content_contains(guide, "WAITING HEARTBEAT..."));
assert(guide.count == 5);
```

Use a dedicated backend/state-machine instance if the existing
`after_wifi_reboot` instance has already advanced in the test.

- [ ] **Step 2: Run the host test and verify RED**

Run:

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_onboarding -j
ctest --test-dir build/product-host -R '^onboarding$' --output-on-failure
```

Expected: compilation fails because `content()` does not accept IP/PIN.

- [ ] **Step 3: Add the content inputs and five Agent rows**

Change the declaration to:

```cpp
[[nodiscard]] OnboardingContent content(
    std::string_view ipv4 = {},
    std::string_view pairing_code = {}
) const;
```

Change the definition signature and the Agent case to:

```cpp
case OnboardingStep::agent_install_guide:
  add("SETUP 3/3 AGENT");
  add(std::string("IP:") + std::string(ipv4));
  add(std::string("PIN:") + std::string(pairing_code));
  add("RUN ./install.sh");
  add("WAITING HEARTBEAT...");
  break;
```

In `update_onboarding_ui_locked()`, call:

```cpp
const OnboardingContent content = g_onboarding.content(
    product_wifi_ipv4(),
    product_web_pairing_code()
);
```

- [ ] **Step 4: Run the focused host test and verify GREEN**

Run the same configure/build/ctest commands from Step 2.

Expected: `onboarding` passes and the five-row contract is proven.

- [ ] **Step 5: Commit the onboarding change**

```bash
git add firmware/test/host/test_onboarding.cpp \
  firmware/main/product/onboarding.hpp \
  firmware/main/product/onboarding.cpp \
  firmware/main/product/product_controller.cpp
git commit -m "fix: show setup agent connection details"
```

---

### Task 2: Isolate the Smaller SETUP Font

**Files:**
- Modify: `tools/product/tests/test_companion_packaging.py`
- Modify: `firmware/main/product/display.cpp`

**Interfaces:**
- Consumes: `UiModel::page() -> UiPage`.
- Produces: `kSetupBodyTextSize = 1` selected only for `UiPage::onboarding`.

- [ ] **Step 1: Strengthen the display source-contract test**

Replace the existing broad font assertion with:

```python
def test_cardputer_display_keeps_large_inner_pages_and_small_setup():
    display = (ROOT / "firmware/main/product/display.cpp").read_text()
    assert "kDisplayBodyTextSize = 2" in display
    assert "kSetupBodyTextSize = 1" in display
    assert (
        "model.page() == UiPage::onboarding"
        in display
    )
    assert (
        "? kSetupBodyTextSize"
        in display
    )
    assert (
        ": kDisplayBodyTextSize"
        in display
    )
```

- [ ] **Step 2: Run the focused Python test and verify RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py::test_cardputer_display_keeps_large_inner_pages_and_small_setup
```

Expected: failure because `kSetupBodyTextSize` is absent.

- [ ] **Step 3: Select the body scale from the current page**

Add:

```cpp
constexpr uint8_t kSetupBodyTextSize = 1;
```

Immediately before rendering `page_content()`, select and apply the scale:

```cpp
const uint8_t body_text_size =
    model.page() == UiPage::onboarding
        ? kSetupBodyTextSize
        : kDisplayBodyTextSize;
M5.Display.setTextSize(body_text_size);
```

Do not change the title scale, normal body constant, cursor, page dots, or
non-onboarding visible-row rules.

- [ ] **Step 4: Run the display test and packaging test file**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py
```

Expected: all tests pass.

- [ ] **Step 5: Commit the display change**

```bash
git add firmware/main/product/display.cpp \
  tools/product/tests/test_companion_packaging.py
git commit -m "fix: reduce setup page text size"
```

---

### Task 3: Accept an IP in the Interactive macOS Installer

**Files:**
- Modify: `tools/product/tests/test_mac_installer.py`
- Modify: `scripts/mac_installer.py`

**Interfaces:**
- Consumes: one user-entered string from `input("Cardputer IP: ")`.
- Produces: `device_url_from_ipv4(value: object) -> str`, returning an HTTPS RFC1918 URL or raising `InstallerError`.

- [ ] **Step 1: Write failing normalization and prompt tests**

Add:

```python
def test_device_url_from_ipv4_accepts_only_private_ipv4():
    module = load_installer()
    assert (
        module.device_url_from_ipv4(" 192.168.1.195 ")
        == "https://192.168.1.195"
    )
    for value in (
        "https://192.168.1.195",
        "cardputer.local",
        "8.8.8.8",
        "127.0.0.1",
        "fe80::1",
    ):
        with pytest.raises(module.InstallerError):
            module.device_url_from_ipv4(value)


def test_interactive_config_prompts_for_ip(monkeypatch):
    module = load_installer()
    prompts = []

    def fake_input(prompt):
        prompts.append(prompt)
        return "192.168.1.195"

    monkeypatch.setattr("builtins.input", fake_input)
    monkeypatch.setattr(module.getpass, "getpass", lambda prompt: PIN)

    assert module.interactive_config()["device"] == "https://192.168.1.195"
    assert prompts == ["Cardputer IP: "]
```

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_mac_installer.py \
  -k 'device_url_from_ipv4 or interactive_config_prompts_for_ip'
```

Expected: `device_url_from_ipv4` is missing and the current prompt still asks
for an HTTPS URL.

- [ ] **Step 3: Implement strict RFC1918 IPv4 normalization**

Add:

```python
def device_url_from_ipv4(value: object) -> str:
    if not isinstance(value, str):
        raise InstallerError("device IP must be a string")
    try:
        address = ipaddress.ip_address(value.strip())
    except ValueError as error:
        raise InstallerError("device IP must be an IPv4 address") from error
    if not isinstance(address, ipaddress.IPv4Address):
        raise InstallerError("device IP must be an IPv4 address")
    if not any(address in network for network in PRIVATE_NETWORKS):
        raise InstallerError("device IP must be an RFC1918 LAN address")
    return f"https://{address}"
```

Update:

```python
def interactive_config() -> Dict[str, object]:
    device = device_url_from_ipv4(input("Cardputer IP: "))
    pairing = getpass.getpass("Cardputer device PIN: ")
    return validate_config(
        {
            "device": device,
            "pairing": pairing,
            "pin_revision": 0,
        }
    )
```

Do not narrow `validate_device_url()`; it continues to accept the existing
stored URL and `.local` contracts.

- [ ] **Step 4: Run the full installer test file**

Run:

```bash
PYTHONPATH=. uv run pytest -q tools/product/tests/test_mac_installer.py
```

Expected: all tests pass and no PIN appears in test output.

- [ ] **Step 5: Commit the installer change**

```bash
git add scripts/mac_installer.py tools/product/tests/test_mac_installer.py
git commit -m "fix: prompt for Cardputer IP during install"
```

---

### Task 4: Advance Active Version Surfaces to 1.2.2

**Files:**
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/main/product/product_types.hpp`
- Modify: `companion/AppBundle/Info.plist`
- Modify: `companion/AudioDriver/Info.plist`
- Modify: `companion/Sources/cardputer-companion/CardputerCompanionMain.swift`
- Modify: `companion/Sources/CodexAppServer/JSONRPCProcess.swift`
- Modify: `windows-agent/internal/codex/process.go`
- Modify: `scripts/mac_installer.py`
- Modify: `scripts/build_windows_agent.sh`
- Modify: `scripts/package_windows_agent.sh`
- Modify: `scripts/verify_product_release.sh`
- Modify: `tools/product/verify_public_artifacts.py`
- Modify: `release/product-release.json`
- Modify: `README.md`
- Modify: `README.zh-CN.md`
- Modify: `docs/USER_GUIDE.md`
- Modify: `docs/WINDOWS_AGENT.md`
- Modify: `docs/IMPLEMENTATION_STATUS.md`
- Modify: `docs/PUBLIC_RELEASE.md`
- Modify: `windows-agent/README.txt`
- Modify: active version assertions under `firmware/test/host/` and `tools/product/tests/`

**Interfaces:**
- Consumes: repository release-version consistency contract.
- Produces: every active version and artifact-name surface set to `1.2.2`.

- [ ] **Step 1: Change the release-contract test expectation**

Set:

```python
VERSION = "1.2.2"
```

in `tools/product/tests/test_audio_release.py` and
`tools/product/tests/test_windows_agent_packaging.py`, and update exact
`1.2.1` assertions in firmware and product tests to `1.2.2`.

- [ ] **Step 2: Run the version tests and verify RED**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_public_release_artifacts.py \
  tools/product/tests/test_windows_agent_packaging.py
```

Also run:

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target \
  test_product_types test_ui_model test_product_web -j
ctest --test-dir build/product-host \
  -R '^(product_types|ui_model|product_web)$' \
  --output-on-failure
```

Expected: tests fail on still-live `1.2.1` declarations.

- [ ] **Step 3: Update active product and package declarations**

Replace `1.2.1` with `1.2.2` only in the files listed for this task. Update
release artifact names to:

```text
CardputerCompanion-1.2.2-windows-amd64.zip
CardputerCompanion-1.2.2-windows-arm64.zip
CardputerCompanion-1.2.2-windows-x64-setup.exe
1.2.2-SHA256SUMS
```

Keep dated `docs/superpowers/`, `docs/validation/`, and progress-history
documents unchanged.

- [ ] **Step 4: Prove no stale active version remains**

Run:

```bash
rg -n '1\.2\.1' \
  README.md README.zh-CN.md \
  firmware companion windows-agent scripts release \
  tools/product docs/USER_GUIDE.md docs/WINDOWS_AGENT.md \
  docs/IMPLEMENTATION_STATUS.md docs/PUBLIC_RELEASE.md
```

Expected: no matches.

- [ ] **Step 5: Run version and focused cross-platform tests**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_audio_release.py \
  tools/product/tests/test_audio_driver_bundle.py \
  tools/product/tests/test_mac_installer.py \
  tools/product/tests/test_public_release_artifacts.py \
  tools/product/tests/test_windows_agent_packaging.py
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
(
  cd windows-agent
  go test ./...
)
```

Expected: all selected Python, C++, and Go tests pass.

- [ ] **Step 6: Commit the version change**

Stage only the files listed in this task, then:

```bash
git commit -m "chore: advance product release to 1.2.2"
```

---

### Task 5: Build, Package, and Verify Firmware

**Files:**
- Generate: `firmware/build/cardputer_codex_companion.bin`
- Generate: `dist/cardputer_codex_companion.bin`
- Generate: `dist/cardputer_codex_companion-full.bin`
- Modify: `docs/2026-07-28-setup-agent-guidance_PROGRESS.md`

**Interfaces:**
- Consumes: firmware source and partition layout at version `1.2.2`.
- Produces: flashable app-only and full firmware images with verified layout and hashes.

- [ ] **Step 1: Run complete focused source gates**

Run:

```bash
PYTHONPATH=. uv run pytest -q \
  tools/product/tests/test_companion_packaging.py \
  tools/product/tests/test_mac_installer.py \
  tools/product/tests/test_audio_release.py
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
git diff --check
```

Expected: all commands pass.

- [ ] **Step 2: Build ESP32-S3 firmware**

Run:

```bash
rm -f firmware/sdkconfig firmware/sdkconfig.old
(
  cd firmware
  ../scripts/phase0/idf.sh set-target esp32s3
  ../scripts/phase0/idf.sh build
)
```

Expected: ESP-IDF reports a successful build of
`firmware/build/cardputer_codex_companion.bin`.

- [ ] **Step 3: Verify memory and partition layout**

Run:

```bash
python3 tools/product/verify_partition_layout.py
idf_python="$(
  find .tools/espressif/python_env \
    -path '*/bin/python' -print | sort | tail -n 1
)"
"${idf_python}" -m esp_idf_size --format json \
  firmware/build/cardputer_codex_companion.map \
  > build/product-firmware-size.json
python3 tools/product/verify_firmware_memory.py \
  build/product-firmware-size.json
```

Expected: partition and memory gates pass.

- [ ] **Step 4: Package and inspect both firmware images**

Run:

```bash
scripts/package_product_firmware.sh
python3 tools/product/verify_public_firmware.py \
  --image dist/cardputer_codex_companion-full.bin \
  --layout firmware/partitions_product.csv
shasum -a 256 \
  dist/cardputer_codex_companion.bin \
  dist/cardputer_codex_companion-full.bin
```

Expected: app-only and full images exist, the full image matches the public
partition contract, and both hashes are recorded in command output.

- [ ] **Step 5: Update progress and commit the closeout**

Append a dated milestone containing exact test/build outcomes and artifact
paths to `docs/2026-07-28-setup-agent-guidance_PROGRESS.md`, then:

```bash
git add docs/2026-07-28-setup-agent-guidance_PROGRESS.md
git commit -m "docs: record setup firmware verification"
```

- [ ] **Step 6: Push the completed branch**

Run:

```bash
GIT_SSH_COMMAND='ssh -i /Users/nicholasliao/.ssh/id_co_openclaw -o IdentitiesOnly=yes -o BatchMode=yes' \
  git push origin main
```

Expected: `origin/main` advances through all task commits.

