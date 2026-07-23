# Cardputer Phase 0 Firmware Concurrency Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在同一台目标 Cardputer、同一份 ESP-IDF `v5.5.4` 固件和同一连续取证时间窗内，证明 BLE HID、自定义加密 GATT、Wi-Fi、真实 HTTPS 管理端与固定 SPKI/TLS exporter 的 WSS 能共存，并关闭 Phase 0 Gate 1、Gate 5 及 Gate 6 的 Web 前置限流门槛。

**Architecture:** 探针固件把键盘扫描与 HID 入队放在不依赖网络的高优先级路径，把 BLE、HTTPS、WSS、显示和指标采集放在有界低优先级路径。HID 与自定义 GATT 注册到同一个 NimBLE GATT 数据库；HTTPS 在创建 `esp_tls_t` 以前通过固定容量 admission table，允许“4 个已建立连接 + 1 个正在握手”，管理 API 再经过真实配对、会话、Host、Origin、CSRF、速率和尺寸状态机。独立 Python HIL runner 负责刷写后身份核对、Mac 侧 HID/GATT 绑定证明、攻击负载、同窗指标采集、证据哈希和 schema 校验，不能拼接不同固件或不同启动周期的数据。

**Tech Stack:** ESP-IDF `v5.5.4`（annotated tag target commit `735507283d5b2f9fb363a1901172dbd9e847945d`）、ESP32-S3、C++20、FreeRTOS、NimBLE、ESP HTTP Server、ESP-TLS、mbedTLS、`esp_websocket_client` `1.7.0`、M5Unified `0.2.17`、Python `3.11.11`、uv、pytest、pyserial、jsonschema。

## Global Constraints

- Source of truth: [`2026-07-24-cardputer-codex-companion-design.md`](../specs/2026-07-24-cardputer-codex-companion-design.md)。
- Security protocol source of truth: the foundation plan owns `protocol/phase0/pairing-v1.md`, `protocol/phase0/gatt-auth-v1.md`, `protocol/phase0/wss-auth-v1.md` and `protocol/phase0/fixtures/{pairing-v1,gatt-auth-v1,wss-auth-v1}.json`. Firmware may only consume generated constants and golden vectors from those files; it must not redefine field order, field width, byte order, HKDF/exporter labels, signature encoding, `connection_id` rules, counter rules or replay-window rules.
- 本子计划只覆盖固件并发探针。Gate 2、Gate 3、Gate 4，以及 Gate 6 的 SAS、完整 Companion 身份、Secure Boot、Flash/NVS Encryption 与签名 USB 恢复由相邻 Phase 0 子计划关闭。
- 本子计划只产出无 verdict 的原始 measurements、artifact hashes、capture blockers 和本地一致性错误。不得在 child report 中写 `status`、`reported_status`、`overall_status` 或任何 `P0-G*=PASS/FAIL/BLOCKED`；foundation adapter/finalizer 独占 gate 状态计算。
- 自定义 GATT 的接收端是 macOS Companion。固件按 foundation canonical fixture 生成连接计数器和认证标签，但不得新增固件侧 replay window；重复、回退、过大跳跃和旧 `connection_id` 的拒绝规则及证据必须来自 foundation 契约和 `producer="macos_companion"` 的报告。
- 固件固定使用 ESP-IDF `v5.5.4` target commit `735507283d5b2f9fb363a1901172dbd9e847945d`；tag、commit 或受管组件 lock 不一致时不允许构建 HIL 镜像。
- 目标约束固定为 Cardputer 的 8MiB Flash、512KiB SRAM、无 PSRAM。硬件型号、产品修订、PCB 修订、ESP32-S3 revision、Flash JEDEC/capacity 和 PSRAM 探测结果缺一项时，真机结果为 `BLOCKED`。
- BLE 只使用 NimBLE，并遵循 ESP-IDF `esp_hid` 官方初始化路径。`esp_hidd_dev_init()` 拥有 HID/Device Information GATT 数据库及 `ble_hs_cfg.gatts_register_cb`；固件不得覆盖该 callback，也不得自建替换 HID Service。自定义服务只能在 `esp_hidd_dev_init()` 成功后、`esp_nimble_enable()` 前各调用一次 `ble_gatts_count_cfg()` 与 `ble_gatts_add_svcs()`。
- v1 只保留 1 个 HID host bond。身份契约固定为 HID serial 等于原始 16-byte `device_id` 的无 padding base32，受保护 GATT Identity Characteristic 返回相同的原始 16 bytes；不得使用 HID Feature Report `0x7e`、设备名或同时在线作为同机证明。
- HTTPS 上限的精确定义是最多 4 个 `established` session，外加最多 1 个 `pending_handshake`。HTTP server socket 上限因此是 5；第 2 个并发握手必须在 `esp_tls_init()` 以前拒绝。
- Web 状态机必须真实执行 5 分钟物理配对窗口、8 位一次性代码、5 次失败关闭窗口、10 分钟退避、物理确认、最多 5 个管理客户端、30 分钟 idle expiry、`Secure; HttpOnly; SameSite=Strict` Cookie、写请求 CSRF、Host/Origin 精确校验和 CORS 禁用。
- Web 硬限制固定为：Header `<=8192` bytes、普通 Body `<=16384` bytes、配置导入 `<=131072` bytes 且流式解析、JSON depth `<=8`、WebSocket frame `<=16384` bytes、认证请求 10/s burst 20、未认证请求每源 1/s burst 4、`/healthz` 全局 10/s、TLS handshake 每源 3/min、全局 6/min、来源表最多 16 项。
- 键盘路径不调用 Wi-Fi、TLS、HTTP、显示或 NVS API；网络队列满只能计数并丢弃低优先级网络事件，不能阻塞 scanner 或 HID queue。
- Gate 5 阈值是：steady worst-case internal free heap `>=65536` bytes、largest internal free block `>=32768` bytes；单个 TLS handshake 加 event burst 时 internal free heap `>=40960` bytes、allocation failure 为 0；每个任务剩余 stack `>=max(configured_bytes*20%, 1024)`。
- Gate 5 transient event burst 固定为同一 5 秒内 100 个合法 16KiB WSS frame、一个流式 128KiB import、一个 20-session page 和一个总计 64KiB/4-fragment approval detail；不得用较小或顺序执行的负载替代。
- HID latency 从去抖完成的稳定状态时间戳计算到 HID report 成功进入真实 BLE 发送队列；至少 10,000 个样本，`generated == queued`、`queue_failures == 0`、nearest-rank p95 `<=20000µs`，且最后必须观察到完整 `release all`。
- 真机证据必须来自一个 `run_id`、一个 `boot_id`、一个 runtime `app_elf_sha256`、一个 running-partition `firmware_image_sha256`、一个 `device_id_sha256` 和一段连续 30 分钟 runner 时间窗。任何重启、重刷、digest 改变、时间窗不重叠或原始日志缺失都使整份报告无效。
- 探针是 `PHASE 0 / NOT FOR RELEASE` 开发固件，不烧写 Secure Boot、Flash Encryption 或其他不可逆 eFuse。
- 首次刷写目标设备前必须完成 8MiB 全 Flash 备份和 SHA-256 记录；串口不唯一、容量不符或设备 ID 与操作者确认不一致时停止。
- 缺少 Cardputer、Mac Companion probe、17 个可路由测试源地址、物理配对确认或完整 30 分钟时间窗时，结果必须是 `BLOCKED`，不得用 mock、宿主机测试或较短 smoke run 替代。

---

## Foundation Measurement Mapping

| Foundation consumer | Raw measurements supplied | Producing task |
|---|---|---|
| `P0-G1-CONCURRENCY` | HID、加密 GATT、Wi-Fi、HTTPS、固定 SPKI/exporter 的 WSS 同时在线；Mac 证明 HID serial 与加密 GATT `device_id` 相同 | Tasks 2, 3, 6, 8 |
| `P0-G5-RESOURCE` | 4 established + 1 pending、事件突发、攻击负载期间的 heap、largest block、allocation、stack 与 10k HID p95 | Tasks 4, 5, 7, 8 |
| `P0-G6-WEB-PRECONDITION` | pre-TLS handshake admission、来源表、速率、真实 Web 配对/auth/Host/Origin/CSRF/尺寸限制均 fail closed | Tasks 4, 5, 8 |

## Target File Map

```text
toolchain.lock.json
firmware/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   ├── app_main.cpp
│   └── probe/
│       ├── probe_types.hpp
│       ├── probe_controller.hpp
│       ├── probe_controller.cpp
│       ├── hardware_probe.hpp
│       ├── hardware_probe.cpp
│       ├── keyboard_probe.hpp
│       ├── keyboard_probe.cpp
│       ├── hid_engine.hpp
│       ├── hid_engine.cpp
│       ├── protocol_codec.hpp
│       ├── protocol_codec.cpp
│       ├── ble_services.hpp
│       ├── ble_services.cpp
│       ├── pre_tls_limiter.hpp
│       ├── pre_tls_limiter.cpp
│       ├── bounded_https_server.hpp
│       ├── bounded_https_server.cpp
│       ├── web_guard.hpp
│       ├── web_guard.cpp
│       ├── web_handlers.hpp
│       ├── web_handlers.cpp
│       ├── pinned_wss_transport.hpp
│       ├── pinned_wss_transport.cpp
│       ├── resource_metrics.hpp
│       └── resource_metrics.cpp
└── test/host/
    ├── CMakeLists.txt
    ├── test_probe_controller.cpp
    ├── test_hid_engine.cpp
    ├── test_ble_manifest.cpp
    ├── test_protocol_vectors.cpp
    ├── test_gatt_sender.cpp
    ├── test_pre_tls_limiter.cpp
    ├── test_web_guard.cpp
    ├── test_wss_contract.cpp
    └── test_resource_metrics.cpp
protocol/phase0/
├── pairing-v1.md                         # foundation-owned, read-only here
├── gatt-auth-v1.md                       # foundation-owned, read-only here
├── wss-auth-v1.md                        # foundation-owned, read-only here
├── fixtures/
│   ├── pairing-v1.json                   # foundation-owned canonical vector
│   ├── gatt-auth-v1.json                 # foundation-owned canonical vector
│   └── wss-auth-v1.json                  # foundation-owned canonical vector
├── hardware-manifest.schema.json
├── firmware-concurrency-report.schema.json
└── companion-probe-event.schema.json
scripts/phase0/
├── idf.sh
├── capture_hardware_manifest.py
└── run_concurrency_hil.py
tools/phase0/
├── generate_firmware_protocol_vectors.py
├── validate_hardware_manifest.py
├── validate_concurrency_report.py
└── tests/
    ├── test_firmware_toolchain.py
    ├── test_hardware_manifest.py
    ├── test_concurrency_report.py
    └── test_run_concurrency_hil.py
docs/validation/phase0/
└── firmware-concurrency-hil.md
```

Raw serial, network, Companion and report artifacts live under ignored `build/phase0/firmware-concurrency/`; only the redacted Markdown result and schemas are committed.

## Task 1: Pin ESP-IDF and Capture the Exact Hardware Manifest

**Files:**

- Modify: `toolchain.lock.json`
- Create: `firmware/CMakeLists.txt`
- Create: `firmware/sdkconfig.defaults`
- Create: `firmware/main/CMakeLists.txt`
- Create: `firmware/main/idf_component.yml`
- Create: `firmware/main/probe/hardware_probe.hpp`
- Create: `firmware/main/probe/hardware_probe.cpp`
- Modify: `scripts/phase0/idf.sh`
- Create: `scripts/phase0/capture_hardware_manifest.py`
- Create: `tools/phase0/validate_hardware_manifest.py`
- Create: `tools/phase0/tests/test_firmware_toolchain.py`
- Create: `tools/phase0/tests/test_hardware_manifest.py`
- Create: `protocol/phase0/hardware-manifest.schema.json`

**Interfaces:**

- Consumes: foundation-created repo-local `.tools/esp-idf`, `toolchain.lock.json` and `scripts/phase0/idf.sh`; unique Cardputer serial port; operator-observed M5Stack model/product/PCB revision.
- Produces: `HardwareRuntime probe_hardware()` and a validated `hardware-manifest.json` containing `manifest_version`, `model`, `product_revision`, `pcb_revision`, `chip_model`, `chip_revision`, `flash_jedec_id`, `flash_bytes`, `psram_bytes`, `usb_serial_sha256`, `keyboard_matrix_source` and `captured_at`.

- [ ] **Step 1: Write failing toolchain and hardware validation tests.**

`tools/phase0/tests/test_firmware_toolchain.py`:

```python
import json
from pathlib import Path


def test_esp_idf_is_pinned_to_the_reviewed_commit() -> None:
    lock = json.loads(Path("toolchain.lock.json").read_text())
    assert lock["esp_idf"] == {
        "tag": "v5.5.4",
        "commit": "735507283d5b2f9fb363a1901172dbd9e847945d",
    }
    assert lock["components"]["espressif/esp_websocket_client"] == "1.7.0"
    assert lock["components"]["m5stack/m5unified"] == "0.2.17"
```

`tools/phase0/tests/test_hardware_manifest.py`:

```python
from tools.phase0.validate_hardware_manifest import validate_manifest


def valid_manifest() -> dict:
    return {
        "manifest_version": 1,
        "model": "M5Stack Cardputer",
        "product_revision": "1.2",
        "pcb_revision": "K132",
        "chip_model": "ESP32-S3",
        "chip_revision": 1,
        "flash_jedec_id": "c84017",
        "flash_bytes": 8388608,
        "psram_bytes": 0,
        "usb_serial_sha256": "1a" * 32,
        "keyboard_matrix_source": {
            "repository": "m5stack/M5Cardputer",
            "commit": "2d4fa6646e4e5b47e0af96214b003aa7b15b8d81",
            "outputs": [8, 9, 11],
            "inputs": [13, 15, 3, 4, 5, 6, 7],
            "physically_verified": True,
        },
        "captured_at": "2026-07-24T08:00:00Z",
    }


def test_exact_target_is_accepted() -> None:
    assert validate_manifest(valid_manifest()) == []


def test_psram_or_wrong_flash_is_rejected() -> None:
    manifest = valid_manifest()
    manifest["psram_bytes"] = 2097152
    manifest["flash_bytes"] = 4194304
    assert validate_manifest(manifest) == [
        "flash_bytes must equal 8388608",
        "psram_bytes must equal 0",
    ]
```

- [ ] **Step 2: Run the tests and verify that the new contracts are missing.**

Run:

```bash
uv run pytest tools/phase0/tests/test_firmware_toolchain.py tools/phase0/tests/test_hardware_manifest.py -q
```

Expected: collection fails because `validate_hardware_manifest.py` and the lock entries do not exist.

- [ ] **Step 3: Add the immutable toolchain lock and repo-local IDF wrapper.**

Add this exact lock fragment to `toolchain.lock.json`:

```json
{
  "esp_idf": {
    "tag": "v5.5.4",
    "commit": "735507283d5b2f9fb363a1901172dbd9e847945d"
  },
  "components": {
    "espressif/esp_websocket_client": "1.7.0",
    "m5stack/m5unified": "0.2.17"
  }
}
```

`scripts/phase0/idf.sh`:

```sh
#!/bin/sh
set -eu
REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
IDF_PATH="$REPO_ROOT/.tools/esp-idf"
IDF_TOOLS_PATH="$REPO_ROOT/.tools/espressif"
EXPECTED_COMMIT=735507283d5b2f9fb363a1901172dbd9e847945d
ACTUAL_COMMIT=$(git -C "$IDF_PATH" rev-parse HEAD)
if [ "$ACTUAL_COMMIT" != "$EXPECTED_COMMIT" ]; then
  echo "ESP-IDF commit mismatch" >&2
  exit 2
fi
export IDF_PATH IDF_TOOLS_PATH
. "$IDF_PATH/export.sh" >/dev/null
exec "$IDF_PATH/tools/idf.py" "$@"
```

Use `EXTRA_COMPONENT_DIRS` only through `idf_component.yml`; commit the generated `dependencies.lock` and fail CI if its versions differ from the lock file.

- [ ] **Step 4: Add the 8MiB/no-PSRAM/NimBLE build baseline.**

`firmware/sdkconfig.defaults` must contain:

```ini
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_SPIRAM=n
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_SECURITY_ENABLE=y
CONFIG_BT_NIMBLE_SM_LEGACY=n
CONFIG_BT_NIMBLE_SM_SC=y
CONFIG_BT_NIMBLE_SM_SC_ONLY=1
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_MAX_BONDS=1
CONFIG_BT_NIMBLE_HID_SERVICE=y
CONFIG_HTTPD_MAX_REQ_HDR_LEN=8192
CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE=y
CONFIG_MBEDTLS_SSL_KEYING_MATERIAL_EXPORT=y
CONFIG_FREERTOS_USE_TRACE_FACILITY=y
```

`firmware/main/idf_component.yml` must pin:

```yaml
dependencies:
  idf:
    version: "==5.5.4"
  espressif/esp_websocket_client:
    version: "==1.7.0"
  m5stack/m5unified:
    version: "==0.2.17"
```

`firmware/CMakeLists.txt` sets `PROJECT_VER` to `phase0-probe` and `CMAKE_CXX_STANDARD 20`; `app_main.cpp` must print `PHASE 0 / NOT FOR RELEASE` before starting services.

- [ ] **Step 5: Implement runtime hardware capture and strict manifest validation.**

`firmware/main/probe/hardware_probe.hpp`:

```cpp
#pragma once

#include <cstdint>

struct HardwareRuntime {
  const char* chip_model;
  uint32_t chip_revision;
  uint32_t flash_jedec_id;
  uint32_t flash_bytes;
  uint32_t psram_bytes;
};

HardwareRuntime probe_hardware();
```

`probe_hardware()` uses `esp_chip_info()`, `esp_flash_read_id()`, `esp_flash_get_size()` and `esp_psram_get_size()`. A missing PSRAM device is recorded as zero, not as an ignored error.

`tools/phase0/validate_hardware_manifest.py`:

```python
from jsonschema import Draft202012Validator
import json
from pathlib import Path

SCHEMA = json.loads(
    Path("protocol/phase0/hardware-manifest.schema.json").read_text()
)


def validate_manifest(manifest: dict) -> list[str]:
    errors = [
        error.message
        for error in sorted(
            Draft202012Validator(SCHEMA).iter_errors(manifest),
            key=lambda item: list(item.path),
        )
    ]
    if manifest.get("flash_bytes") != 8388608:
        errors.append("flash_bytes must equal 8388608")
    if manifest.get("psram_bytes") != 0:
        errors.append("psram_bytes must equal 0")
    return errors
```

The JSON schema sets `additionalProperties: false`, requires every produced field, constrains `usb_serial_sha256` to `^[0-9a-f]{64}$`, and requires `keyboard_matrix_source.physically_verified` to be `true`. `capture_hardware_manifest.py` reads one firmware `hardware_runtime` JSON event, asks for product and PCB revisions, hashes the USB serial before writing, and never stores the raw serial.

- [ ] **Step 6: Verify the host contract and target configuration.**

Run:

```bash
uv run pytest tools/phase0/tests/test_firmware_toolchain.py tools/phase0/tests/test_hardware_manifest.py -q
scripts/phase0/idf.sh -C firmware set-target esp32s3
scripts/phase0/idf.sh -C firmware reconfigure
rg 'CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y|CONFIG_SPIRAM=n|CONFIG_BT_NIMBLE_ENABLED=y' firmware/sdkconfig
```

Expected: `3 passed`; all three required sdkconfig lines are printed; dependency resolution records exactly `esp_websocket_client 1.7.0` and M5Unified `0.2.17`.

- [ ] **Step 7: Commit the task.**

```bash
git add toolchain.lock.json firmware/CMakeLists.txt firmware/sdkconfig.defaults firmware/main/CMakeLists.txt firmware/main/idf_component.yml firmware/dependencies.lock firmware/main/probe/hardware_probe.hpp firmware/main/probe/hardware_probe.cpp scripts/phase0/idf.sh scripts/phase0/capture_hardware_manifest.py tools/phase0/validate_hardware_manifest.py tools/phase0/tests/test_firmware_toolchain.py tools/phase0/tests/test_hardware_manifest.py protocol/phase0/hardware-manifest.schema.json
git commit -m "chore: pin firmware concurrency probe target"
```

## Task 2: Define One-Run Service State and Evidence Identity

**Files:**

- Create: `firmware/main/probe/probe_types.hpp`
- Create: `firmware/main/probe/probe_controller.hpp`
- Create: `firmware/main/probe/probe_controller.cpp`
- Create: `firmware/test/host/CMakeLists.txt`
- Create: `firmware/test/host/test_probe_controller.cpp`
- Create: `protocol/phase0/firmware-concurrency-report.schema.json`
- Create: `protocol/phase0/companion-probe-event.schema.json`
- Create: `tools/phase0/validate_concurrency_report.py`
- Create: `tools/phase0/tests/test_concurrency_report.py`

**Interfaces:**

- Consumes: runtime `boot_id`, `app_elf_sha256`, running-partition `firmware_image_sha256`, persistent `device_id`, runner-generated `run_id`, and individual service transitions.
- Produces: `ProbeIdentity`, `ServiceSnapshot`, `ProbeController::snapshot()` and a report schema that forbids cross-run evidence.

- [ ] **Step 1: Write the failing service-state test.**

`firmware/test/host/test_probe_controller.cpp`:

```cpp
#include <cassert>
#include "probe/probe_controller.hpp"

int main() {
  ProbeController controller;
  controller.set(Service::ble_hid, true);
  controller.set(Service::encrypted_gatt, true);
  controller.set(Service::wifi, true);
  controller.set(Service::https, true);
  assert(!controller.snapshot().all_live());

  controller.set(Service::wss_authenticated, true);
  assert(controller.snapshot().all_live());

  controller.set(Service::wifi, false);
  const auto degraded = controller.snapshot();
  assert(!degraded.all_live());
  assert(degraded.ble_hid);
  assert(degraded.encrypted_gatt);
  return 0;
}
```

- [ ] **Step 2: Write the failing cross-run evidence test.**

`tools/phase0/tests/test_concurrency_report.py`:

```python
from tools.phase0.validate_concurrency_report import (
    forbidden_verdict_fields,
    same_run_errors,
)


def event(producer: str, run_id: str, boot_id: str, digest: str) -> dict:
    return {
        "producer": producer,
        "run_id": run_id,
        "boot_id": boot_id,
        "app_elf_sha256": digest,
        "firmware_image_sha256": "33" * 32,
        "device_id_sha256": "11" * 32,
        "observed_at_ns": 100,
    }


def test_different_boot_cannot_be_merged() -> None:
    events = [
        event("firmware", "run-a", "boot-a", "22" * 32),
        event("macos_companion", "run-a", "boot-b", "22" * 32),
    ]
    assert same_run_errors(events) == ["boot_id differs across evidence"]


def test_firmware_cannot_claim_companion_replay_rejection() -> None:
    candidate = event("firmware", "run-a", "boot-a", "22" * 32)
    candidate["kind"] = "gatt_replay_result"
    assert same_run_errors([candidate]) == [
        "gatt_replay_result must be produced by macos_companion"
    ]


def test_child_report_cannot_claim_gate_status() -> None:
    report = {
        "capture_complete": True,
        "status": "PASS",
        "nested": {"reported_status": "PASS"},
    }
    assert forbidden_verdict_fields(report) == [
        "nested.reported_status",
        "status",
    ]
```

- [ ] **Step 3: Run both tests and observe missing contracts.**

Run:

```bash
cmake -S firmware/test/host -B build/phase0/firmware-host
cmake --build build/phase0/firmware-host
uv run pytest tools/phase0/tests/test_concurrency_report.py -q
```

Expected: CMake fails because `probe_controller.hpp` is missing and pytest fails to import `validate_concurrency_report`.

- [ ] **Step 4: Implement the immutable identity and service snapshot.**

`firmware/main/probe/probe_types.hpp`:

```cpp
#pragma once

#include <array>
#include <cstdint>

struct ProbeIdentity {
  std::array<uint8_t, 16> run_id;
  std::array<uint8_t, 16> boot_id;
  std::array<uint8_t, 32> app_elf_sha256;
  std::array<uint8_t, 32> firmware_image_sha256;
  std::array<uint8_t, 16> device_id;
};

struct ServiceSnapshot {
  bool ble_hid = false;
  bool encrypted_gatt = false;
  bool wifi = false;
  bool https = false;
  bool wss_authenticated = false;

  [[nodiscard]] bool all_live() const {
    return ble_hid && encrypted_gatt && wifi && https &&
           wss_authenticated;
  }
};
```

`firmware/main/probe/probe_controller.hpp`:

```cpp
#pragma once

#include "probe_types.hpp"

enum class Service {
  ble_hid,
  encrypted_gatt,
  wifi,
  https,
  wss_authenticated,
};

class ProbeController {
 public:
  void set(Service service, bool ready);
  [[nodiscard]] ServiceSnapshot snapshot() const;

 private:
  ServiceSnapshot snapshot_;
};
```

`set()` changes only the addressed boolean. Wi-Fi or WSS failure must not call the BLE shutdown path. The runtime creates `boot_id` from `esp_fill_random()` on each boot, reads the application ELF SHA from `esp_app_get_description()->app_elf_sha256`, validates the runner-supplied flashed-image byte length against the public ESP image parser and streams exactly those logical running-partition bytes through SHA-256 once, reads the persistent `device_id` from encrypted-ready NVS storage, and accepts `run_id` once from the serial HIL control channel. The WSS signed identity and protected GATT identity/control response carry the same boot/app/image digests so the Mac can bind its Gate 6 run to the exact probe firmware.

- [ ] **Step 5: Implement fail-closed report identity validation.**

`tools/phase0/validate_concurrency_report.py`:

```python
def same_run_errors(events: list[dict]) -> list[str]:
    errors: list[str] = []
    if any(
        item.get("kind") == "gatt_replay_result"
        and item.get("producer") != "macos_companion"
        for item in events
    ):
        errors.append(
            "gatt_replay_result must be produced by macos_companion"
        )
    for key in (
        "run_id",
        "boot_id",
        "app_elf_sha256",
        "firmware_image_sha256",
        "device_id_sha256",
    ):
        values = {item.get(key) for item in events}
        if len(values) != 1:
            errors.append(f"{key} differs across evidence")
    return errors


def forbidden_verdict_fields(value: object, path: str = "") -> list[str]:
    forbidden = {"status", "reported_status", "overall_status"}
    found: list[str] = []
    if isinstance(value, dict):
        for key, child in value.items():
            child_path = f"{path}.{key}" if path else key
            if key in forbidden:
                found.append(child_path)
            found.extend(forbidden_verdict_fields(child, child_path))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            found.extend(
                forbidden_verdict_fields(child, f"{path}[{index}]")
            )
    return sorted(found)
```

`firmware-concurrency-report.schema.json` uses JSON Schema 2020-12, `additionalProperties: false`, explicitly rejects `status`, `reported_status` and `overall_status`, and requires top-level `schema_version`, `capture_complete`, `run`, `hardware`, `services`, `ble_identity`, `https`, `web_security`, `wss`, `resources`, `hid`, `artifacts`, `blockers` and `consistency_errors`. `run` requires the five evidence identity fields (`run_id`, `boot_id`, `app_elf_sha256`, `firmware_image_sha256`, `device_id_sha256`) plus `git_commit`, `git_tree_clean`, `toolchain_manifest_sha256`, `started_at`, `ended_at`, `duration_seconds` and `continuous_capture=true`; complete gate evidence requires `git_tree_clean=true`. Foundation `tools/phase0/adapters.py` consumes these measurements and the finalizer independently computes every gate verdict.

`companion-probe-event.schema.json` allows only `producer="macos_companion"` and kinds `ready`, `ble_identity`, `hid_observation`, `gatt_security`, `gatt_replay_result`, `wss_auth`, `heartbeat`, `interface_changed`, and `stopped`. Every event carries the same `run_id`, `boot_id`, `app_elf_sha256`, `firmware_image_sha256` and device-ID digest plus `producer_monotonic_ns`; the Python runner adds its own `observed_at_ns` immediately after reading each complete stdout JSONL line and only then performs schema validation. No event may carry raw device ID, SAS, TLS exporter, GATT secret or text payload.

- [ ] **Step 6: Verify host and schema tests.**

Run:

```bash
cmake --build build/phase0/firmware-host
ctest --test-dir build/phase0/firmware-host -R probe_controller --output-on-failure
uv run pytest tools/phase0/tests/test_concurrency_report.py -q
```

Expected: the C++ test and all three Python tests pass.

- [ ] **Step 7: Commit the task.**

```bash
git add firmware/main/probe/probe_types.hpp firmware/main/probe/probe_controller.hpp firmware/main/probe/probe_controller.cpp firmware/test/host/CMakeLists.txt firmware/test/host/test_probe_controller.cpp protocol/phase0/firmware-concurrency-report.schema.json protocol/phase0/companion-probe-event.schema.json tools/phase0/validate_concurrency_report.py tools/phase0/tests/test_concurrency_report.py
git commit -m "test: bind firmware evidence to one run"
```

## Task 3: Initialize Official ESP HID, Custom GATT, and Canonical Protocol Vectors

**Files:**

- Create: `firmware/main/probe/keyboard_probe.hpp`
- Create: `firmware/main/probe/keyboard_probe.cpp`
- Create: `firmware/main/probe/hid_engine.hpp`
- Create: `firmware/main/probe/hid_engine.cpp`
- Create: `firmware/main/probe/protocol_codec.hpp`
- Create: `firmware/main/probe/protocol_codec.cpp`
- Create: `firmware/main/probe/ble_services.hpp`
- Create: `firmware/main/probe/ble_services.cpp`
- Create: `tools/phase0/generate_firmware_protocol_vectors.py`
- Modify: `firmware/main/app_main.cpp`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/test/host/CMakeLists.txt`
- Create: `firmware/test/host/test_hid_engine.cpp`
- Create: `firmware/test/host/test_ble_manifest.cpp`
- Create: `firmware/test/host/test_protocol_vectors.cpp`
- Create: `firmware/test/host/test_gatt_sender.cpp`

**Interfaces:**

- Consumes: `StableKeyEvent{physical_key, pressed, stable_at_us}`, persistent raw 16-byte `device_id`, current BLE bond, and foundation-owned pairing/GATT/WSS docs plus JSON fixtures.
- Produces: `HidEngine::make_report()`, mandatory `release_all()`, HID serial `base32(raw_device_id)` with no padding, encrypted GATT identity returning the raw device ID, official `esp_hidd_dev_t`, one appended custom GATT service, and firmware protocol bytes proven against foundation golden vectors.

- [ ] **Step 1: Write failing HID correctness and identity tests.**

`firmware/test/host/test_hid_engine.cpp`:

```cpp
#include <array>
#include <cassert>
#include "probe/hid_engine.hpp"

int main() {
  HidEngine hid;
  const std::array<uint8_t, 2> keys{0x06, 0x4c};
  const auto chord = hid.make_report(0x08, keys);
  assert(chord.error == HidError::none);
  assert(chord.report.modifiers == 0x08);
  assert(chord.report.keys[0] == 0x06);
  assert(chord.report.keys[1] == 0x4c);
  assert(hid.release_all() == HidReport{});

  const std::array<uint8_t, 7> overflow{4, 5, 6, 7, 8, 9, 10};
  assert(hid.make_report(0, overflow).error == HidError::too_many_keys);

  const std::array<uint8_t, 16> device_id{
      0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
      0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
  assert(hid_serial_from_device_id(device_id) ==
         "CEIRCEIRCEIRCEIRCEIRCEIRCE");
  assert(!hid.report_map_has_feature_report());
  return 0;
}
```

This test fixes Delete at usage `0x4c`, keeps modifier `0x08` outside the six regular-key slots, rejects more than six keys, fixes the base32 identity encoding and forbids every HID Feature Report, including report ID `0x7e`.

- [ ] **Step 2: Write the failing official-init-order test.**

`firmware/test/host/test_ble_manifest.cpp`:

```cpp
#include <array>
#include <cassert>
#include "probe/ble_services.hpp"

int main() {
  constexpr std::array expected{
      BleInitStep::esp_hid_gap_init,
      BleInitStep::esp_hid_ble_gap_adv_init,
      BleInitStep::esp_hidd_dev_init,
      BleInitStep::count_custom_service,
      BleInitStep::add_custom_service,
      BleInitStep::ble_store_config_init,
      BleInitStep::esp_nimble_enable,
  };
  const BleServiceManifest manifest = ble_service_manifest();
  assert(manifest.stack == BleStack::nimble);
  assert(manifest.hid_owner == HidGattOwner::esp_hid);
  assert(manifest.steps == expected);
  assert(!manifest.overrides_gatts_register_cb);
  assert(manifest.custom_count_cfg_calls == 1);
  assert(manifest.custom_add_svcs_calls == 1);
  assert(manifest.identity_read_requires_encryption);
  assert(manifest.identity_read_requires_authentication);
  assert(manifest.text_write_requires_current_companion);
  assert(manifest.max_bonds == 1);
  return 0;
}
```

- [ ] **Step 3: Write failing firmware consumers of all foundation vectors.**

`tools/phase0/generate_firmware_protocol_vectors.py` must generate ignored `build/phase0/generated/phase0_protocol_vectors.hpp` from exactly:

```text
protocol/phase0/pairing-v1.md
protocol/phase0/gatt-auth-v1.md
protocol/phase0/wss-auth-v1.md
protocol/phase0/fixtures/pairing-v1.json
protocol/phase0/fixtures/gatt-auth-v1.json
protocol/phase0/fixtures/wss-auth-v1.json
```

`firmware/test/host/test_protocol_vectors.cpp`:

```cpp
#include <cassert>
#include "phase0_protocol_vectors.hpp"
#include "probe/protocol_codec.hpp"

int main() {
  assert(protocol_vectors::source_files_verified());
  assert(protocol_vectors::pairing::source_sha256.size() == 32);
  assert(protocol_vectors::gatt::source_sha256.size() == 32);
  assert(protocol_vectors::wss::source_sha256.size() == 32);
  assert(encode_pairing_transcript(protocol_vectors::pairing::input) ==
         protocol_vectors::pairing::canonical_transcript);
  assert(derive_pairing_values(protocol_vectors::pairing::input) ==
         protocol_vectors::pairing::expected_values);
  return 0;
}
```

`firmware/test/host/test_gatt_sender.cpp`:

```cpp
#include <cassert>
#include "phase0_protocol_vectors.hpp"
#include "probe/protocol_codec.hpp"

int main() {
  GattSender sender(protocol_vectors::gatt::auth_key);
  sender.begin_connection(protocol_vectors::gatt::connection_id);
  const auto frame = sender.encode(protocol_vectors::gatt::message);
  assert(frame.authenticated_bytes ==
         protocol_vectors::gatt::authenticated_bytes);
  assert(frame.tag == protocol_vectors::gatt::tag);
  assert(frame.counter == protocol_vectors::gatt::initial_counter);
  assert(sender.next_counter() ==
         protocol_vectors::gatt::counter_after_first_frame);
  sender.end_connection();
  assert(!sender.has_connection());
  return 0;
}
```

The generated namespace and typed fields are derived from the foundation fixture schema. This plan does not assign independent values to protocol fields, labels, signature representation, counter limits, connection ID width or replay-window width.

- [ ] **Step 4: Run RED and prove the generator/codecs are absent.**

Run:

```bash
uv run python tools/phase0/generate_firmware_protocol_vectors.py \
  --protocol-root protocol/phase0 \
  --output build/phase0/generated/phase0_protocol_vectors.hpp
cmake --build build/phase0/firmware-host
ctest --test-dir build/phase0/firmware-host -R 'hid_engine|ble_manifest|protocol_vectors|gatt_sender' --output-on-failure
```

Expected: the generator command fails because the script is missing; after its RED is observed, the C++ build also fails because HID, BLE and protocol codec sources are missing.

- [ ] **Step 5: Generate typed C++ vectors without duplicating protocol constants.**

The generator loads all three foundation docs and fixtures, verifies each file SHA-256, validates every fixture through the foundation generator/validator, and emits:

```cpp
namespace protocol_vectors {
bool source_files_verified();
namespace pairing {
extern const std::array<uint8_t, 32> source_sha256;
extern const PairingInput input;
extern const ByteVector canonical_transcript;
extern const PairingExpected expected_values;
}
namespace gatt {
extern const std::array<uint8_t, 32> source_sha256;
extern const GattAuthKey auth_key;
extern const ConnectionId connection_id;
extern const GattMessage message;
extern const ByteVector authenticated_bytes;
extern const AuthTag tag;
extern const Counter initial_counter;
extern const Counter counter_after_first_frame;
}
namespace wss {
extern const std::array<uint8_t, 32> source_sha256;
extern const WssAuthInput input;
extern const ByteVector canonical_message;
extern const SignatureBytes signature;
extern const PublicKeyBytes device_public_key;
extern const std::array<uint8_t, 32> spki_sha256;
extern const std::string_view exporter_label;
extern const size_t exporter_bytes;
WssAuthInput make_runtime_input(
    std::span<const uint8_t> exporter,
    std::span<const uint8_t> challenge,
    const RuntimeWssIdentity& identity);
}
}
```

All concrete byte arrays, strings, widths and numeric values come from the canonical fixture bundle. The generator exits non-zero if a canonical file is missing, its digest changes without regenerated vectors, a fixture fails foundation validation, or an expected canonical field is absent. `firmware/CMakeLists.txt` and host CMake add a custom command depending on all six source files; the generated header lives under `build/phase0/generated/` and is never hand-edited or committed.

- [ ] **Step 6: Implement standard boot-keyboard reports and unconditional release.**

`firmware/main/probe/hid_engine.hpp`:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

struct HidReport {
  uint8_t modifiers = 0;
  uint8_t reserved = 0;
  std::array<uint8_t, 6> keys{};
  bool operator==(const HidReport&) const = default;
};

enum class HidError { none, too_many_keys, duplicate_key };

struct HidResult {
  HidError error;
  HidReport report;
};

class HidEngine {
 public:
  HidResult make_report(uint8_t modifiers,
                        std::span<const uint8_t> usages) const;
  HidReport release_all() const { return {}; }
  bool report_map_has_feature_report() const;
};

std::string hid_serial_from_device_id(
    std::span<const uint8_t, 16> device_id);
```

`make_report()` rejects more than six usages and duplicates, preserves usage order, and never places modifier bits into `keys`. The official ESP HID report map contains keyboard input/output reports only and no Feature item. `keyboard_probe.cpp` delivers reports with `esp_hidd_dev_input_set()` and sends `release_all()` after every chord and on macro abort, mode change, BLE disconnect, scanner fault and controlled reboot. The 10,000-event synthetic source calls the same `enqueue_stable_key_event()` used after physical debounce.

- [ ] **Step 7: Initialize HID through `esp_hid`, then append only the custom service.**

`firmware/main/probe/ble_services.hpp` exposes:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <span>

enum class BleStack { nimble };
enum class HidGattOwner { esp_hid };
enum class BleInitStep {
  esp_hid_gap_init,
  esp_hid_ble_gap_adv_init,
  esp_hidd_dev_init,
  count_custom_service,
  add_custom_service,
  ble_store_config_init,
  esp_nimble_enable,
};

struct BleServiceManifest {
  BleStack stack;
  HidGattOwner hid_owner;
  std::array<BleInitStep, 7> steps;
  bool overrides_gatts_register_cb;
  uint8_t custom_count_cfg_calls;
  uint8_t custom_add_svcs_calls;
  bool identity_read_requires_encryption;
  bool identity_read_requires_authentication;
  bool text_write_requires_current_companion;
  uint8_t max_bonds;
};

BleServiceManifest ble_service_manifest();
esp_err_t initialize_ble(
    std::span<const uint8_t, 16> device_id,
    esp_hidd_dev_t** hid_device);
```

`firmware/main/CMakeLists.txt` compiles the pinned official helper directly from `$IDF_PATH/examples/bluetooth/esp_hid_device/main/esp_hid_gap.c` and adds that directory to private includes. Before build, CMake verifies the helper is unchanged from the locked checkout:

```cmake
set(HID_EXAMPLE_DIR
    "$ENV{IDF_PATH}/examples/bluetooth/esp_hid_device/main")
execute_process(
  COMMAND git -C "$ENV{IDF_PATH}" diff --quiet HEAD --
          examples/bluetooth/esp_hid_device/main/esp_hid_gap.c
          examples/bluetooth/esp_hid_device/main/esp_hid_gap.h
  RESULT_VARIABLE HID_GAP_DIRTY)
if(NOT HID_GAP_DIRTY EQUAL 0)
  message(FATAL_ERROR "pinned ESP-IDF HID GAP helper is modified")
endif()
target_sources(
  ${COMPONENT_LIB} PRIVATE "${HID_EXAMPLE_DIR}/esp_hid_gap.c")
target_include_directories(
  ${COMPONENT_LIB} PRIVATE "${HID_EXAMPLE_DIR}")
```

`ble_services.cpp` then uses the official `esp_hid_device` sequence:

```cpp
ESP_ERROR_CHECK(esp_hid_gap_init(ESP_BT_MODE_BLE));
ESP_ERROR_CHECK(esp_hid_ble_gap_adv_init(
    ESP_HID_APPEARANCE_KEYBOARD, kDeviceName));

static std::string hid_serial;
hid_serial = hid_serial_from_device_id(device_id);
esp_hid_device_config_t hid_config{
    .vendor_id = kVendorId,
    .product_id = kProductId,
    .version = kDeviceVersion,
    .device_name = kDeviceName,
    .manufacturer_name = kManufacturer,
    .serial_number = hid_serial.c_str(),
    .report_maps = kKeyboardReportMaps,
    .report_maps_len = 1,
};
ESP_ERROR_CHECK(esp_hidd_dev_init(
    &hid_config, ESP_HID_TRANSPORT_BLE,
    ble_hidd_event_callback, hid_device));

int rc = ble_gatts_count_cfg(kCustomGattServices);
if (rc != 0) {
  return ESP_FAIL;
}
rc = ble_gatts_add_svcs(kCustomGattServices);
if (rc != 0) {
  return ESP_FAIL;
}

ble_store_config_init();
ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
ESP_ERROR_CHECK(esp_nimble_enable(ble_hid_device_host_task));
```

`kCustomGattServices` contains only the 128-bit Companion service. It must not contain HID `0x1812`, Device Information `0x180A`, Battery, GAP or GATT services because `esp_hidd_dev_init()` already owns those registrations and its `gatts_register_cb`. Firmware never assigns `ble_hs_cfg.gatts_register_cb`.

The custom identity read uses `BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN` and returns the raw 16-byte `device_id`. Control write uses encrypted/authenticated flags and accepts only the foundation-canonical connection context after LE Secure Connections bonding and current-Companion binding. `GattSender` uses generated canonical bytes, tag rules and counters; disconnect discards its connection state.

No firmware `ReplayWindow` type is permitted. MAC verification and replay-window mutation ordering belong to the Companion receiver and are checked against the foundation fixture there.

- [ ] **Step 8: Bind and prove the same raw identity through HID serial and protected GATT.**

On first boot, generate 16 bytes with `esp_fill_random()`, reject an all-zero value and atomically store it as `device_id` in NVS. Pass base32-without-padding of those bytes to `esp_hid_device_config_t.serial_number`; return the original 16 bytes from the protected GATT Identity Characteristic. Do not add a Feature Report to the report map.

The Mac probe contract is:

```json
{
  "producer": "macos_companion",
  "kind": "ble_identity",
  "hid_serial_base32": "CEIRCEIRCEIRCEIRCEIRCEIRCE",
  "gatt_device_id_hex": "11111111111111111111111111111111",
  "gatt_link_encrypted": true,
  "gatt_link_authenticated": true,
  "bonded": true,
  "same_device": true
}
```

The Companion reads the standard HID serial property, decodes it to 16 bytes and compares it to the protected GATT value. It does not issue HID Feature Report `0x7e`.

- [ ] **Step 9: Run GREEN tests, build the target, and scan for forbidden forks.**

Run:

```bash
uv run python tools/phase0/generate_firmware_protocol_vectors.py \
  --protocol-root protocol/phase0 \
  --output build/phase0/generated/phase0_protocol_vectors.hpp
cmake --build build/phase0/firmware-host
ctest --test-dir build/phase0/firmware-host -R 'hid_engine|ble_manifest|protocol_vectors|gatt_sender' --output-on-failure
scripts/phase0/idf.sh -C firmware build
if rg -n 'gatts_register_cb[[:space:]]*=|esp_ble_gatts_register_callback|esp_bluedroid|CONFIG_BT_BLUEDROID|0x7[eE]' firmware/main firmware/sdkconfig.defaults; then exit 1; fi
if rg -n 'EXPORTER-|pair-root|pairing-root|gatt-auth|pairing-sas' firmware/main/probe; then exit 1; fi
```

Expected: four host tests pass; the generated source digests match all foundation files; the target image links through `esp_hidd_dev_init()` followed by custom count/add and `esp_nimble_enable()`; both forbidden scans print nothing. `protocol_codec.cpp` references generated symbols and contains no literal label or independent field-width constant.

- [ ] **Step 10: Commit the task.**

```bash
git add firmware/main/probe/keyboard_probe.hpp firmware/main/probe/keyboard_probe.cpp firmware/main/probe/hid_engine.hpp firmware/main/probe/hid_engine.cpp firmware/main/probe/protocol_codec.hpp firmware/main/probe/protocol_codec.cpp firmware/main/probe/ble_services.hpp firmware/main/probe/ble_services.cpp firmware/main/app_main.cpp firmware/main/CMakeLists.txt firmware/test/host/CMakeLists.txt firmware/test/host/test_hid_engine.cpp firmware/test/host/test_ble_manifest.cpp firmware/test/host/test_protocol_vectors.cpp firmware/test/host/test_gatt_sender.cpp tools/phase0/generate_firmware_protocol_vectors.py
git commit -m "feat: extend official esp hid with canonical gatt"
```

## Task 4: Admit HTTPS Before TLS Allocation with Four Established Plus One Pending

**Files:**

- Create: `firmware/main/probe/pre_tls_limiter.hpp`
- Create: `firmware/main/probe/pre_tls_limiter.cpp`
- Create: `firmware/main/probe/bounded_https_server.hpp`
- Create: `firmware/main/probe/bounded_https_server.cpp`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/test/host/CMakeLists.txt`
- Create: `firmware/test/host/test_pre_tls_limiter.cpp`

**Interfaces:**

- Consumes: normalized IPv4/IPv6 `SourceKey`, monotonic milliseconds, accepted socket FD and server certificate/key held by the probe.
- Produces: `PreTlsLimiter::begin()`, `complete()`, `close_established()`, exact occupancy/counter metrics, and `start_bounded_https_server()` with `max_open_sockets=5`.

- [ ] **Step 1: Write the failing admission-state test.**

`firmware/test/host/test_pre_tls_limiter.cpp`:

```cpp
#include <cassert>
#include "probe/pre_tls_limiter.hpp"

SourceKey source(uint8_t last) {
  SourceKey value{};
  value.bytes[15] = last;
  return value;
}

int main() {
  PreTlsLimiter limiter;
  uint64_t now = 1000;

  for (uint8_t index = 1; index <= 4; ++index) {
    const auto lease = limiter.begin(source(index), now);
    assert(lease.allowed());
    limiter.note_tls_alloc_started(lease.token);
    assert(limiter.complete(lease.token, true) ==
           Completion::established);
  }
  assert(limiter.snapshot().established == 4);

  const auto pending = limiter.begin(source(5), now);
  assert(pending.allowed());
  assert(limiter.snapshot().established == 4);
  assert(limiter.snapshot().pending_handshakes == 1);

  const auto second = limiter.begin(source(6), now);
  assert(second.reason == RejectReason::handshake_busy);
  assert(limiter.snapshot().tls_alloc_started == 4);

  limiter.note_tls_alloc_started(pending.token);
  assert(limiter.complete(pending.token, true) ==
         Completion::rejected_established_full);
  assert(limiter.snapshot().established == 4);
  assert(limiter.snapshot().pending_handshakes == 0);
  return 0;
}
```

- [ ] **Step 2: Add failing rate-table tests.**

Extend the same executable with these assertions:

```cpp
PreTlsLimiter rates;
assert(rates.begin(source(1), 0).allowed());
rates.cancel_pending();
assert(rates.begin(source(1), 1000).allowed());
rates.cancel_pending();
assert(rates.begin(source(1), 2000).allowed());
rates.cancel_pending();
assert(rates.begin(source(1), 3000).reason ==
       RejectReason::source_rate);

PreTlsLimiter capacity;
for (uint8_t index = 1; index <= 16; ++index) {
  assert(capacity.begin(source(index), index * 61000).allowed());
  capacity.cancel_pending();
}
assert(capacity.begin(source(17), 16 * 61000).reason ==
       RejectReason::source_table_full);
```

The test also records the global sixth accepted attempt in 60 seconds and requires the seventh to return `global_rate`.

- [ ] **Step 3: Run and observe missing admission types.**

Run:

```bash
cmake --build build/phase0/firmware-host
ctest --test-dir build/phase0/firmware-host -R pre_tls_limiter --output-on-failure
```

Expected: compilation fails because `pre_tls_limiter.hpp` is missing.

- [ ] **Step 4: Implement the fixed-capacity limiter.**

`pre_tls_limiter.hpp` fixes the public contract:

```cpp
#pragma once

#include <array>
#include <cstdint>

struct SourceKey {
  std::array<uint8_t, 16> bytes{};
  bool operator==(const SourceKey&) const = default;
};

enum class RejectReason {
  none,
  handshake_busy,
  source_rate,
  global_rate,
  source_table_full,
  invalid_token,
};

enum class Completion {
  established,
  handshake_failed,
  rejected_established_full,
  invalid_token,
};

struct Admission {
  RejectReason reason;
  uint32_t token;
  [[nodiscard]] bool allowed() const {
    return reason == RejectReason::none;
  }
};

struct AdmissionSnapshot {
  uint8_t established;
  uint8_t pending_handshakes;
  uint32_t tls_alloc_started;
  uint32_t rejected_before_tls;
};

class PreTlsLimiter {
 public:
  Admission begin(const SourceKey& source, uint64_t now_ms);
  void note_tls_alloc_started(uint32_t token);
  Completion complete(uint32_t token, bool handshake_ok);
  void cancel_pending();
  void close_established();
  [[nodiscard]] AdmissionSnapshot snapshot() const;
};
```

Implementation uses fixed arrays: 16 source records, three timestamps per source and six global timestamps. `begin()` prunes timestamps older than 60,000ms, refuses an unknown source when all 16 records remain occupied, checks per-source and global windows, checks `pending_handshakes == 0`, then returns the sole generation token. It does not allocate memory. `complete()` first releases pending state; successful TLS promotes only when `established < 4`, otherwise it returns `rejected_established_full`.

- [ ] **Step 5: Put the limiter in front of `esp_tls_init()`.**

`bounded_https_server.cpp` starts plain ESP HTTP Server with:

```cpp
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.max_open_sockets = 5;
config.lru_purge_enable = false;
config.open_fn = bounded_open;
config.close_fn = bounded_close;
```

`bounded_open()` performs `getpeername()`, normalizes IPv4 into IPv4-mapped IPv6 bytes, calls `PreTlsLimiter::begin()`, and returns `ESP_FAIL` immediately on rejection. Only an allowed lease executes:

```cpp
esp_tls_t* tls = esp_tls_init();
if (tls == nullptr) {
  limiter.complete(lease.token, false);
  return ESP_ERR_NO_MEM;
}
limiter.note_tls_alloc_started(lease.token);
const int handshake = esp_tls_server_session_create(
    &server_tls_config, sockfd, tls);
const Completion completion =
    limiter.complete(lease.token, handshake == 0);
if (completion != Completion::established) {
  esp_tls_server_session_delete(tls);
  return ESP_FAIL;
}
httpd_sess_set_transport_ctx(hd, sockfd, tls, nullptr);
ESP_ERROR_CHECK(
    httpd_sess_set_recv_override(hd, sockfd, tls_recv));
ESP_ERROR_CHECK(
    httpd_sess_set_send_override(hd, sockfd, tls_send));
return ESP_OK;
```

`bounded_close()` decrements established exactly once, deletes the TLS session, calls `httpd_sess_set_transport_ctx(hd, sockfd, nullptr, nullptr)`, closes the socket and never delegates the same pointer to a second free callback. The server exposes metrics but never exports source IPs.

- [ ] **Step 6: Compile the public-API spike before adding handlers.**

Run:

```bash
ctest --test-dir build/phase0/firmware-host -R pre_tls_limiter --output-on-failure
scripts/phase0/idf.sh -C firmware build
```

Expected: host admission tests pass; target code compiles using only `esp_http_server.h` and `esp_tls.h`. If `open_fn` cannot reject before `esp_tls_init()` or the runtime cannot hold 4 established sessions while one handshake is pending, mark Gate 5 `FAIL` and return to design.

- [ ] **Step 7: Commit the task.**

```bash
git add firmware/main/probe/pre_tls_limiter.hpp firmware/main/probe/pre_tls_limiter.cpp firmware/main/probe/bounded_https_server.hpp firmware/main/probe/bounded_https_server.cpp firmware/main/CMakeLists.txt firmware/test/host/CMakeLists.txt firmware/test/host/test_pre_tls_limiter.cpp
git commit -m "feat: bound https before tls allocation"
```

## Task 5: Exercise a Real Web Pairing, Authentication, and Request-Budget State Machine

**Files:**

- Create: `firmware/main/probe/web_guard.hpp`
- Create: `firmware/main/probe/web_guard.cpp`
- Create: `firmware/main/probe/web_handlers.hpp`
- Create: `firmware/main/probe/web_handlers.cpp`
- Modify: `firmware/main/probe/bounded_https_server.cpp`
- Modify: `firmware/main/probe/keyboard_probe.cpp`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/test/host/CMakeLists.txt`
- Create: `firmware/test/host/test_web_guard.cpp`

**Interfaces:**

- Consumes: physical pairing-window and confirmation events, monotonic milliseconds, fixed expected Host, parsed request metadata, streamed body chunks and hardware RNG.
- Produces: `WebGuard`, `RequestBudget`, five fixed admin-session slots, stable reject reasons and real HTTPS endpoints used by the HIL attack client.

- [ ] **Step 1: Write the failing pairing and write-auth tests.**

`firmware/test/host/test_web_guard.cpp`:

```cpp
#include <array>
#include <cassert>
#include <span>
#include <string>
#include "probe/web_guard.hpp"

class DeterministicRandom final : public RandomSource {
 public:
  explicit DeterministicRandom(uint8_t first) : next_(first) {}

  void fill(std::span<uint8_t> output) override {
    for (uint8_t& byte : output) {
      byte = next_++;
    }
  }

 private:
  uint8_t next_;
};

int main() {
  DeterministicRandom random(0x42);
  WebGuard guard("cardputer-codex-3f2a.local", random);
  guard.open_pairing_window("12345678", 0);

  const auto submitted =
      guard.submit_pairing_code("12345678", "Safari", 1000);
  assert(submitted == PairingResult::awaiting_physical_confirmation);
  assert(!guard.has_admin_session());

  const auto credential = guard.confirm_pairing(true, 2000);
  assert(credential.has_value());
  assert(guard.has_admin_session());

  RequestMeta valid{
      .method = HttpMethod::post,
      .path = "/api/v1/probe/echo",
      .host = "cardputer-codex-3f2a.local",
      .origin = "https://cardputer-codex-3f2a.local",
      .cookie_token = credential->cookie_token,
      .csrf_token = credential->csrf_token,
      .header_bytes = 512,
      .content_length = 16,
      .now_ms = 3000,
  };
  assert(guard.authorize(valid).reason == WebReject::none);

  RequestMeta wrong_origin = valid;
  wrong_origin.origin = "https://attacker.invalid";
  assert(guard.authorize(wrong_origin).reason ==
         WebReject::origin_mismatch);

  RequestMeta wrong_csrf = valid;
  wrong_csrf.csrf_token = std::string(64, '0');
  assert(guard.authorize(wrong_csrf).reason ==
         WebReject::csrf_mismatch);

  RequestMeta expired = valid;
  expired.now_ms = 1802001;
  assert(guard.authorize(expired).reason ==
         WebReject::session_expired);
  return 0;
}
```

- [ ] **Step 2: Add failing brute-force and request-size tests.**

Append these independent cases:

```cpp
WebGuard brute_force("cardputer-codex-3f2a.local", random);
brute_force.open_pairing_window("87654321", 0);
for (uint8_t attempt = 0; attempt < 4; ++attempt) {
  assert(brute_force.submit_pairing_code(
             "00000000", "Browser", attempt * 1000) ==
         PairingResult::invalid_code);
}
assert(brute_force.submit_pairing_code(
           "00000000", "Browser", 4000) ==
       PairingResult::backoff_started);
assert(brute_force.submit_pairing_code(
           "87654321", "Browser", 5000) ==
       PairingResult::in_backoff);
assert(brute_force.backoff_remaining_ms(5000) == 599000);

WebGuard capacity("cardputer-codex-3f2a.local", random);
for (uint8_t index = 0; index < 5; ++index) {
  const std::string code = std::to_string(11111111 + index);
  capacity.open_pairing_window(code, index * 10000);
  assert(capacity.submit_pairing_code(
             code, "Browser", index * 10000 + 1000) ==
         PairingResult::awaiting_physical_confirmation);
  assert(capacity.confirm_pairing(
             true, index * 10000 + 2000).has_value());
}
assert(capacity.admin_session_count() == 5);
capacity.open_pairing_window("22222222", 60000);
assert(capacity.submit_pairing_code(
           "22222222", "Sixth", 61000) ==
       PairingResult::awaiting_physical_confirmation);
assert(!capacity.confirm_pairing(true, 62000).has_value());

assert(RequestBudget::for_request(
           "/api/v1/probe/echo", 8192, 16384)
           .accepted());
assert(RequestBudget::for_request(
           "/api/v1/probe/echo", 8193, 1)
           .reason == WebReject::header_too_large);
assert(RequestBudget::for_request(
           "/api/v1/probe/echo", 100, 16385)
           .reason == WebReject::body_too_large);
assert(RequestBudget::for_request(
           "/api/v1/config/import", 100, 131072)
           .accepted());
assert(RequestBudget::for_request(
           "/api/v1/config/import", 100, 131073)
           .reason == WebReject::body_too_large);

JsonDepthTracker depth;
assert(depth.consume("{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":{\"g\":{\"h\":1}}}}}}}}"));
assert(depth.maximum_depth() == 8);
JsonDepthTracker too_deep;
assert(!too_deep.consume("{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":{\"g\":{\"h\":{\"i\":1}}}}}}}}}"));
```

- [ ] **Step 3: Run and observe the missing Web guard.**

Run:

```bash
cmake --build build/phase0/firmware-host
ctest --test-dir build/phase0/firmware-host -R web_guard --output-on-failure
```

Expected: compilation fails because `web_guard.hpp` is missing.

- [ ] **Step 4: Implement pairing and fixed-session state.**

`firmware/main/probe/web_guard.hpp` defines:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include "pre_tls_limiter.hpp"

class RandomSource {
 public:
  virtual ~RandomSource() = default;
  virtual void fill(std::span<uint8_t> output) = 0;
};

enum class PairingResult {
  invalid_code,
  awaiting_physical_confirmation,
  backoff_started,
  in_backoff,
  window_closed,
};

enum class HttpMethod { get, post, put, delete_ };

enum class WebReject {
  none,
  rate_limited,
  header_too_large,
  body_too_large,
  json_too_deep,
  frame_too_large,
  unauthenticated,
  session_expired,
  host_mismatch,
  origin_mismatch,
  csrf_mismatch,
  pairing_required,
};

struct AdminCredential {
  std::string cookie_token;
  std::string csrf_token;
};

struct RequestMeta {
  HttpMethod method;
  std::string_view path;
  SourceKey source;
  std::string_view host;
  std::string_view origin;
  std::string_view cookie_token;
  std::string_view csrf_token;
  uint32_t header_bytes;
  uint32_t content_length;
  uint64_t now_ms;
};

struct WebDecision {
  WebReject reason;
  uint16_t http_status_code;
};

struct RequestBudget {
  WebReject reason;
  uint32_t limit;
  uint32_t consumed;

  static RequestBudget for_request(std::string_view path,
                                   uint32_t header_bytes,
                                   uint32_t content_length);
  [[nodiscard]] bool accepted() const {
    return reason == WebReject::none;
  }
  bool consume(uint32_t chunk_bytes);
};

class JsonDepthTracker {
 public:
  bool consume(std::string_view chunk);
  [[nodiscard]] uint8_t maximum_depth() const;
};

class WebGuard {
 public:
  WebGuard(std::string expected_host, RandomSource& random);
  void open_pairing_window(std::string_view eight_digit_code,
                           uint64_t now_ms);
  PairingResult submit_pairing_code(std::string_view code,
                                    std::string_view browser_name,
                                    uint64_t now_ms);
  std::optional<AdminCredential> confirm_pairing(bool accepted,
                                                  uint64_t now_ms);
  [[nodiscard]] bool has_admin_session() const;
  [[nodiscard]] uint8_t admin_session_count() const;
  [[nodiscard]] uint64_t backoff_remaining_ms(uint64_t now_ms) const;
  WebDecision authorize(const RequestMeta& request);
};
```

`WebGuard` stores only SHA-256 digests of the eight-digit pairing code, Cookie and CSRF tokens. Code, token and CSRF comparisons use constant-time byte comparison. `open_pairing_window()` sets a hard expiry at `now_ms + 300000`; the fifth failure closes the window and sets `backoff_until_ms = now_ms + 600000`. Correct code changes state only to `awaiting_physical_confirmation`; `confirm_pairing(true)` is callable only from the physical keyboard/G0 event path and allocates one of five fixed session slots. A sixth admin client is rejected without evicting an active client.

Each session stores browser name limited to 64 UTF-8 bytes, credential digests and `last_used_ms`. `authorize()` expires a session when `now_ms - last_used_ms > 1800000`; a successful request updates `last_used_ms`.

- [ ] **Step 5: Implement exact Host, Origin, CSRF, rate and body rules.**

The guard applies checks in this order:

1. Header length and declared body length before body allocation.
2. Per-source and global rate limit before authentication work.
3. Exact Host match for every route.
4. Cookie authentication for every route except `/healthz` and pairing submission.
5. Exact `Origin == "https://" + expected_host` and CSRF for POST, PUT and DELETE.
6. Streaming JSON depth and accumulated body bytes.

`RequestBudget` uses `8192`, `16384` and `131072` byte constants. `JsonDepthTracker` tracks `in_string`, `escaped`, current depth and maximum depth so braces inside strings do not change depth. It rejects depth nine on the incoming byte and never buffers the import body.

Use fixed-point token buckets:

```cpp
struct TokenBucket {
  uint32_t tokens_milli;
  uint64_t updated_ms;

  bool consume(uint32_t rate_per_second, uint32_t burst,
               uint64_t now_ms) {
    const uint64_t elapsed = now_ms - updated_ms;
    const uint64_t refill =
        elapsed * static_cast<uint64_t>(rate_per_second);
    tokens_milli = static_cast<uint32_t>(
        std::min<uint64_t>(burst * 1000ULL,
                           tokens_milli + refill));
    updated_ms = now_ms;
    if (tokens_milli < 1000) {
      return false;
    }
    tokens_milli -= 1000;
    return true;
  }
};
```

Create fixed buckets for authenticated 10/s burst 20, unauthenticated per-source 1/s burst 4, and global `/healthz` 10/s. Unknown seventeenth unauthenticated source fails closed; it never causes table growth or eviction during the run.

- [ ] **Step 6: Wire real HTTPS handlers to the guard.**

Register exactly these probe routes:

| Method | Route | Required behavior |
|---|---|---|
| GET | `/healthz` | Fixed `ok`, firmware version and pairing-required only; global 10/s |
| POST | `/api/v1/web-pairing/submit` | Eight-digit code and browser name; no code in logs |
| GET | `/api/v1/probe/session` | Authenticated session and CSRF bootstrap response |
| POST | `/api/v1/probe/echo` | Authenticated write used for Host/Origin/CSRF/rate tests |
| POST | `/api/v1/config/import` | Authenticated streaming 128KiB/depth-eight probe |
| GET | `/api/v1/probe/ws` | Authenticated WebSocket upgrade; 16KiB frame maximum |

Successful physical confirmation builds the response headers from the generated credential:

```cpp
const std::string cookie =
    "cp_admin=" + credential.cookie_token +
    "; Secure; HttpOnly; SameSite=Strict; Path=/";
httpd_resp_set_hdr(request, "Set-Cookie", cookie.c_str());
httpd_resp_set_hdr(request, "Cache-Control", "no-store");
```

The response writer never logs `cookie` or `credential.cookie_token`. No response includes `Access-Control-Allow-Origin`. Reject mapping is fixed: `401` unauthenticated/expired, `403` Host/Origin/CSRF, `413` Header/Body/depth/frame, `429` rate/backoff, `503` session capacity. An oversized WebSocket frame closes with code `1009`.

- [ ] **Step 7: Verify unit tests and the real target handlers.**

Run:

```bash
cmake --build build/phase0/firmware-host
ctest --test-dir build/phase0/firmware-host -R web_guard --output-on-failure
scripts/phase0/idf.sh -C firmware build
```

Expected: pairing, expiry, brute-force, Host, Origin, CSRF, request-budget and JSON-depth tests pass; the target image links all six handlers.

- [ ] **Step 8: Commit the task.**

```bash
git add firmware/main/probe/web_guard.hpp firmware/main/probe/web_guard.cpp firmware/main/probe/web_handlers.hpp firmware/main/probe/web_handlers.cpp firmware/main/probe/bounded_https_server.cpp firmware/main/probe/keyboard_probe.cpp firmware/main/CMakeLists.txt firmware/test/host/CMakeLists.txt firmware/test/host/test_web_guard.cpp
git commit -m "feat: add fail-closed web management probe"
```

## Task 6: Build an External WSS Transport with SPKI Pinning and TLS Exporter Binding

**Files:**

- Create: `firmware/main/probe/pinned_wss_transport.hpp`
- Create: `firmware/main/probe/pinned_wss_transport.cpp`
- Modify: `firmware/main/probe/probe_controller.cpp`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/test/host/CMakeLists.txt`
- Create: `firmware/test/host/test_wss_contract.cpp`

**Interfaces:**

- Consumes: Companion host/port/certificate, device P-256 signer, and `protocol_vectors::wss` generated exclusively from foundation `wss-auth-v1.md` plus `fixtures/wss-auth-v1.json`.
- Produces: an `esp_transport_handle_t` WebSocket transport supplied through `esp_websocket_client_config_t.ext_transport`, verified exporter bytes kept in volatile memory, and WSS authentication state transitions.

- [ ] **Step 1: Write failing pin and canonical-auth-message tests.**

`firmware/test/host/test_wss_contract.cpp`:

```cpp
#include <array>
#include <cassert>
#include "phase0_protocol_vectors.hpp"
#include "probe/pinned_wss_transport.hpp"

int main() {
  auto changed = protocol_vectors::wss::spki_sha256;
  changed[31] = 2;
  assert(spki_pin_matches(protocol_vectors::wss::spki_sha256,
                          protocol_vectors::wss::spki_sha256));
  assert(!spki_pin_matches(protocol_vectors::wss::spki_sha256,
                           changed));

  const auto encoded =
      encode_wss_auth(protocol_vectors::wss::input);
  assert(encoded == protocol_vectors::wss::canonical_message);
  assert(verify_wss_signature(
      protocol_vectors::wss::input,
      protocol_vectors::wss::signature,
      protocol_vectors::wss::device_public_key));

  auto mutated = protocol_vectors::wss::input;
  mutated.exporter[0] ^= 0x01;
  assert(!verify_wss_signature(
      mutated, protocol_vectors::wss::signature,
      protocol_vectors::wss::device_public_key));
  return 0;
}
```

The test contains no locally chosen field ordering, width, label or signature format. The generated types and expected bytes are the only WSS encoding contract.

- [ ] **Step 2: Run and observe the missing WSS transport.**

Run:

```bash
cmake --build build/phase0/firmware-host
ctest --test-dir build/phase0/firmware-host -R wss_contract --output-on-failure
```

Expected: compilation fails because `pinned_wss_transport.hpp` is missing.

- [ ] **Step 3: Implement constant-time pin verification and exporter extraction through public APIs.**

Provision the exact Companion certificate DER together with its independently computed SPKI digest. `esp_tls_cfg_t` uses that certificate as its trust anchor, keeps hostname verification enabled and never sets a skip-verification flag. After `esp_tls_conn_new_sync()` succeeds, obtain only the public context:

```cpp
auto* ssl = static_cast<mbedtls_ssl_context*>(
    esp_tls_get_ssl_context(context->tls));
if (ssl == nullptr) {
  return TransportError::missing_ssl_context;
}
const mbedtls_x509_crt* peer = mbedtls_ssl_get_peer_cert(ssl);
if (peer == nullptr) {
  return TransportError::missing_peer_certificate;
}

std::array<uint8_t, 512> der_buffer{};
const int der_length = mbedtls_pk_write_pubkey_der(
    &peer->pk, der_buffer.data(), der_buffer.size());
if (der_length <= 0) {
  return TransportError::spki_encoding_failed;
}
const uint8_t* spki =
    der_buffer.data() + der_buffer.size() - der_length;
std::array<uint8_t, 32> observed_pin{};
mbedtls_sha256(spki, der_length, observed_pin.data(), 0);
if (!spki_pin_matches(context->expected_pin, observed_pin)) {
  return TransportError::spki_mismatch;
}

const int exported = mbedtls_ssl_export_keying_material(
    ssl, context->exporter.data(), context->exporter.size(),
    protocol_vectors::wss::exporter_label.data(),
    protocol_vectors::wss::exporter_label.size(),
    nullptr, 0, 0);
if (exported != 0) {
  return TransportError::exporter_failed;
}
```

The final `nullptr, 0, 0` is normative: foundation fixes an empty exporter context, and the generated WSS fixture must assert `exporter_context_hex=""`. Any non-empty context on either platform is a protocol mismatch and fails the blocker spike.

`spki_pin_matches()` compares all 32 bytes without early exit. The code requires `CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE=y` and `CONFIG_MBEDTLS_SSL_KEYING_MATERIAL_EXPORT=y`. It never logs the exporter or auth signature.

- [ ] **Step 4: Wrap the owned ESP-TLS connection as the external WebSocket transport.**

Create a base `esp_transport_handle_t` with `esp_transport_init()`. Its connect callback owns `esp_tls_t`, validates SPKI and derives exporter; its read/write/close/poll/destroy callbacks use public ESP-TLS and socket APIs. Wrap it with:

```cpp
esp_transport_handle_t base = esp_transport_init();
ESP_ERROR_CHECK(esp_transport_set_context_data(base, context));
ESP_ERROR_CHECK(esp_transport_set_func(
    base, pinned_connect, pinned_read, pinned_write, pinned_close,
    pinned_poll_read, pinned_poll_write, pinned_destroy));
esp_transport_handle_t websocket = esp_transport_ws_init(base);
const esp_transport_ws_config_t websocket_config{
    .ws_path = context->websocket_path.c_str(),
    .sub_protocol = context->websocket_subprotocol.c_str(),
    .user_agent = "cardputer-phase0",
    .headers = nullptr,
    .auth = nullptr,
    .propagate_control_frames = true,
};
ESP_ERROR_CHECK(
    esp_transport_ws_set_config(websocket, &websocket_config));
ESP_ERROR_CHECK(
    esp_transport_set_default_port(websocket, companion_port));
esp_websocket_client_config_t config{};
config.uri = companion_uri;
config.ext_transport = websocket;
config.disable_auto_reconnect = true;
config.buffer_size = 4096;
```

The returned `websocket` handle, not the base TLS handle, is assigned to `ext_transport`. Source review must find no `transport_esp_tls_t`, no cast to a private transport context and no direct read of private mbedTLS members.

- [ ] **Step 5: Authenticate the current TLS channel and reject replay on the Companion side.**

After WebSocket upgrade, firmware validates the received challenge through the generated WSS input contract, builds it with `protocol_vectors::wss::make_runtime_input(context->exporter, challenge, context->identity)`, signs exactly the result of `encode_wss_auth()`, and serializes the signature with the generated foundation encoder. No signature conversion rule is defined in this plan. `ProbeController` sets `wss_authenticated=true` only after a Companion `auth_ok` bound to the current connection generation.

For the negative HIL, firmware exposes a phase-zero-only control that reconnects and resends the prior connection's signature without the private key operation. The expected rejection event is emitted by the Mac:

```json
{
  "producer": "macos_companion",
  "kind": "wss_auth",
  "case": "prior_connection_signature",
  "accepted": false,
  "reason": "tls_exporter_mismatch"
}
```

This WSS negative path is distinct from GATT replay. GATT duplicate/counter rejection remains entirely in the Companion receiver test.

- [ ] **Step 6: Compile and scan the public-API blocker spike.**

Run:

```bash
ctest --test-dir build/phase0/firmware-host -R wss_contract --output-on-failure
scripts/phase0/idf.sh -C firmware build
if rg -n 'transport_esp_tls_t|priv_include|MBEDTLS_PRIVATE' firmware/main/probe/pinned_wss_transport.cpp; then exit 1; fi
if rg -n 'EXPORTER-|pair-root|pairing-root|gatt-auth|pairing-sas' firmware/main/probe/pinned_wss_transport.cpp; then exit 1; fi
```

Expected: host contract matches the foundation WSS golden vector, target transport compiles, and both forbidden scans print nothing. If exporter extraction, generated canonical encoding or external transport ownership cannot compile against the pinned public APIs, record a blocker/error measurement and return to design; this child report does not assign a Gate 1 verdict.

- [ ] **Step 7: Commit the task.**

```bash
git add firmware/main/probe/pinned_wss_transport.hpp firmware/main/probe/pinned_wss_transport.cpp firmware/main/probe/probe_controller.cpp firmware/main/CMakeLists.txt firmware/test/host/CMakeLists.txt firmware/test/host/test_wss_contract.cpp
git commit -m "feat: bind wss auth to pinned tls transport"
```

## Task 7: Instrument Heap, Stack, Allocation, Queues, and HID Latency

**Files:**

- Create: `firmware/main/probe/resource_metrics.hpp`
- Create: `firmware/main/probe/resource_metrics.cpp`
- Modify: `firmware/main/probe/keyboard_probe.hpp`
- Modify: `firmware/main/probe/keyboard_probe.cpp`
- Modify: `firmware/main/probe/bounded_https_server.cpp`
- Modify: `firmware/main/probe/web_handlers.cpp`
- Modify: `firmware/main/probe/pinned_wss_transport.cpp`
- Modify: `firmware/main/app_main.cpp`
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/test/host/CMakeLists.txt`
- Create: `firmware/test/host/test_resource_metrics.cpp`

**Interfaces:**

- Consumes: `stable_at_us`, actual HID queue result/time, ESP internal heap APIs, failed-allocation callback, all probe task handles/configured stack bytes, HTTPS admission snapshot and bounded queue counters.
- Produces: fixed-memory `HidLatencyMetrics`, `ResourceSample`, task stack samples and newline-delimited serial evidence carrying the immutable run identity.

- [ ] **Step 1: Write failing HID histogram and inclusive-threshold tests.**

`firmware/test/host/test_resource_metrics.cpp`:

```cpp
#include <cassert>
#include "probe/resource_metrics.hpp"

int main() {
  HidLatencyMetrics hid;
  for (uint32_t index = 0; index < 10000; ++index) {
    hid.observe(1000000, 1019900, true);
  }
  assert(hid.generated == 10000);
  assert(hid.queued == 10000);
  assert(hid.queue_failures == 0);
  assert(hid.p95_upper_bound_us() == 19900);
  assert(evaluate_hid(hid).passed);

  hid.observe(2000000, 2000000, false);
  assert(!evaluate_hid(hid).passed);

  ResourceThresholdInput boundary{
      .steady_free_internal = 65536,
      .steady_largest_internal = 32768,
      .tls_burst_free_internal = 40960,
      .allocation_failures = 0,
  };
  assert(evaluate_resource_thresholds(boundary).passed);
  boundary.tls_burst_free_internal = 40959;
  assert(!evaluate_resource_thresholds(boundary).passed);

  assert(required_stack_free(4096) == 1024);
  assert(required_stack_free(8192) == 1638);

  BurstMetrics burst{
      .window_us = 5000000,
      .wss_frames = 100,
      .wss_bytes = 100 * 16384,
      .import_bytes = 131072,
      .session_items = 20,
      .approval_fragments = 4,
      .approval_bytes = 65536,
  };
  assert(validate_transient_burst(burst) == BurstError::none);
  burst.approval_fragments = 3;
  assert(validate_transient_burst(burst) ==
         BurstError::approval_fragment_count);
  return 0;
}
```

- [ ] **Step 2: Run and observe missing metrics types.**

Run:

```bash
cmake --build build/phase0/firmware-host
ctest --test-dir build/phase0/firmware-host -R resource_metrics --output-on-failure
```

Expected: compilation fails because `resource_metrics.hpp` is missing.

- [ ] **Step 3: Implement an allocation-free HID histogram.**

`firmware/main/probe/resource_metrics.hpp`:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include "probe_types.hpp"

struct HidLatencyMetrics {
  uint32_t generated = 0;
  uint32_t queued = 0;
  uint32_t queue_failures = 0;
  std::array<uint32_t, 1002> buckets{};

  void observe(int64_t stable_at_us, int64_t queued_at_us,
               bool queued_ok);
  [[nodiscard]] uint32_t p95_upper_bound_us() const;
};

struct TaskStackMetric {
  const char* name;
  uint32_t configured_bytes;
  uint32_t high_water_free_bytes;
};

struct BurstMetrics {
  uint64_t window_us = 0;
  uint32_t wss_frames = 0;
  uint32_t wss_bytes = 0;
  uint32_t import_bytes = 0;
  uint16_t session_items = 0;
  uint16_t approval_fragments = 0;
  uint32_t approval_bytes = 0;
};

enum class BurstError {
  none,
  window_duration,
  wss_frame_count,
  wss_byte_count,
  import_byte_count,
  session_item_count,
  approval_fragment_count,
  approval_byte_count,
};

BurstError validate_transient_burst(const BurstMetrics& metrics);

struct ResourceSample {
  ProbeIdentity identity;
  uint64_t monotonic_us;
  uint32_t free_internal_heap;
  uint32_t largest_internal_block;
  uint32_t allocation_failures;
  uint8_t https_established;
  uint8_t https_pending_handshakes;
  HidLatencyMetrics hid;
  BurstMetrics burst;
  std::array<TaskStackMetric, 7> tasks;
};
```

Buckets use 100µs upper bounds from zero through 100,000µs and bucket 1001 for overflow. `observe()` increments `generated` before queue outcome; failed enqueue increments `queue_failures` and does not fabricate latency. Successful enqueue increments `queued` and the ceiling bucket. `p95_upper_bound_us()` uses nearest rank `(queued * 95 + 99) / 100`; `evaluate_hid()` first requires at least 10,000 generated, equality of generated/queued, zero failures, no overflow and p95 `<=20000`. These local helpers return validation errors for immediate diagnostics but their boolean convenience fields are not serialized as gate verdicts.

- [ ] **Step 4: Instrument internal heap and every long-lived task.**

At 1Hz and each scenario boundary, sample:

```cpp
sample.free_internal_heap = heap_caps_get_free_size(
    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
sample.largest_internal_block =
    heap_caps_get_largest_free_block(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
sample.allocation_failures =
    allocation_failure_count.load(std::memory_order_relaxed);
```

Register `heap_caps_register_failed_alloc_callback()` before starting BLE or Wi-Fi. Record scanner, HID sender, NimBLE host, HTTPS, WSS, display and metrics task handles. On ESP-IDF's FreeRTOS port, store `uxTaskGetStackHighWaterMark2(handle)` directly as bytes; do not multiply by `sizeof(StackType_t)`. Each task passes only when `high_water_free_bytes >= max(configured_bytes / 5, 1024)`.

- [ ] **Step 5: Make queue boundaries measurable and non-blocking.**

Use these fixed capacities:

```cpp
inline constexpr size_t kHidQueueDepth = 32;
inline constexpr size_t kNetworkQueueDepth = 16;
inline constexpr size_t kDisplayQueueDepth = 8;
```

Scanner-to-HID uses `xQueueSend(hid_queue, &event, 0)`. Network and display fan-out also use zero wait and increment their own overflow counters. No scanner/HID source file may include ESP-TLS, HTTP server, Wi-Fi, display or NVS headers. WSS uses a 4096-byte working buffer and incremental 16KiB frame validation. Config import uses a 1024-byte streaming buffer.

The transient controller opens one exact 5,000,000µs measurement window and resets only its fixed counters at the boundary. It counts 100 completed 16KiB WSS frames without retaining their payloads, a streamed 131,072-byte import, 20 normalized session items, and four approval fragments totaling 65,536 bytes. `validate_transient_burst()` returns an error for any smaller count, any larger time window or non-overlapping timestamps.

- [ ] **Step 6: Emit fixed-size JSONL evidence without leaking requests.**

The metrics task formats into one 4096-byte static buffer and writes one serial line per sample. Each line includes the evidence identity quintet, scenario, occupancy, heap, largest block, allocations, queue counters, HID totals/histogram summary and all stack metrics. It excludes raw device ID, source addresses, pairing code, Cookie, CSRF, request bodies, exporter, signatures and text payloads. A truncated line increments `metrics_encode_failure` and makes the run fail.

- [ ] **Step 7: Verify host metrics, include boundaries and target build.**

Run:

```bash
ctest --test-dir build/phase0/firmware-host -R resource_metrics --output-on-failure
if rg -l 'esp_tls|esp_http|esp_wifi|M5Display|nvs_' firmware/main/probe/keyboard_probe.cpp firmware/main/probe/hid_engine.cpp; then exit 1; fi
scripts/phase0/idf.sh -C firmware build
```

Expected: metric tests pass, keyboard/HID source scan is empty, and target build succeeds.

- [ ] **Step 8: Commit the task.**

```bash
git add firmware/main/probe/resource_metrics.hpp firmware/main/probe/resource_metrics.cpp firmware/main/probe/keyboard_probe.hpp firmware/main/probe/keyboard_probe.cpp firmware/main/probe/bounded_https_server.cpp firmware/main/probe/web_handlers.cpp firmware/main/probe/pinned_wss_transport.cpp firmware/main/app_main.cpp firmware/main/CMakeLists.txt firmware/test/host/CMakeLists.txt firmware/test/host/test_resource_metrics.cpp
git commit -m "test: instrument firmware concurrency limits"
```

## Task 8: Run One Independent 30-Minute Concurrency HIL and Seal the Evidence

**Files:**

- Create: `scripts/phase0/run_concurrency_hil.py`
- Modify: `tools/phase0/validate_concurrency_report.py`
- Create: `tools/phase0/tests/test_run_concurrency_hil.py`
- Update: `tools/phase0/tests/test_concurrency_report.py`
- Create: `docs/validation/phase0/firmware-concurrency-hil.md`

**Interfaces:**

- Consumes: unique Cardputer USB device, built firmware binary, validated hardware manifest, 17 assigned/routable LAN source addresses, interactive hidden Web pairing code, physical device confirmation, `companion/.build/release/cardputer-phase0-probe`, selected Companion interface/address/netmask, CoreBluetooth peripheral UUID, expected raw device ID, mode-`0600` GATT secret file and Keychain TLS identity label.
- Produces: verdict-free `build/phase0/firmware-concurrency/report.json`, hashed raw artifacts and a redacted Markdown measurement summary for the three foundation consumers listed above.

- [ ] **Step 1: Write failing same-window and measurement-validation tests.**

`tools/phase0/tests/test_run_concurrency_hil.py`:

```python
from scripts.phase0.run_concurrency_hil import (
    EvidenceClock,
    validate_hid_measurement,
    validate_continuous_window,
)


def test_cross_boot_or_nonoverlap_is_rejected() -> None:
    records = [
        EvidenceClock(
            producer="firmware",
            run_id="run-a",
            boot_id="boot-a",
            app_elf_sha256="22" * 32,
            firmware_image_sha256="33" * 32,
            device_id_sha256="11" * 32,
            first_ns=0,
            last_ns=120_000_000_000,
        ),
        EvidenceClock(
            producer="macos_companion",
            run_id="run-a",
            boot_id="boot-b",
            app_elf_sha256="22" * 32,
            firmware_image_sha256="33" * 32,
            device_id_sha256="11" * 32,
            first_ns=10_000_000_000,
            last_ns=110_000_000_000,
        ),
    ]
    assert validate_continuous_window(records) == [
        "boot_id differs across evidence"
    ]


def test_queue_loss_is_reported_even_when_p95_is_low() -> None:
    hid = {
        "generated": 10000,
        "queued": 9999,
        "queue_failures": 1,
        "p95_upper_bound_us": 1000,
        "overflow_samples": 0,
        "release_all_observed": True,
    }
    assert validate_hid_measurement(hid) == [
        "generated must equal queued",
        "queue_failures must equal zero",
    ]
```

Add a report fixture whose firmware, attacker and Companion artifacts overlap for exactly 60 seconds and validate it; then change one artifact digest and require schema evaluation to fail.

- [ ] **Step 2: Run and observe the missing runner.**

Run:

```bash
uv run pytest tools/phase0/tests/test_run_concurrency_hil.py tools/phase0/tests/test_concurrency_report.py -q
```

Expected: collection fails because `run_concurrency_hil.py` and its evaluator types do not exist.

- [ ] **Step 3: Implement strict preflight and destructive-action barriers.**

The runner supports this exact CLI:

```text
run_concurrency_hil.py
  --auto-port
  --firmware-bin firmware/build/cardputer_codex_phase0.bin
  --hardware-manifest build/phase0/hardware-manifest.json
  --companion-probe companion/.build/release/cardputer-phase0-probe
  --companion-interface en0
  --companion-address 192.168.1.10
  --companion-netmask 255.255.255.0
  --companion-peripheral-id aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee
  --companion-device-id-hex 00112233445566778899aabbccddeeff
  --companion-gatt-secret-file build/phase0/concurrency/gatt-secret.bin
  --companion-tls-identity-label cardputer-phase0-wss
  --attacker-interface auto
  --duration-seconds 1800
  --output build/phase0/firmware-concurrency
```

Before flashing it must:

1. Find exactly one ESP32-S3 serial device or set `capture_complete=false` and append a machine-readable blocker.
2. Read chip identity and 8MiB Flash size.
3. Display only the sanitized device digest and require the operator to type it back.
4. Read all 8MiB into the output directory, fsync it and record SHA-256.
5. Verify the manifest, IDF lock, firmware image SHA-256 and embedded application ELF SHA.
6. Refuse a duration below 1800 seconds for a gate run.
7. Enumerate assigned addresses on the selected LAN interface and require 17 distinct routable source addresses; it never adds or removes interface aliases.
8. Verify the Companion probe is executable and advertises the exact `concurrency-hil-agent` option set. After the one flash/boot identity is captured, spawn that subcommand with the runner-owned `run_id`, observed `boot_id`, parsed app ELF SHA-256, flashed image SHA-256 and all explicit Companion parameters. Read stdout as one JSON object per line, add `observed_at_ns=time.monotonic_ns()` on receipt, validate against `companion-probe-event.schema.json`, and require `ready` followed by a healthy `heartbeat` within five seconds. Stderr is redacted diagnostics only and never supplies measurements.

The runner owns the child lifecycle: it never invokes `pair-gatt-hil`, never guesses interface/peripheral values, and never merges a prior agent output. It terminates the run if the agent exits early, produces malformed/non-canonical JSONL, repeats `ready`/`stopped`, omits a heartbeat for over five seconds or reports an identity/digest mismatch. On normal completion it waits for the agent's unique `stopped` event and exit 0. In a `finally` block it terminates the child if needed and unlinks the GATT secret input on success, failure, blocker, exception or interrupt; the child also deletes it immediately after reading.

The pairing code is read through `getpass.getpass()` and passed directly over TLS. It is never placed in argv, environment, files or logs. The script pauses for physical window start and physical confirmation; non-interactive execution appends a blocker and leaves `capture_complete=false`.

- [ ] **Step 4: Enforce one firmware, one boot, one run, and one receipt clock.**

After flashing once, the runner opens one serial capture and must not reset or reflash until completion. It generates `run_id`, parses and sends the exact flashed image byte length to firmware, and attaches local `time.monotonic_ns()` receipt times to serial, attacker and Companion records. Firmware must return a running-partition digest equal to the runner's SHA-256 of those exact flashed bytes before the Companion agent is started.

Use this validator core:

```python
from dataclasses import dataclass


@dataclass(frozen=True)
class EvidenceClock:
    producer: str
    run_id: str
    boot_id: str
    app_elf_sha256: str
    firmware_image_sha256: str
    device_id_sha256: str
    first_ns: int
    last_ns: int


def validate_continuous_window(
    records: list[EvidenceClock],
) -> list[str]:
    errors: list[str] = []
    for key in (
        "run_id",
        "boot_id",
        "app_elf_sha256",
        "firmware_image_sha256",
        "device_id_sha256",
    ):
        if len({getattr(record, key) for record in records}) != 1:
            errors.append(f"{key} differs across evidence")
    overlap_start = max(record.first_ns for record in records)
    overlap_end = min(record.last_ns for record in records)
    if overlap_end - overlap_start < 60_000_000_000:
        errors.append("evidence overlap is shorter than 60 seconds")
    return errors
```

The runner also compares runtime `app_elf_sha256` with the value parsed from the flashed binary. Any serial reconnect, new boot event, device ID change, WSS Companion instance change or capture gap over five seconds fails the whole run. It never imports evidence from another output directory.

- [ ] **Step 5: Run the fixed 30-minute scenario schedule.**

The schedule totals exactly 1800 seconds:

| Runner seconds | Scenario | Required live state |
|---:|---|---|
| 0–119 | warmup | HID, encrypted GATT, Wi-Fi, HTTPS, WSS authenticated |
| 120–599 | steady | same five services, four authenticated HTTPS sessions held open, display/CJK lookup events active |
| 600–899 | transient | four established plus one slow handshake; within one 5-second sub-window inject 100 valid 16KiB WSS frames, one streaming 128KiB import, one 20-session page and one 64KiB approval split into four canonical fragments |
| 900–1679 | attack | unauthenticated TLS/HTTP/WebSocket/pairing attack plus 10,000 post-debounce HID events |
| 1680–1799 | recovery | five services healthy, four test sessions closed, final `release all` and final metrics |

At least one continuous 60-second interval must show all five services, `https_established=4`, `https_pending_handshakes=1`, one Mac heartbeat and unchanged evidence identity quintet. A successful fifth handshake cannot become an established fifth session.

- [ ] **Step 6: Execute the complete real-Web and transport attack matrix.**

The runner records attempted, accepted, rejected, expected HTTP status code/reason, firmware pre/post allocation counters and heap delta for every case. Raw JSON names the numeric field `http_status_code`; it never uses a verdict field named `status`:

| Case | Required result |
|---|---|
| Second concurrent TLS handshake | rejected before `tls_alloc_started` increments |
| Fourth per-source handshake inside 60s | rejected `source_rate` |
| Seventh global handshake inside 60s | rejected `global_rate` |
| Seventeenth distinct source | rejected `source_table_full` without table allocation |
| Fifth established HTTPS session | never established |
| Unauthenticated fifth request burst | `429` |
| Authenticated twenty-first request burst | `429` |
| Global eleventh `/healthz` request burst | `429` |
| Header 8192/8193 bytes | boundary accepted/rejected |
| Normal body 16384/16385 bytes | boundary accepted/rejected |
| Import 131072/131073 bytes | streamed accepted/rejected |
| JSON depth 8/9 | accepted/rejected |
| WebSocket frame 16384/16385 bytes | accepted/close `1009` |
| Missing or wrong Cookie | `401` |
| Wrong Host | `403` |
| Missing or wrong Origin on write | `403` |
| Missing or wrong CSRF on write | `403` |
| Cross-origin preflight | no CORS allow header |
| Five incorrect pairing codes | window closes; `Retry-After: 600` |
| Correct code during backoff | rejected without code comparison success |
| Prior WSS signature on new TLS connection | Companion rejects `tls_exporter_mismatch` |
| Foundation GATT duplicate/rollback/window/old-connection vectors | Companion-produced replay report matches every canonical accept/reject result |
| 100 valid 16KiB WSS frames in one 5s window | all increment the bounded event count without full-frame queue retention |
| 128KiB import + 20-session page + four-fragment 64KiB approval in the same 5s window | streamed/fragmented counts complete, no allocation failure, heap sample covers the overlap |

The Web pairing brute-force case runs only after the first valid admin credential is established, so the retained authenticated session can complete the remaining write tests during pairing backoff.

- [ ] **Step 7: Validate raw measurement completeness without claiming a gate verdict.**

The concurrency measurement group has no consistency error only when:

- the five service booleans overlap for at least 60 seconds;
- HID serial decodes to the exact encrypted GATT `device_id`;
- the Mac reports one bonded BLE peripheral for HID and GATT;
- Wi-Fi/WSS interruption does not clear HID/GATT;
- expected and observed SPKI match;
- TLS exporter auth succeeds on the current connection and prior-connection signature fails.

The resource measurement group has no consistency error only when:

- steady minimum internal heap is at least 65,536 bytes;
- steady minimum largest internal block is at least 32,768 bytes;
- transient/attack minimum internal heap is at least 40,960 bytes;
- the exact five-second transient burst records all 100 16KiB WSS frames, the 128KiB streaming import, the 20-session page and all four fragments/64KiB of approval detail in one overlapping window;
- allocation failures and metrics encode failures are zero;
- all seven task stack minima meet `max(20%, 1024 bytes)`;
- HTTPS occupancy reaches exactly `established=4, pending=1`;
- every attack reaches its requested rate;
- HID has at least 10,000 generated, no queue loss/overflow/stuck modifier, p95 at most 20,000µs and a final release-all observation.

The Web-security measurement group has no consistency error only when every row in the real-Web matrix matches. Companion GATT replay evidence is attached for boundary checking, names the foundation fixture SHA-256, and remains a raw Companion-owned measurement.

Any threshold breach is appended to `consistency_errors`. Missing equipment, source addresses, permissions, pairing action, Companion report or duration is appended to `blockers` and sets `capture_complete=false`. Neither condition creates a child verdict; foundation adapters normalize the report and foundation evaluators decide `P0-G1`, `P0-G5`, `P0-G6` and final `GO/NO_GO`.

- [ ] **Step 8: Hash raw evidence and validate the final report.**

For every raw file, store relative path, byte length, SHA-256, first runner receipt time and last runner receipt time. Redaction scans reject an eight-digit pairing code, `cp_admin=`, `X-CSRF-Token`, PEM private key, Wi-Fi credential marker, exporter bytes, request body and text payload before generating Markdown.

Run unit verification:

```bash
uv run pytest tools/phase0/tests/test_run_concurrency_hil.py tools/phase0/tests/test_concurrency_report.py -q
```

Expected: all same-run, overlap, threshold, producer-boundary, artifact-hash and redaction tests pass.

- [ ] **Step 9: Commit the validated HIL harness before any live run.**

```bash
git add scripts/phase0/run_concurrency_hil.py tools/phase0/validate_concurrency_report.py tools/phase0/tests/test_run_concurrency_hil.py tools/phase0/tests/test_concurrency_report.py protocol/phase0/firmware-concurrency-report.schema.json
git commit -m "test: add firmware concurrency hil harness"
```

Do not generate live evidence from an uncommitted runner. After the macOS and release-security harness commits also exist, require a clean tree and record this shared HEAD as `HIL_BASE_COMMIT`.

- [ ] **Step 10: Execute the target HIL only after the hardware checkpoint.**

Run:

```bash
uv run python scripts/phase0/run_concurrency_hil.py \
  --auto-port \
  --firmware-bin firmware/build/cardputer_codex_phase0.bin \
  --hardware-manifest build/phase0/hardware-manifest.json \
  --companion-probe companion/.build/release/cardputer-phase0-probe \
  --companion-interface "$COMPANION_INTERFACE" \
  --companion-address "$COMPANION_ADDRESS" \
  --companion-netmask "$COMPANION_NETMASK" \
  --companion-peripheral-id "$COMPANION_PERIPHERAL_ID" \
  --companion-device-id-hex "$COMPANION_DEVICE_ID_HEX" \
  --companion-gatt-secret-file "$COMPANION_GATT_SECRET_FILE" \
  --companion-tls-identity-label "$COMPANION_TLS_IDENTITY_LABEL" \
  --attacker-interface auto \
  --duration-seconds 1800 \
  --output build/phase0/firmware-concurrency
uv run python tools/phase0/validate_concurrency_report.py \
  build/phase0/firmware-concurrency/report.json
```

Expected on a fully provisioned bench: validator prints `capture_complete=true`, measurement counts and evidence SHA-256 values, with no secrets or gate verdict. If the bench lacks any prerequisite, it writes a schema-valid report with `capture_complete=false` and named blockers, then exits non-zero.

- [ ] **Step 11: Record the redacted result and commit only the summary.**

`docs/validation/phase0/firmware-concurrency-hil.md` records test date, Git commit, sanitized hardware manifest digest, firmware image and app ELF digests, 30-minute window, capture completeness, threshold extrema, HID sample/p95 values, measurement counts, artifact digests, consistency errors and blockers. It does not contain a gate verdict, raw logs or credentials.

```bash
git add docs/validation/phase0/firmware-concurrency-hil.md
git commit -m "docs: record firmware concurrency hil evidence"
```

## Exit Conditions

- This subplan exits successfully only with one schema-valid, `capture_complete=true`, blocker-free, consistency-error-free measurement report; that condition is not itself a gate verdict.
- Foundation adapters consume the raw report, reject any child verdict field, independently recompute `P0-G1-CONCURRENCY`, `P0-G5-RESOURCE` and the relevant `P0-G6-SECURITY` inputs, then the foundation finalizer alone decides `GO/NO_GO`.
- Gate 6 remains incomplete until the parent plan combines these Web measurements with SAS, Companion-owned GATT replay, Secure Boot, Flash/NVS Encryption and signed USB recovery measurements.
- A public ESP-IDF API blocker, NimBLE coexistence failure, inability to prove HID/GATT identity, inability to hold 4 established + 1 pending, missing TLS exporter, any resource threshold breach or HID p95 failure returns the project to design review.
- A capture with blockers may be rerun after the named bench prerequisite is supplied, but evidence from that incomplete run cannot be merged into a later run.
