# Cardputer Codex Companion Phase 0 Feasibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. This file is an index; execute one linked subplan task-by-task and treat the later trace sections as non-executable.

**Goal:** 在编写产品功能以前，以可重复的自动化证据和目标硬件实测关闭正式设计的六项 Phase 0 硬门槛，并只在六项均为 `PASS` 时给出 `GO`。

**Architecture:** Phase 0 由相互隔离但共享协议 fixture 的四类探针组成：ESP32-S3/Cardputer 固件探针、原生 Swift macOS 探针、代表性 Vue/Vite Web 与 CJK 资源探针、只读 Codex App Server 契约探针。原始日志写入被 Git 忽略的 `build/phase0/`，经脱敏、哈希和 schema 校验后汇总为六个机器可读 gate；mock、宿主机测试或缺失证据不能代替真机结果。

**Tech Stack:** ESP-IDF `v5.5.4`（commit `735507283d5b2f9fb363a1901172dbd9e847945d`）、C++20、FreeRTOS、NimBLE、mbedTLS、ESP HTTPS Server、`esp_websocket_client` `1.7.0`、M5Unified `0.2.17`；Swift 6、SwiftPM、AppKit、CoreBluetooth、Network、ApplicationServices、Carbon、SQLite3；Node.js `22.14.0`、Vue `3.5.39`、TypeScript `5.9.3`、Vite `6.4.3`；Python `3.11.11`、uv、标准库 `unittest` 与锁定的字体/密码学工具依赖。

## Global Constraints

- Source of truth: [`2026-07-24-cardputer-codex-companion-design.md`](../specs/2026-07-24-cardputer-codex-companion-design.md)。
- Scope boundary: 本计划只实施 Phase 0。Phase 1–5 的实现计划只有在本阶段得到 `GO` 后才编写；任一硬门槛失败都回到设计复核。
- Gate 状态只允许 `PASS`、`FAIL`、`BLOCKED`、`NOT_RUN`。最终判定只有六项全部 `PASS` 才是 `GO`，其他组合一律是 `NO_GO`。
- 子计划原始报告可使用操作级 outcome（例如 Unicode `partial`），但 foundation adapter 必须删除任何 child verdict/overall status，并从原始事实生成无状态 measurement；只有最终 evaluator 能产生上述大写 Gate 状态。
- 固件 mock、虚拟 eFuse、成功编译、API 无报错、截图、同名 BLE 设备或人工口头确认都不能单独产生 `PASS`。
- 第一次写入普通 Cardputer 前，必须解析唯一串口并读取完整 8MiB Flash 备份，记录 SHA-256。端口不唯一或设备容量不是 8MiB 时停止。
- Secure Boot、Flash Encryption Release 和相关 eFuse 是不可逆步骤，只能在用户明确指定的专用安全测试机上执行。执行时再次核对精确 chip ID，并另行获得用户对永久 eFuse 改动的批准；普通开发机永不自动烧写这些 eFuse。
- Phase 0 使用的测试签名私钥只生成在 `build/phase0/keys/`，不得提交、打印或复用于发布。真实发布私钥不进入本仓库、日志或命令行。
- Wi-Fi 密码通过交互式隐藏输入写入探针 NVS，不进入 `sdkconfig`、shell history、报告或 Git。
- 所有现场证据必须记录 Git commit、工具版本、固件 SHA-256、目标硬件版本、开始/结束时间和原始证据 SHA-256；设备 MAC、chip ID、用户名、路径和 Codex 内容在进入仓库前脱敏。
- 当前仓库没有 remote。每项任务只做本地提交，不创建远端、不推送。
- 当前已知预检不是 gate 结论：本机尚无 ESP-IDF/Ninja、没有有效 codesigning identity、没有完整 Xcode、未安装 VS Code、当前没有 Cardputer USB 设备。相关 HIL 在条件补齐前必须保持 `BLOCKED`。

---

## Executable Subplans

This file is the Phase 0 dependency map and gate index. Implementers execute only the reviewer-sized subplans below. This file is normative for global constraints, dependency order and final gate membership; each subplan is normative for its exact file paths, interfaces, tests and commits. The later 13 trace sections are non-executable design history and cannot override a subplan or be run as a second implementation plan.

1. [`2026-07-24-phase0-foundation-codex-evidence.md`](2026-07-24-phase0-foundation-codex-evidence.md) — toolchain, protocol vectors, App Server audit and independently recomputed evidence.
2. [`2026-07-24-phase0-firmware-concurrency.md`](2026-07-24-phase0-firmware-concurrency.md) — Cardputer HID/GATT/Wi-Fi/HTTPS/WSS, Web admission, resources and concurrency HIL.
3. [`2026-07-24-phase0-macos-unicode-ble.md`](2026-07-24-phase0-macos-unicode-ble.md) — Unicode injection, TCC, CoreBluetooth receive/replay, identity and macOS HIL.
4. [`2026-07-24-phase0-web-flash-release-security.md`](2026-07-24-phase0-web-flash-release-security.md) — representative Web/CJK assets, fixed flash budget and release/recovery security HIL.

Execution order is foundation Tasks 1–2 first. Then macOS Tasks 1–6, firmware Tasks 1–7, Web Task 1 and the Codex audit may proceed in parallel; macOS Task 7 starts only after firmware Task 2 has committed `companion-probe-event.schema.json`. To avoid concurrent edits to `firmware/main/CMakeLists.txt`, `app_main.cpp` and host-test CMake, execute Web/Release Tasks 2–5 only after firmware Task 7 is committed. Next implement, test and commit all three HIL harnesses/schemas without running them. Require a clean tree and freeze that commit as `HIL_BASE_COMMIT`; every child report and release build records this same commit, and no source/docs change is allowed between the three live runs. Regenerate the read-only Codex capability report from the frozen tree, then execute macOS Task 8, firmware Task 8 and Web/Release Task 6 at their explicit hardware/approval checkpoints. Sanitized summaries are committed only after all raw evidence is complete. The foundation finalizer runs last.

### HIL Freeze Contract

Immediately before the first live run, execute:

```bash
test -z "$(git status --porcelain)"
export HIL_BASE_COMMIT="$(git rev-parse HEAD)"
export HIL_TOOLCHAIN_SHA256="$(
  shasum -a 256 build/phase0/toolchain.json | awk '{print $1}'
)"
test -n "$HIL_BASE_COMMIT"
test "${#HIL_TOOLCHAIN_SHA256}" -eq 64
scripts/phase0/generate_app_server_schema.sh build/phase0/app-server
scripts/phase0/run_app_server_capability_probe.sh \
  --schema-root build/phase0/app-server \
  --requirements protocol/phase0/codex-capability-requirements.json \
  --output build/phase0/app-server/capability.json
```

Before and after each live runner, require both:

```bash
test -z "$(git status --porcelain)"
test "$(git rev-parse HEAD)" = "$HIL_BASE_COMMIT"
```

The finalizer consumes exactly five child reports covering six gates: Codex capability at `build/phase0/app-server/capability.json`, macOS HIL at `build/phase0/macos-hil/<run-id>/raw.json`, firmware concurrency at `build/phase0/firmware-concurrency/report.json`, release budget at `build/phase0/release-image-budget.json` and release security at `build/phase0/security-hil/raw.json`. Regenerate the read-only Codex capability report after freezing the tree; all five must contain `git_commit=$HIL_BASE_COMMIT` and `toolchain_manifest_sha256=$HIL_TOOLCHAIN_SHA256`, while the four hardware-derived records additionally require `git_tree_clean=true`. Firmware/macOS must also agree on development `probe_firmware_sha256`, runtime `app_elf_sha256` and device-ID digest; release budget/security must agree on the distinct `release_firmware_sha256`. The finalizer rejects any mismatch, dirty-tree marker, missing raw artifact hash or non-overlapping same-window Companion event. No implementation, schema, test, documentation or generated source may change until all five child reports exist and validate. Only then may sanitized summaries be committed; those later summary commits are deliberately not the tested commit.

## Gate Mapping

| Gate ID | 正式门槛 | 规范子计划 | 必需现场证据 |
|---|---|---|---|
| `P0-G1-CONCURRENCY` | BLE HID、加密 GATT、Wi-Fi、HTTPS、WSS 共存；macOS 证明 HID/GATT 属于同一设备 | firmware Tasks 2–8 + macOS Tasks 4–5、8 | 目标 Cardputer + macOS BLE/HID identity 报告 |
| `P0-G2-UNICODE` | 五类应用中文注入、权限、Secure Input、焦点、部分完成、崩溃与 TCC 升级 | macOS Tasks 1–2、6–8 | 已签名 `.app`、五应用逐项读回结果 |
| `P0-G3-CODEX` | 当前 Codex App Server 的 thread/turn/compact/Approval/Input 完整能力 | foundation Task 3 | schema hash、脱敏只读握手和字段级矩阵 |
| `P0-G4-FLASH` | 固定 8MiB 分区、代表性 Web/CJK、双 OTA 及安全余量 | Web/Release Tasks 1–4、6 | 最终签名 app 镜像大小和分区解析结果 |
| `P0-G5-RESOURCE` | heap、largest block、TLS 瞬态、task stack、攻击负载下 HID p95 | firmware Tasks 4–5、7–8 | 目标硬件指标流、攻击端负载报告 |
| `P0-G6-SECURITY` | SAS、WSS channel binding、GATT replay、Web 限流、Secure Boot/加密/签名恢复 | foundation Task 2 + firmware Tasks 4–6、8 + macOS Tasks 3–5、8 + Web/Release Tasks 4–6 | 跨语言向量、负面测试、专用安全测试机报告 |

## Non-Normative Consolidated File Map

This tree is only an ownership overview. The exact, executable file maps are the four subplans above; do not create a path from this overview when the owning subplan names a different path.

```text
.
├── .gitignore
├── .nvmrc
├── .python-version
├── pyproject.toml
├── uv.lock
├── toolchain.lock.json
├── assets/cjk/
│   ├── LICENSE-OFL-1.1.txt
│   ├── NOTICE.md
│   ├── noto-sans-cjk-sc.lock.json
│   └── ui_strings_zh-Hans.txt
├── protocol/phase0/
│   ├── README.md
│   ├── gates.json
│   ├── producer-map.json
│   ├── phase0-report.schema.json
│   ├── codex-capability.schema.json
│   ├── pairing-v1.md
│   ├── gatt-auth-v1.md
│   ├── wss-auth-v1.md
│   ├── gatt_probe_contract.json
│   ├── release-trust-v1.md
│   ├── codex-capability-requirements.json
│   ├── concurrency-evidence.schema.json
│   └── fixtures/
│       ├── pairing-v1.json
│       ├── gatt-auth-v1.json
│       ├── wss-auth-v1.json
│       └── text-operation-v1.json
├── firmware/
│   ├── CMakeLists.txt
│   ├── partitions.csv
│   ├── sdkconfig.defaults
│   ├── sdkconfig.release-probe.defaults
│   ├── main/
│   │   ├── CMakeLists.txt
│   │   ├── idf_component.yml
│   │   ├── app_main.cpp
│   │   └── probe/
│   │       ├── cardputer_hal.{hpp,cpp}
│   │       ├── hid_engine.{hpp,cpp}
│   │       ├── ble_services.{hpp,cpp}
│   │       ├── network_services.{hpp,cpp}
│   │       ├── bounded_https_server.{h,c}
│   │       ├── pinned_wss_transport.{h,c}
│   │       ├── web_security.{hpp,cpp}
│   │       ├── security_probe.{hpp,cpp}
│   │       ├── recovery_policy.{hpp,cpp}
│   │       ├── usb_recovery.{hpp,cpp}
│   │       ├── resource_metrics.{hpp,cpp}
│   │       └── probe_controller.{hpp,cpp}
│   └── test/host/
│       ├── CMakeLists.txt
│       ├── test_probe_controller.cpp
│       ├── test_accept_limiter.cpp
│       ├── test_web_security.cpp
│       ├── test_hid_engine.cpp
│       ├── test_gatt_frame.cpp
│       ├── test_gatt_sender.cpp
│       ├── test_cjk_font_pack.cpp
│       └── test_resource_metrics.cpp
├── companion/
│   ├── Package.swift
│   ├── ProbeApp/Info.plist
│   ├── Sources/
│   │   ├── CardputerProbeCore/
│   │   │   ├── InjectionModels.swift
│   │   │   ├── UnicodeChunker.swift
│   │   │   ├── UnicodeInjector.swift
│   │   │   ├── InjectionLedger.swift
│   │   │   ├── SecurityProtocol.swift
│   │   │   ├── GATTAuthenticatedReceiver.swift
│   │   │   └── BLEIdentityProbe.swift
│   │   ├── CardputerProbeMac/
│   │   │   ├── AXFocusChecker.swift
│   │   │   ├── CGEventUnicodePoster.swift
│   │   │   ├── SecureInputChecker.swift
│   │   │   ├── SQLiteInjectionLedger.swift
│   │   │   ├── CoreBluetoothCentral.swift
│   │   │   ├── IOHIDInventory.swift
│   │   │   └── WebSocketProbeServer.swift
│   │   ├── cardputer-companion-probe/main.swift
│   │   ├── focus-change-fixture/main.swift
│   │   └── secure-input-fixture/main.swift
│   └── Tests/CardputerProbeCoreTests/
│       ├── UnicodeChunkerTests.swift
│       ├── UnicodeInjectorTests.swift
│       ├── InjectionRecoveryTests.swift
│       ├── SecurityProtocolTests.swift
│       ├── GATTAuthenticatedReceiverTests.swift
│       ├── BLEIdentityProbeTests.swift
│       └── WebSocketProbeServerTests.swift
├── web-probe/
│   ├── package.json
│   ├── package-lock.json
│   ├── vite.config.ts
│   ├── src/
│   │   ├── App.vue
│   │   └── pages/
│   │       ├── StatusProbe.vue
│   │       ├── ProfilesProbe.vue
│   │       ├── KeymapProbe.vue
│   │       ├── MacrosProbe.vue
│   │       ├── PairingProbe.vue
│   │       └── ImportExportProbe.vue
│   └── tests/bundle.test.ts
├── tools/
│   ├── app_server_probe/
│   │   ├── audit.py
│   │   ├── live_probe.py
│   │   └── tests/
│   ├── cjk/
│   │   ├── build_cjk_pack.py
│   │   └── tests/test_cjk_pack.py
│   ├── phase0/
│   │   ├── image_budget.py
│   │   ├── ota_manifest.py
│   │   ├── adapters.py
│   │   ├── generate_security_vectors.py
│   │   ├── evaluate_metrics.py
│   │   ├── evidence.py
│   │   ├── run_phase0.py
│   │   ├── run_concurrency_hil.py
│   │   ├── verify_release_config.py
│   │   ├── stress_device.py
│   │   └── tests/
│   └── recovery/
│       ├── verify_bundle.py
│       ├── flash_bundle.py
│       └── tests/test_recovery_bundle.py
├── scripts/phase0/
│   ├── bootstrap_toolchain.sh
│   ├── idf.sh
│   ├── node.sh
│   ├── npm.sh
│   ├── check_toolchain.py
│   ├── backup_flash.sh
│   ├── provision_probe.py
│   ├── build_companion_probe_app.sh
│   ├── run_macos_unicode_hil.sh
│   ├── generate_app_server_schema.sh
│   ├── run_app_server_capability_probe.sh
│   ├── run_security_hil.py
│   └── run_phase0.sh
├── tests/phase0/
│   ├── test_toolchain.py
│   ├── test_gate_report.py
│   ├── test_no_clipboard_fallback.py
│   ├── test_finalize_report.py
│   └── test_protocol_fixtures.py
└── docs/validation/phase0/
    ├── README.md
    ├── gate-matrix.json
    ├── hardware-manifest.md
    ├── flash-budget.md
    ├── macos-unicode-matrix.md
    ├── macos-ble-binding.md
    ├── app-server-capability-matrix.md
    ├── security-hil.md
    └── go-no-go.md
```

## Non-Executable Requirements Trace

The following traces preserve the rationale and acceptance clauses used to split the work. They are not implementation tasks: do not execute their commands, create their listed files or make their suggested commits. Use the corresponding executable subplan instead.

### Trace 1: Reproducible Toolchain and Probe Boundaries

**Files:**

- Modify: `.gitignore`
- Create: `.python-version`, `.nvmrc`, `pyproject.toml`, `toolchain.lock.json`
- Create: `scripts/__init__.py`, `scripts/phase0/__init__.py`
- Create: `scripts/phase0/bootstrap_toolchain.sh`, `scripts/phase0/idf.sh`, `scripts/phase0/node.sh`, `scripts/phase0/npm.sh`, `scripts/phase0/check_toolchain.py`
- Create: `tests/phase0/test_toolchain.py`

**Interfaces:**

- Consumes: arm64 macOS host, `git`, `curl`, `shasum`, `uv`, installed Swift and current `codex`.
- Produces: repo-local ESP-IDF/Node/Python tools under `.tools/` and a JSON toolchain manifest; it never changes a global runtime.

- [ ] **Step 1: Write the failing toolchain-lock test.**

```python
from pathlib import Path
import json
import unittest

from scripts.phase0.check_toolchain import validate_lock


class ToolchainLockTest(unittest.TestCase):
    def test_phase0_versions_are_immutable(self) -> None:
        lock = json.loads(Path("toolchain.lock.json").read_text())
        self.assertEqual([], validate_lock(lock))
        self.assertEqual("v5.5.4", lock["esp_idf"]["tag"])
        self.assertEqual(
            "735507283d5b2f9fb363a1901172dbd9e847945d",
            lock["esp_idf"]["commit"],
        )
        self.assertEqual("22.14.0", lock["node"]["version"])
        self.assertEqual("3.11.11", lock["python"]["version"])
```

- [ ] **Step 2: Run the test and observe the intended failure.**

Run:

```bash
python3 -m unittest discover -s tests/phase0 -p 'test_toolchain.py' -v
```

Expected: `ModuleNotFoundError: No module named 'scripts.phase0'` or missing lock file.

- [ ] **Step 3: Add the immutable lock and repo-local wrappers.**

`toolchain.lock.json` must pin:

```json
{
  "esp_idf": {
    "tag": "v5.5.4",
    "commit": "735507283d5b2f9fb363a1901172dbd9e847945d"
  },
  "node": {
    "version": "22.14.0",
    "archive": "node-v22.14.0-darwin-arm64.tar.gz",
    "sha256": "e9404633bc02a5162c5c573b1e2490f5fb44648345d64a958b17e325729a5e42"
  },
  "python": {"version": "3.11.11"},
  "components": {
    "espressif/esp_websocket_client": "1.7.0",
    "m5stack/m5unified": "0.2.17"
  }
}
```

`bootstrap_toolchain.sh` must clone ESP-IDF into `.tools/esp-idf`, verify `HEAD`, set `IDF_TOOLS_PATH=.tools/espressif`, use uv-managed Python 3.11.11, and install only the `esp32s3` target. It must download Node into `.tools/` and verify the archive before extraction. `idf.sh`, `node.sh`, and `npm.sh` must resolve paths relative to the repository, not `$HOME`.

- [ ] **Step 4: Lock Python tooling.**

Use this dependency boundary in `pyproject.toml`, then generate `uv.lock`:

```toml
[project]
name = "cardputer-codex-phase0-tools"
version = "0.0.1"
requires-python = "==3.11.*"
dependencies = [
  "brotli==1.1.0",
  "cryptography==45.0.6",
  "fonttools[woff]==4.59.0",
  "jsonschema[format-nongpl]==4.26.0",
  "pillow==11.3.0",
  "pyserial==3.5",
]

[dependency-groups]
dev = [
  "check-jsonschema==0.37.4",
  "pytest==8.4.1",
]
```

Run:

```bash
uv lock
scripts/phase0/bootstrap_toolchain.sh
scripts/phase0/check_toolchain.py --json build/phase0/toolchain.json
```

Expected: ESP-IDF reports `v5.5.4`, Python `3.11.11`, Node `v22.14.0`; installed Swift, macOS SDK, codesigning identity count, application availability and current Codex version are captured without secrets. A missing optional HIL prerequisite is reported as `BLOCKED`, not as bootstrap failure.

- [ ] **Step 5: Make generated state uncommittable.**

Add `.tools/`, `build/`, `companion/.build/`, `.swiftpm/`, `web-probe/node_modules/`, generated app bundles, raw SQLite files and test keys to `.gitignore`. Keep source fixtures, lock files and sanitized reports trackable.

- [ ] **Step 6: Re-run the tests.**

Run:

```bash
python3 -m unittest discover -s tests/phase0 -p 'test_toolchain.py' -v
git check-ignore .tools/esp-idf build/phase0/raw.log build/phase0/keys/test.pem
```

Expected: tests pass and all three generated paths are ignored.

- [ ] **Step 7: Commit the task.**

```bash
git add .gitignore .nvmrc .python-version pyproject.toml uv.lock toolchain.lock.json scripts tests/phase0/test_toolchain.py
git commit -m "chore: bootstrap phase zero toolchain"
```

### Trace 2: Gate Contract, Evidence Schema, and Fail-Closed Aggregation

**Files:**

- Create: `protocol/phase0/README.md`
- Create: `protocol/phase0/gates.json`
- Create: `protocol/phase0/phase0-report.schema.json`
- Create: `tools/phase0/evidence.py`
- Create: `tools/phase0/tests/test_evidence.py`
- Create: `tests/phase0/test_gate_report.py`
- Create: `docs/validation/phase0/README.md`
- Create: `docs/validation/phase0/gate-matrix.json`

**Interfaces:**

- Consumes: sanitized per-probe measurements and case results with SHA-256 references to raw evidence.
- Produces: independently recalculated gate statuses, a schema-valid `Phase0Report` and exit code `0` only for six `PASS` gates. Probe-supplied status strings are rejected.

- [ ] **Step 1: Write the failing fail-closed tests.**

```python
import unittest

from tools.phase0.evidence import decide, evaluate_gate5, EvidenceError


REQUIRED = {
    "P0-G1-CONCURRENCY",
    "P0-G2-UNICODE",
    "P0-G3-CODEX",
    "P0-G4-FLASH",
    "P0-G5-RESOURCE",
    "P0-G6-SECURITY",
}


class DecisionTest(unittest.TestCase):
    def test_only_six_passes_are_go(self) -> None:
        self.assertEqual("GO", decide({gate: "PASS" for gate in REQUIRED}))

    def test_missing_blocked_or_not_run_is_no_go(self) -> None:
        self.assertEqual("NO_GO", decide({}))
        for state in ("FAIL", "BLOCKED", "NOT_RUN"):
            statuses = {gate: "PASS" for gate in REQUIRED}
            statuses["P0-G6-SECURITY"] = state
            self.assertEqual("NO_GO", decide(statuses))

    def test_probe_cannot_self_declare_pass(self) -> None:
        measurement = {
            "reported_status": "PASS",
            "generated": 10_000,
            "queued": 9_999,
            "queue_failures": 1,
        }
        with self.assertRaises(EvidenceError):
            evaluate_gate5(measurement)
```

- [ ] **Step 2: Run the tests and confirm import failure.**

```bash
uv run python -m unittest discover -s tools/phase0/tests -p 'test_evidence.py' -v
```

Expected: import fails because `tools.phase0.evidence` does not exist.

- [ ] **Step 3: Define the exact report contract.**

Each finalized output gate entry must contain:

```json
{
  "gate_id": "P0-G1-CONCURRENCY",
  "status": "BLOCKED",
  "summary": "Target hardware is not connected",
  "checks": [],
  "evidence": [
    {
      "kind": "raw_manifest",
      "path": "build/phase0/firmware-concurrency/report.json",
      "sha256": "64-lowercase-hex-characters"
    }
  ]
}
```

Measurement inputs do not contain `status`. The finalized top level must include schema version, Git commit, clean/dirty state, UTC timestamps, toolchain manifest hash, hardware model/revision, firmware hash, six unique gate IDs and `decision`. `gates.json` fixes every required check ID, evidence kind and threshold. `evidence.py` loads referenced measurements, verifies raw hashes, calls a gate-specific evaluator and writes each status itself. It must reject probe-supplied status fields, unknown state strings, duplicate or missing checks, absent hashes, timestamps in reverse order, mismatched source/firmware hashes, host-only evidence for a HIL check, and a claimed `GO` that does not recompute to `GO`.

- [ ] **Step 4: Implement and verify.**

```python
REQUIRED_GATES = frozenset({
    "P0-G1-CONCURRENCY",
    "P0-G2-UNICODE",
    "P0-G3-CODEX",
    "P0-G4-FLASH",
    "P0-G5-RESOURCE",
    "P0-G6-SECURITY",
})

def decide(statuses: dict[str, str]) -> str:
    if set(statuses) != REQUIRED_GATES:
        return "NO_GO"
    return "GO" if all(value == "PASS" for value in statuses.values()) else "NO_GO"
```

Implement six explicit evaluators:

- Gate 1 recomputes simultaneous five-service activity, physical HID/GATT identity, bond access and reconnect from one time window.
- Gate 2 recomputes every application/case count and source/readback hash equality.
- Gate 3 recomputes every semantic App Server method, field and stability rule.
- Gate 4 parses the committed partition table and final signed image length.
- Gate 5 recomputes nearest-rank p95, sample counts, heap/largest-block/allocation and every stack threshold.
- Gate 6 recomputes the complete positive/negative security case set and rejects virtual-only eFuse evidence.

Run:

```bash
uv run python -m unittest discover -s tools/phase0/tests -v
uv run python -m unittest discover -s tests/phase0 -p 'test_gate_report.py' -v
```

Expected: all tests pass; a fixture with one `NOT_RUN` exits non-zero.

- [ ] **Step 5: Commit the task.**

```bash
git add protocol/phase0 tools/phase0 tests/phase0/test_gate_report.py docs/validation/phase0
git commit -m "test: define phase zero evidence gates"
```

### Trace 3: Pairing, GATT, and WSS Protocol Drafts with Cross-Language Vectors

**Files:**

- Create: `protocol/phase0/pairing-v1.md`
- Create: `protocol/phase0/gatt-auth-v1.md`
- Create: `protocol/phase0/wss-auth-v1.md`
- Create: `protocol/phase0/fixtures/pairing-v1.json`
- Create: `protocol/phase0/fixtures/gatt-auth-v1.json`
- Create: `protocol/phase0/fixtures/wss-auth-v1.json`
- Create: `tools/phase0/generate_security_vectors.py`
- Create: `tests/phase0/test_protocol_fixtures.py`

**Interfaces:**

- Consumes: fixed P-256 test scalars, nonces, IDs, TLS exporter bytes and fragment bytes.
- Produces: canonical byte encodings and deterministic results independently consumed by Python, CryptoKit and mbedTLS.

- [ ] **Step 1: Write failing canonicalization tests.**

```python
from hashlib import sha256
import json
from pathlib import Path
import unittest


class ProtocolFixtureTest(unittest.TestCase):
    def test_transcript_hash_matches_bytes(self) -> None:
        fixture = json.loads(
            Path("protocol/phase0/fixtures/pairing-v1.json").read_text()
        )
        encoded = bytes.fromhex(fixture["transcript_hex"])
        self.assertEqual(fixture["transcript_sha256"], sha256(encoded).hexdigest())
        self.assertNotEqual(fixture["pairing_root_hex"], fixture["gatt_auth_hex"])
```

- [ ] **Step 2: Run and observe the missing-fixture failure.**

```bash
uv run python -m unittest tests/phase0/test_protocol_fixtures.py -v
```

Expected: `FileNotFoundError` for `pairing-v1.json`.

- [ ] **Step 3: Fix the protocol bytes before generating vectors.**

`pairing-v1.md` must specify:

- prefix `CCP-PAIR` plus `0x0001`;
- role-ordered, unsigned 16-bit big-endian length-prefixed device ID, Companion instance ID and protocol version;
- device then Companion long-term P-256 SEC1 uncompressed 65-byte public keys;
- device then Companion ephemeral P-256 SEC1 uncompressed 65-byte public keys;
- device then Companion 32-byte nonces;
- SHA-256 transcript hash used as HKDF-SHA256 salt;
- distinct labels `cardputer-codex/pair-root/v1`, `cardputer-codex/gatt-auth/v1`, `cardputer-codex/sas/v1`;
- six-digit SAS from rejection-sampled 32-bit words, rendered with leading zeroes.
- after both SAS confirmations, a fresh 32-byte bind challenge must arrive over the authenticated WSS connection and the bonded encrypted GATT connection; only an exact match associates that BLE bond with the Companion identity, and either missing channel aborts the bind.

`gatt-auth-v1.md` must fix the authenticated frame fields and byte order: version, flags, 128-bit `connection_id`, 128-bit `operation_id`, 64-bit counter, 16-bit fragment index/count, 32-bit total UTF-8 length, full-message SHA-256, 16-bit fragment length, fragment bytes, and a 16-byte truncated HMAC-SHA256 tag. The receiver verifies MAC before touching a 32-entry replay window; duplicate, rollback, jump beyond 32, counter wrap and stale connection ID are rejected.

`wss-auth-v1.md` must pin the TLS certificate SPKI SHA-256, derive a 32-byte exporter with label `EXPORTER-Cardputer-Codex-Companion-v1` and explicitly empty exporter context, and sign the canonical length-prefixed tuple `exporter + Companion instance ID + device ID + protocol version + 32-byte random challenge` using the device long-term P-256 key. ECDSA is encoded as fixed-width 64-byte `r || s`, not DER.

- [ ] **Step 4: Generate deterministic fixtures and verify independent properties.**

Run:

```bash
uv run tools/phase0/generate_security_vectors.py --write protocol/phase0/fixtures
uv run tools/phase0/generate_security_vectors.py \
  --emit-transcript build/phase0/transcript.bin
uv run python -m unittest tests/phase0/test_protocol_fixtures.py -v
openssl dgst -sha256 build/phase0/transcript.bin
```

Expected: Python tests pass and OpenSSL prints the same transcript digest recorded by the fixture generator.

- [ ] **Step 5: Add negative vectors.**

Generate cases for changed role, long-term identity, ephemeral identity, nonce, protocol version, TLS exporter, SPKI, GATT tag, counter, connection ID and operation payload hash. Every mutation must change the derived result or return the specified stable rejection.

- [ ] **Step 6: Commit the task.**

```bash
git add protocol/phase0 tools/phase0/generate_security_vectors.py tests/phase0/test_protocol_fixtures.py
git commit -m "docs: fix phase zero security protocols"
```

### Trace 4: Cardputer Firmware Skeleton and Concurrent Service Bring-Up

**Files:**

- Create: `firmware/CMakeLists.txt`
- Create: `firmware/sdkconfig.defaults`
- Create: `firmware/main/CMakeLists.txt`
- Create: `firmware/main/idf_component.yml`
- Create: `firmware/main/app_main.cpp`
- Create: `firmware/main/probe/cardputer_hal.hpp`, `firmware/main/probe/cardputer_hal.cpp`
- Create: `firmware/main/probe/ble_services.hpp`, `firmware/main/probe/ble_services.cpp`
- Create: `firmware/main/probe/network_services.hpp`, `firmware/main/probe/network_services.cpp`
- Create: `firmware/main/probe/bounded_https_server.h`, `firmware/main/probe/bounded_https_server.c`
- Create: `firmware/main/probe/pinned_wss_transport.h`, `firmware/main/probe/pinned_wss_transport.c`
- Create: `firmware/main/probe/probe_controller.hpp`, `firmware/main/probe/probe_controller.cpp`
- Create: `firmware/test/host/CMakeLists.txt`
- Create: `firmware/test/host/test_accept_limiter.cpp`
- Create: `firmware/test/host/test_probe_controller.cpp`
- Create: `scripts/phase0/provision_probe.py`
- Create: `docs/validation/phase0/hardware-manifest.md`

**Interfaces:**

- Consumes: Cardputer matrix GPIOs, G0/Home button, interactive Wi-Fi credentials, Companion host/port/SPKI.
- Produces: one BLE identity exposing HID and protected custom GATT, Wi-Fi STA, HTTPS `/healthz`, authenticated WSS probe traffic, display/serial status and bounded service queues.

- [ ] **Step 1: Lock the exact target hardware before selecting a HAL.**

Record product/PCB revision, M5Stack model identifier, ESP32-S3 revision, detected 8MiB Flash ID/capacity and absence of PSRAM in `hardware-manifest.md`. The K132 pin candidate `{8,9,11}` outputs and `{13,15,3,4,5,6,7}` inputs is traced to M5Stack Cardputer `1.2.0`, commit `2d4fa6646e4e5b47e0af96214b003aa7b15b8d81`; it is not accepted until the target board scans every physical key correctly.

- [ ] **Step 2: Add failing controller and pre-TLS admission tests.**

Create a pure C++ host test that requires these states:

```cpp
enum class ProbeState {
  boot,
  ble_ready,
  wifi_ready,
  https_ready,
  wss_authenticated,
  live,
  failed,
};

struct ServiceSnapshot {
  bool ble_hid;
  bool encrypted_gatt;
  bool wifi;
  bool https;
  bool wss;
};
```

The test must prove `live` is impossible until all five booleans are true, and loss of Wi-Fi/WSS never clears `ble_hid`. `test_accept_limiter.cpp` must also prove a fixed 16-source table rejects the second simultaneous handshake and all over-rate sources before `tls_alloc_started` increments.

- [ ] **Step 3: Run the host build and observe missing sources.**

```bash
cmake -S firmware/test/host -B build/phase0/host-tests
cmake --build build/phase0/host-tests
ctest --test-dir build/phase0/host-tests --output-on-failure
```

Expected: configure or compile failure because the controller files do not exist.

- [ ] **Step 4: Complete two public-API blocker spikes before the rest of the firmware.**

Do not use stock `httpd_ssl_start()` as the final path: in ESP-IDF `v5.5.4` it can allocate/handshake before a user `open_fn` can enforce the required source/handshake limits. Build `bounded_https_server` over public `httpd_start()` session hooks: `open_fn` calls `getpeername()`, applies the fixed limiter, and only then creates `esp_tls`; accepted sessions install TLS send/receive/close overrides. Expose test counters `accepted_before_tls`, `rejected_before_tls` and `tls_alloc_started`.

Do not depend on `esp_websocket_client` internals for WSS proof. Build `pinned_wss_transport` as an external transport that owns `esp_tls_t`, verifies peer SPKI, enables `CONFIG_MBEDTLS_SSL_KEYING_MATERIAL_EXPORT`, obtains the public TLS context through `esp_tls_get_ssl_context()`, and wraps it with public WebSocket transport APIs before passing it as `ext_transport`. Any cast to a private `transport_esp_tls_t` or private mbedTLS field fails source review.

Compile both spikes against the pinned IDF/component versions:

```bash
scripts/phase0/idf.sh -C firmware build
```

Expected: the public API path compiles and a target self-test can derive exporter bytes and reject a connection before TLS allocation. If either public path is unavailable, stop Phase 0 and return to design before adding product-facing services.

- [ ] **Step 5: Add minimal firmware configuration.**

Pin `espressif/esp_websocket_client: "1.7.0"` and `m5stack/m5unified: "0.2.17"` in `idf_component.yml`; commit the generated `dependencies.lock`. Configure:

- ESP32-S3, 8MiB flash, no PSRAM;
- NimBLE BLE-only host, LE Secure Connections and bonding;
- one HID host bond;
- transport capacity for four established HTTPS sessions plus one pending handshake, a four-established semaphore and one handshake admission token;
- heap allocation-failure callback and FreeRTOS stack high-water collection;
- retained peer certificate and TLS keying-material exporter support;
- development build banner `PHASE 0 / NOT FOR RELEASE`.

The raw keyboard HAL may use the hardware-manifest pin candidate as a starting point, but must independently map every coordinate and HID usage. Do not copy the UserDemo's nonstandard Delete, Alt, punctuation or change-detection behavior.

- [ ] **Step 6: Bring up HID and custom GATT in one NimBLE database.**

Initialize the ESP HID profile without replacing its GATT registration callback, add the custom service with `ble_gatts_count_cfg()` / `ble_gatts_add_svcs()` after HID setup and before enabling NimBLE, and require encrypted, authenticated and bonded access flags plus an application-level current-Companion check. A text fragment is processed at the negotiated ATT MTU and never causes allocation of the full 1024-byte operation.

- [ ] **Step 7: Implement strict task isolation.**

Use bounded queues and priorities:

```cpp
struct StableKeyEvent {
  uint16_t physical_key;
  bool pressed;
  int64_t stable_at_us;
};

static constexpr size_t kHidQueueDepth = 32;
static constexpr size_t kNetworkQueueDepth = 16;
```

Keyboard scanning and HID queueing must not call Wi-Fi, TLS, display or NVS APIs. Network and screen tasks consume copies from lower-priority queues; queue-full behavior increments a metric instead of blocking the scanner. WSS uses a 4KiB working buffer and incrementally validates an allowed 16KiB frame instead of allocating 16KiB per event.

- [ ] **Step 8: Build the target image.**

```bash
scripts/phase0/idf.sh -C firmware set-target esp32s3
scripts/phase0/idf.sh -C firmware build
```

Expected: `firmware/build/cardputer_codex_phase0.bin` is produced for 8MiB/no-PSRAM configuration and the boot log includes the non-release banner.

- [ ] **Step 9: Provision without leaking credentials.**

`provision_probe.py` must use `getpass.getpass()`, redact serial echo and write Wi-Fi/Companion settings to probe NVS. Verify:

```bash
uv run scripts/phase0/provision_probe.py --self-test
uv run scripts/phase0/provision_probe.py \
  --verify-redaction build/phase0
```

Expected: self-test passes; the verifier reports counts and file names only and returns non-zero for any unredacted credential without printing its value.

- [ ] **Step 10: Commit the task.**

```bash
git add firmware scripts/phase0/provision_probe.py docs/validation/phase0/hardware-manifest.md
git commit -m "feat: add concurrent cardputer feasibility probe"
```

### Trace 5: HID Correctness, Device Identity, and Latency Accounting

**Files:**

- Create: `firmware/main/probe/hid_engine.hpp`, `firmware/main/probe/hid_engine.cpp`
- Create: `firmware/main/probe/resource_metrics.hpp`, `firmware/main/probe/resource_metrics.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Create: `firmware/test/host/test_hid_engine.cpp`
- Create: `firmware/test/host/test_resource_metrics.cpp`
- Create: `protocol/phase0/gatt_probe_contract.json`

**Interfaces:**

- Consumes: `StableKeyEvent` and a monotonic microsecond clock.
- Produces: standard 8-byte boot-keyboard reports, mandatory `release all`, immutable raw 16-byte `device_id` encoded as no-padding base32 in HID serial and returned raw by protected GATT identity, and complete latency histograms.

- [ ] **Step 1: Write failing HID report tests.**

```cpp
int main() {
  HidEngine hid;
  const auto chord = hid.press({0x08, {0x06, 0x4c}});
  assert(chord.error == HidError::none);
  assert(chord.report == (HidReport{0x08, 0x00, {0x06, 0x4c, 0, 0, 0, 0}}));
  assert(hid.release_all() == HidReport{});

  const auto overflow = hid.press({0x00, {4, 5, 6, 7, 8, 9, 10}});
  assert(overflow.error == HidError::too_many_keys);
  return 0;
}
```

The expected Delete usage is `0x4c`; modifier bits remain separate from the six regular keys.
Tests also require left Alt `0x04`, non-keypad punctuation usages and a changed report when one key is replaced by another without changing the pressed-key count.

- [ ] **Step 2: Run and observe compilation failure.**

```bash
cmake --build build/phase0/host-tests
ctest --test-dir build/phase0/host-tests -R 'hid|metrics' --output-on-failure
```

Expected: missing `HidEngine` and metrics types.

- [ ] **Step 3: Implement fail-safe HID semantics.**

Every transition, macro abort, mode change, disconnect and reboot path must queue `release all`. A Phase 0 synthetic source must feed at least 10,000 stable events through the same post-debounce route used by real matrix events; it may not call the BLE sender directly.

- [ ] **Step 4: Count all attempts, including failures.**

Use a fixed 100µs-bucket histogram through 100ms plus overflow:

```cpp
struct HidLatencyMetrics {
  uint32_t generated;
  uint32_t queued;
  uint32_t queue_failures;
  std::array<uint32_t, 1001> buckets;
};
```

The p95 calculation uses nearest-rank over `generated`; queue failures and overflow are failures, not discarded samples. Gate 5 requires `generated == queued`, `queue_failures == 0`, at least 10,000 samples and p95 upper bound `<= 20ms` during attack load.

- [ ] **Step 5: Bind HID and GATT identities.**

Generate the 16-byte device ID once from hardware-backed random data and store it in NVS. Expose no-padding RFC 4648 base32 in the BLE HID serial and the original 16 bytes through the encrypted GATT identity Characteristic. The Companion must strictly decode the serial before comparing; device name equality is not evidence.

- [ ] **Step 6: Verify host tests and target build.**

```bash
ctest --test-dir build/phase0/host-tests --output-on-failure
scripts/phase0/idf.sh -C firmware build
```

Expected: report sequence tests, overflow tests, release tests and histogram boundary tests pass.

- [ ] **Step 7: Commit the task.**

```bash
git add firmware protocol/phase0/gatt_probe_contract.json
git commit -m "test: enforce hid identity and latency semantics"
```

### Trace 6: Representative Web UI, Licensed CJK Pack, and Fixed OTA Budget

**Files:**

- Create: `web-probe/package.json`, `web-probe/package-lock.json`, `web-probe/vite.config.ts`
- Create: `web-probe/src/App.vue`, `web-probe/src/pages/*.vue`, `web-probe/tests/bundle.test.ts`
- Create: `assets/cjk/LICENSE-OFL-1.1.txt`, `assets/cjk/NOTICE.md`, `assets/cjk/noto-sans-cjk-sc.lock.json`, `assets/cjk/ui_strings_zh-Hans.txt`
- Create: `tools/cjk/build_cjk_pack.py`, `tools/cjk/tests/test_cjk_pack.py`
- Create: `firmware/partitions.csv`
- Create: `firmware/main/probe/cjk_font_pack.hpp`, `firmware/main/probe/cjk_font_pack.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Create: `firmware/test/host/test_cjk_font_pack.cpp`
- Create: `tools/phase0/image_budget.py`, `tools/phase0/tests/test_image_budget.py`
- Create: `docs/validation/phase0/flash-budget.md`

**Interfaces:**

- Consumes: Noto Sans CJK SC `Sans2.004`, GB2312 level-1 characters, all fixed Chinese UI strings, Vite production output and final signed app binary.
- Produces: deterministic gzip Web assets, flash-resident `CardputerCJK16` 16×16 2bpp font pack, fixed 8MiB partition layout and a preliminary unsigned-image budget result. Task 11 must repeat the same calculation on the final signed image before Gate 4 can pass.

- [ ] **Step 1: Write failing font and budget tests.**

```python
def test_slot_budget_uses_signed_image() -> None:
    result = evaluate_image_budget(
        signed_image_bytes=0x360000,
        slot_bytes=0x3C0000,
    )
    assert result.required_free_bytes == 0x60000
    assert result.remaining_bytes == 0x60000
    assert result.passed

def test_one_byte_over_budget_fails() -> None:
    assert not evaluate_image_budget(0x360001, 0x3C0000).passed
```

Font tests must require 3,755 GB2312 level-1 characters, every non-ASCII character in `ui_strings_zh-Hans.txt`, `U+FFFD`, sorted unique codepoints, deterministic output and replacement-glyph fallback.

- [ ] **Step 2: Run and observe missing-module failures.**

```bash
uv run pytest tools/cjk/tests tools/phase0/tests/test_image_budget.py -q
```

Expected: imports or files are missing.

- [ ] **Step 3: Pin the font source and license.**

`noto-sans-cjk-sc.lock.json` must record:

```json
{
  "url": "https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf",
  "sha256": "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b",
  "license_sha256": "6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2",
  "generated_family": "CardputerCJK16"
}
```

The build downloads the 16,437,364-byte source into `.tools/fonts/`, verifies it, creates a renamed subset and keeps the OFL license/notice beside source metadata. The full OTF is never embedded.

- [ ] **Step 4: Build a representative, not trivial, Web bundle.**

Pin `vue@3.5.39`, `typescript@5.9.3`, `vite@6.4.3`, `@vitejs/plugin-vue@5.2.4`, `vitest@3.2.7`. The probe includes Status, Profiles, physical Keymap, Macros/UTF-8 snippets, Pairing, and Import/Export pages with representative validation data. Tests reject external URLs/CDNs, sourcemaps, missing pages and nondeterministic gzip metadata.

Run:

```bash
scripts/phase0/npm.sh --prefix web-probe ci
scripts/phase0/npm.sh --prefix web-probe run build
scripts/phase0/npm.sh --prefix web-probe test
```

Expected: all six views are present and every shipped asset has a deterministic `.gz` counterpart.

- [ ] **Step 5: Fix the 8MiB partition table.**

Use:

```csv
# Name,Type,SubType,Offset,Size,Flags
nvs_keys,data,nvs_keys,0x13000,0x1000,encrypted
nvs,data,nvs,0x14000,0xC000,
otadata,data,ota,0x20000,0x2000,
phy_init,data,phy,0x22000,0x1000,
config_a,data,0x40,0x23000,0x24000,encrypted
config_b,data,0x41,0x47000,0x24000,encrypted
ota_0,app,ota_0,0x80000,0x3C0000,
ota_1,app,ota_1,0x440000,0x3C0000,
```

Set partition-table offset `0x12000`, 8MiB flash and no PSRAM. Each 144KiB config slot can hold the 128KiB import ceiling plus metadata. Each OTA slot is `0x3C0000`; required free space is `0x60000`, so the final signed app maximum is `0x360000`.

- [ ] **Step 6: Embed the actual generated resources and evaluate the actual signed image.**

The firmware build must embed the generated gzip files and CJK pack. `image_budget.py` reads `partitions.csv` and the final signed `.bin`; it must not use ELF section totals, raw Vite size or pre-signing image size.

```bash
uv run tools/cjk/build_cjk_pack.py --output build/phase0/generated
scripts/phase0/idf.sh -C firmware build
uv run tools/phase0/image_budget.py \
  --partitions firmware/partitions.csv \
  --image firmware/build/cardputer_codex_phase0.bin \
  --json build/phase0/flash-budget.json
```

Expected: tests pass; the preliminary report contains exact slot, image, remaining and threshold bytes. A build over `0x360000` fails, but a passing unsigned result keeps Gate 4 at `NOT_RUN` until Task 11 verifies the signed image.

- [ ] **Step 7: Commit the task.**

```bash
git add web-probe assets/cjk tools/cjk tools/phase0/image_budget.py tools/phase0/tests/test_image_budget.py firmware docs/validation/phase0/flash-budget.md
git commit -m "feat: prove web font and ota image budget"
```

### Trace 7: Unicode Injection Core and Persistent Crash Semantics

**Files:**

- Create: `protocol/phase0/fixtures/text-operation-v1.json`
- Create: `companion/Package.swift`
- Create: `companion/Sources/CardputerProbeCore/InjectionModels.swift`
- Create: `companion/Sources/CardputerProbeCore/UnicodeChunker.swift`
- Create: `companion/Sources/CardputerProbeCore/UnicodeInjector.swift`
- Create: `companion/Sources/CardputerProbeCore/InjectionLedger.swift`
- Create: `companion/Tests/CardputerProbeCoreTests/UnicodeChunkerTests.swift`
- Create: `companion/Tests/CardputerProbeCoreTests/UnicodeInjectorTests.swift`
- Create: `companion/Tests/CardputerProbeCoreTests/InjectionRecoveryTests.swift`

**Interfaces:**

- Consumes: authenticated UTF-8 operation up to 1024 bytes, Accessibility/Secure Input/focus backends and a SQLite ledger.
- Produces: `completed`, `failed`, `partial` or `indeterminate`; prefix lengths are always UTF-8 bytes at a Swift `Character` boundary.

- [ ] **Step 1: Write failing Swift tests.**

Tests must include:

```swift
func testFocusChangeAfterOneChineseCharacterIsPartial() throws {
    let text = "中文"
    let result = try injectorWithFocusSequence([sameFocus, changedFocus])
        .inject(text, maxUTF16CodeUnitsPerChunk: 1)
    XCTAssertEqual(result.state, .partial)
    XCTAssertEqual(result.postedUTF8Bytes, 3)
}

func testIntentRecoveredAfterCrashIsIndeterminate() throws {
    try ledger.begin(operation, focus: focusAudit)
    let recovered = try reopenedLedger.reconcileNonTerminal()
    XCTAssertEqual(recovered.count, 1)
    XCTAssertEqual(recovered.first?.state, .indeterminate)
}
```

Also test exactly 1024 UTF-8 bytes accepted, 1025 rejected, surrogate/combining sequences never split, permission/Secure Input failures post zero events, duplicate operation with same hash returns the known result, and same operation ID with different hash returns `invalid_request`.

- [ ] **Step 2: Run and confirm compile failure.**

```bash
swift test --package-path companion --filter UnicodeChunkerTests
swift test --package-path companion --filter UnicodeInjectorTests
swift test --package-path companion --filter InjectionRecoveryTests
```

Expected: types and targets are missing.

- [ ] **Step 3: Lock the public result model.**

```swift
public struct InjectionResult: Equatable, Codable, Sendable {
    public let operationID: UUID
    public let state: InjectionState
    public let postedUTF8Bytes: Int
    public let verifiedUTF8Bytes: Int?
    public let errorCode: InjectionErrorCode?
}

public protocol UnicodeEventPosting: Sendable {
    func post(utf16: [UInt16], toPID pid: pid_t) throws
}

public protocol InjectionLedger: Sendable {
    func begin(_ operation: TextOperation, focus: FocusAuditDescriptor) throws -> BeginOutcome
    func markPosted(operationID: UUID, utf8Bytes: Int) throws
    func markVerified(operationID: UUID, utf8Bytes: Int) throws
    func finish(_ result: InjectionResult) throws
    func reconcileNonTerminal() throws -> [InjectionResult]
}
```

The ledger stores paired device ID, operation ID, payload SHA-256, PID/AX audit descriptor, posted/verified byte counts, status and timestamps, never the source text.

- [ ] **Step 4: Implement check-before-each-chunk semantics.**

```swift
for chunk in chunker.chunks(text) {
    guard accessibility.isTrusted() else { return failOrPartial(.permissionDenied) }
    guard !secureInput.isEnabled() else { return failOrPartial(.secureInputActive) }
    guard try focus.stillMatches(binding) else { return partial(.staleState) }
    try poster.post(utf16: Array(chunk.utf16), toPID: binding.pid)
    posted += chunk.utf8.count
    try ledger.markPosted(operationID: operation.id, utf8Bytes: posted)
}
```

`completed` is written only after all chunks. A posted count does not assert application acceptance; `verifiedUTF8Bytes` advances only after independent AX/target readback.

- [ ] **Step 5: Re-run all Swift core tests.**

```bash
swift test --package-path companion
```

Expected: all chunking, focus, Secure Input, idempotency and crash-window tests pass.

- [ ] **Step 6: Commit the task.**

```bash
git add companion protocol/phase0/fixtures/text-operation-v1.json
git commit -m "feat: add fail-closed unicode injection core"
```

### Trace 8: Native macOS Backends, Signed Probe App, and Five-Application HIL

**Files:**

- Modify: `companion/Package.swift`
- Create: `companion/ProbeApp/Info.plist`
- Create: `companion/Sources/CardputerProbeMac/AXFocusChecker.swift`
- Create: `companion/Sources/CardputerProbeMac/CGEventUnicodePoster.swift`
- Create: `companion/Sources/CardputerProbeMac/SecureInputChecker.swift`
- Create: `companion/Sources/CardputerProbeMac/SQLiteInjectionLedger.swift`
- Create: `companion/Sources/cardputer-companion-probe/main.swift`
- Create: `companion/Sources/focus-change-fixture/main.swift`
- Create: `companion/Sources/secure-input-fixture/main.swift`
- Create: `tests/phase0/test_no_clipboard_fallback.py`
- Create: `scripts/phase0/build_companion_probe_app.sh`
- Create: `scripts/phase0/run_macos_unicode_hil.sh`
- Create: `docs/validation/phase0/macos-unicode-matrix.md`

**Interfaces:**

- Consumes: a focused macOS AX element, real codesigning identity, stable bundle ID and operator-selected test target.
- Produces: a signed `.app`, exact target readback hashes and per-case results for TextEdit, VS Code, browser, Terminal and iTerm2.

- [ ] **Step 1: Add native-backend contract tests with fakes.**

Require that native adapters conform to the core protocols, never read/write the clipboard and never synthesize Command-V. A source-level test must fail if `NSPasteboard`, `kVK_ANSI_V` combined with Command, or AppleScript paste appears in the injector.

- [ ] **Step 2: Run the native contract tests and observe failure.**

```bash
uv run python -m unittest tests/phase0/test_no_clipboard_fallback.py -v
swift test --package-path companion --filter CardputerProbeMac
```

Expected: the source scan or Swift build fails because the native target and files do not exist.

- [ ] **Step 3: Implement public macOS APIs.**

- `AXFocusChecker`: bind `NSWorkspace.shared.frontmostApplication.processIdentifier` and `AXFocusedUIElement`; compare both before every chunk.
- `SecureInputChecker`: call `Carbon.HIToolbox.IsSecureEventInputEnabled()` before every chunk.
- `CGEventUnicodePoster`: call `CGEvent.keyboardSetUnicodeString` and post directly to the bound PID/event path.
- `SQLiteInjectionLedger`: use transactional SQLite3 and reconcile non-terminal injection records to `indeterminate` after restart.

- [ ] **Step 4: Build and verify an app bundle.**

```bash
swift build --package-path companion -c release
scripts/phase0/build_companion_probe_app.sh \
  --bundle-id com.lynx.cardputer.companion.phase0 \
  --output build/phase0/CardputerCompanionProbe.app
codesign --verify --deep --strict --verbose=2 \
  build/phase0/CardputerCompanionProbe.app
```

Expected now: if no valid identity exists, the build script emits a machine-readable `BLOCKED` prerequisite and does not ad-hoc sign. Once a real identity is supplied through a non-secret identity label, strict verification passes.

- [ ] **Step 5: Run the exact HIL matrix.**

Browser baseline is Google Chrome with a locally served `contenteditable` fixture. Each target must run 100 normal Chinese/标点/emoji/combining-string operations plus 10 exact 1024-byte operations:

```bash
scripts/phase0/run_macos_unicode_hil.sh \
  --apps textedit,vscode,chrome,terminal,iterm2 \
  --normal-iterations 100 \
  --full-size-iterations 10 \
  --output build/phase0/macos/unicode-matrix.json
```

For each app, independently read back the target value or AX content and compare SHA-256 with the source. Injector `posted` counts and screenshots are insufficient. The suite also changes focus mid-stream, enables a Secure Input fixture, kills the Companion after ledger `intent`, restarts it, and checks that no text is replayed.

- [ ] **Step 6: Prove TCC continuity across a signed upgrade.**

Build A and B with the same bundle ID and real signing identity but different versions. Grant Accessibility to A, replace it with B, verify B retains access and re-run an injection. `swift run`, terminal-inherited permission and ad-hoc signatures do not count.

- [ ] **Step 7: Gate the result honestly.**

`P0-G2-UNICODE` can be `PASS` only when all five apps, focus, Secure Input, crash and signed-upgrade cases pass. Missing VS Code, a missing signing identity or an unreadable target makes the gate `BLOCKED`; content mismatch makes it `FAIL`.

- [ ] **Step 8: Commit the task.**

```bash
git add companion tests/phase0/test_no_clipboard_fallback.py scripts/phase0/build_companion_probe_app.sh scripts/phase0/run_macos_unicode_hil.sh docs/validation/phase0/macos-unicode-matrix.md
git commit -m "test: add native macos unicode hil probe"
```

### Trace 9: CoreBluetooth Identity, Pairing, GATT Replay, and WSS Channel Binding

**Files:**

- Create: `companion/Sources/CardputerProbeCore/SecurityProtocol.swift`
- Create: `companion/Sources/CardputerProbeCore/GATTAuthenticatedReceiver.swift`
- Create: `companion/Sources/CardputerProbeCore/BLEIdentityProbe.swift`
- Create: `companion/Sources/CardputerProbeMac/CoreBluetoothCentral.swift`
- Create: `companion/Sources/CardputerProbeMac/IOHIDInventory.swift`
- Create: `companion/Sources/CardputerProbeMac/WebSocketProbeServer.swift`
- Create: `companion/Tests/CardputerProbeCoreTests/SecurityProtocolTests.swift`
- Create: `companion/Tests/CardputerProbeCoreTests/GATTAuthenticatedReceiverTests.swift`
- Create: `companion/Tests/CardputerProbeCoreTests/BLEIdentityProbeTests.swift`
- Create: `companion/Tests/CardputerProbeCoreTests/WebSocketProbeServerTests.swift`
- Create: `firmware/main/probe/security_probe.hpp`, `firmware/main/probe/security_probe.cpp`
- Create: `firmware/test/host/test_gatt_frame.cpp`, `firmware/test/host/test_gatt_sender.cpp`
- Create: `docs/validation/phase0/macos-ble-binding.md`

**Interfaces:**

- Consumes: Task 3 vectors, a bonded Cardputer, current Network.framework TLS metadata and protected GATT frames.
- Produces: matching CryptoKit/mbedTLS derivations, a physical same-device identity proof and replay/channel-binding negative-test evidence.

- [ ] **Step 1: Write cross-language fixture tests first.**

Swift and firmware tests must load the same committed vectors and match transcript hash, pair root, GATT key, SAS, GATT tag and WSS signature verification. Negative vectors must fail for any transcript or connection mutation.

- [ ] **Step 2: Run and observe expected failures.**

```bash
swift test --package-path companion --filter SecurityProtocolTests
swift test --package-path companion --filter GATTAuthenticatedReceiverTests
swift test --package-path companion --filter BLEIdentityProbeTests
cmake --build build/phase0/host-tests
ctest --test-dir build/phase0/host-tests -R 'gatt|replay' --output-on-failure
```

Expected: missing implementations.

- [ ] **Step 3: Implement verify-before-state-update replay handling.**

```swift
public mutating func accept(_ frame: GATTAuthenticatedFrame) -> ReplayDecision {
    guard frame.connectionID == activeConnectionID else { return .staleConnection }
    guard authenticator.verify(frame.authenticatedBytes, tag: frame.tag) else {
        return .invalidTag
    }
    return replayWindow.observe(frame.counter)
}
```

The Companion generates each 128-bit connection ID and owns the per-device 32-entry receive window, which changes only after a valid tag. Firmware owns the sending counter and HMAC generation. New BLE connections replace the connection ID and reset both connection-local states; the Companion SQLite operation ledger continues cross-connection deduplication. C++ tests validate sender encoding/tags, while Swift tests drive real CoreBluetooth notification bytes through authentication, replay, reassembly and ledger integration.

- [ ] **Step 4: Implement real TLS exporter binding.**

Use `sec_protocol_metadata_create_secret_with_context` on macOS with zero context bytes and the public mbedTLS exporter callback/API exposed by ESP-IDF `v5.5.4` with `use_context=0`; both must match the foundation fixture. Do not read private TLS structures or substitute certificate hash/TLS random. Reject wrong SPKI before application authentication; reject a valid signature replayed on another TLS connection.

The macOS listener must bind only to the user-selected local LAN interface and reject source addresses outside that interface's current subnet. Unit tests inspect the selected `NWInterface`/source policy; HIL verifies no wildcard/public interface, UPnP, NAT-PMP or tunnel listener is opened.

- [ ] **Step 5: Prove HID/GATT are the same physical device.**

```swift
public struct BLEIdentityEvidence: Codable, Sendable {
    public let coreBluetoothPeripheralID: UUID
    public let gattDeviceID: String
    public let hidSerialDeviceID: String?
    public let encryptedReadDeniedBeforeBond: Bool
    public let encryptedReadSucceededAfterBond: Bool
}
```

The protected GATT raw device ID and strictly base32-decoded IOHID serial must match exactly. A CoreBluetooth UUID, simultaneous connection or equal device name is insufficient. Verify access denied before bonding, successful after bonding, successful after reconnect, and only one active HID host bond. If macOS does not expose a stable HID serial that can be decoded and matched, Gate 1 is `BLOCKED` and the design returns for another binding proof.

- [ ] **Step 6: Execute the pairing and negative matrix.**

Test changed identity/nonce/role/version, wrong SAS confirmation, mismatched or single-channel bind challenge, wrong SPKI, old TLS signature, duplicate/rollback/too-far-ahead GATT counter, old connection ID, unbound central, a second HID host outside the physical pairing window, cross-domain Web credential and revoked identity. The Cardputer display and Mac must independently confirm the same six-digit SAS.

- [ ] **Step 7: Re-run tests and target build.**

```bash
swift test --package-path companion
ctest --test-dir build/phase0/host-tests --output-on-failure
scripts/phase0/idf.sh -C firmware build
```

Expected: all vector/negative tests pass before HIL is attempted.

- [ ] **Step 8: Commit the task.**

```bash
git add companion firmware docs/validation/phase0/macos-ble-binding.md
git commit -m "feat: prove ble identity and channel binding"
```

### Trace 10: Current Codex App Server Capability Audit

**Files:**

- Create: `protocol/phase0/codex-capability-requirements.json`
- Create: `tools/app_server_probe/audit.py`
- Create: `tools/app_server_probe/live_probe.py`
- Create: `tools/app_server_probe/tests/test_audit.py`
- Create: `tools/app_server_probe/tests/test_live_probe.py`
- Create: `tools/app_server_probe/tests/fixtures/complete/`
- Create: `tools/app_server_probe/tests/fixtures/missing_available_decisions/`
- Create: `tools/app_server_probe/tests/fixtures/missing_network_port/`
- Create: `tools/app_server_probe/tests/fixtures/experimental_input/`
- Create: `scripts/phase0/generate_app_server_schema.sh`
- Create: `scripts/phase0/run_app_server_capability_probe.sh`
- Create: `docs/validation/phase0/app-server-capability-matrix.md`

**Interfaces:**

- Consumes: `codex app-server generate-json-schema` stable/experimental output and a read-only stdio App Server handshake.
- Produces: a field-level, stability-aware capability matrix with no thread title, cwd, prompt or item content.

- [ ] **Step 1: Write field-level failing tests.**

Test names must include:

```text
test_method_presence_without_required_fields_fails
test_missing_available_decisions_fails
test_missing_network_port_fails
test_experimental_description_is_conditional_even_in_stable_bundle
test_permission_reject_requires_defined_upstream_encoding
test_live_probe_never_writes_thread_payload_to_report
```

- [ ] **Step 2: Run and observe missing implementation.**

```bash
uv run python -m unittest discover -s tools/app_server_probe/tests -v
```

Expected: import failures.

- [ ] **Step 3: Encode semantic requirements, not filename guesses.**

The matrix must cover:

- `thread/list`, `thread/start`, `thread/resume`;
- `turn/start`, `turn/interrupt`;
- `thread/compact/start` and compaction lifecycle notification;
- command, file-change, network and permission approval request/response envelopes;
- request ID, `threadId`, `turnId`, `itemId`, optional approval ID, state/version and upstream available decisions;
- network host, port, protocol and reason;
- file path/change counts/summary and optional `grantRoot`;
- option and free-text user input request/response;
- `initialize.capabilities.experimentalApi`;
- thread/turn/item lifecycle notifications.

Presence of a method without required fields is `FAIL`, not partial success. Experimental Input may pass only when accurately detected, enabled through capability negotiation and disabled by default for devices that do not advertise support.

- [ ] **Step 4: Generate and hash current schemas.**

```bash
scripts/phase0/generate_app_server_schema.sh build/phase0/app-server
shasum -a 256 build/phase0/app-server/stable/*.json \
  build/phase0/app-server/experimental/*.json \
  > build/phase0/app-server/schema-sha256.txt
```

The generator captures `codex --version`, exact command, stable and experimental outputs. It never modifies Codex databases, JSONL or internal files.

- [ ] **Step 5: Perform a minimal read-only live handshake.**

Send:

```json
{"id":1,"method":"initialize","params":{"clientInfo":{"name":"cardputer-phase0-readonly-probe","version":"0.0.1"},"capabilities":{"experimentalApi":true}}}
{"method":"initialized"}
{"id":2,"method":"thread/list","params":{"limit":1}}
```

The report retains only method names, response field names, counts and stable error classes. It must drop titles, cwd, IDs, prompts and item content before writing disk.

- [ ] **Step 6: Run the audit and inspect likely blockers.**

```bash
scripts/phase0/run_app_server_capability_probe.sh \
  --schema-root build/phase0/app-server \
  --requirements protocol/phase0/codex-capability-requirements.json \
  --output build/phase0/app-server/capability.json
```

Current preflight on Codex CLI `0.144.6` found the core thread/turn/interrupt/compact methods, but also found reasons that require exact audit: user input is marked experimental, approval requests may lack `availableDecisions`, network context may lack a distinct port, and permission responses are not a common decision enum. If the scripted audit confirms any required field is unavailable, `P0-G3-CODEX` is `FAIL` and implementation stops for design review.

- [ ] **Step 7: Commit the task.**

```bash
git add protocol/phase0/codex-capability-requirements.json tools/app_server_probe scripts/phase0/generate_app_server_schema.sh scripts/phase0/run_app_server_capability_probe.sh docs/validation/phase0/app-server-capability-matrix.md
git commit -m "test: audit codex app server capabilities"
```

### Trace 11: Secure Boot, Encryption, OTA Trust, and Signed USB Recovery

**Files:**

- Create: `firmware/sdkconfig.release-probe.defaults`
- Create: `firmware/main/probe/recovery_policy.hpp`, `firmware/main/probe/recovery_policy.cpp`
- Create: `firmware/main/probe/usb_recovery.hpp`, `firmware/main/probe/usb_recovery.cpp`
- Create: `protocol/phase0/release-trust-v1.md`
- Create: `tools/phase0/ota_manifest.py`
- Create: `tools/phase0/tests/test_ota_manifest.py`
- Create: `tools/phase0/verify_release_config.py`
- Create: `tools/recovery/verify_bundle.py`
- Create: `tools/recovery/flash_bundle.py`
- Create: `tools/recovery/tests/test_recovery_bundle.py`
- Create: `scripts/phase0/generate_test_release_keys.sh`
- Create: `scripts/phase0/backup_flash.sh`
- Create: `scripts/phase0/run_security_hil.py`
- Create: `docs/validation/phase0/security-hil.md`

**Interfaces:**

- Consumes: dedicated security-test Cardputer, repo-external test keys, signed firmware and OTA manifest.
- Produces: an irreversible-HIL report proving Secure Boot v2, Flash/NVS encryption and signed recovery, or `BLOCKED/FAIL`.

- [ ] **Step 1: Write failing trust-chain tests.**

Tests must reject a changed hardware model, image length/hash, protocol range, config schema range, version, P-256 manifest signature or Secure Boot RSA public-key digest.

- [ ] **Step 2: Run and observe the missing recovery verifier.**

```bash
uv run pytest \
  tools/phase0/tests/test_ota_manifest.py \
  tools/recovery/tests/test_recovery_bundle.py -q
```

Expected: imports fail because the manifest and recovery verifier do not exist.

- [ ] **Step 3: Document and implement the dual-key trust chain.**

ESP32-S3 Secure Boot v2 signs bootloader/apps with RSA-PSS/RSA-3072, while the approved OTA manifest uses ECDSA P-256. Do not pretend one private key serves both algorithms. `release-trust-v1.md` must define:

- ECDSA P-256 manifest key signs model/version/protocol/schema/image length/image SHA-256 and the expected Secure Boot RSA public-key digest;
- RSA-3072 Secure Boot v2 key signs bootloader/app;
- USB recovery verifies the P-256 manifest, pinned RSA digest, model, version/schema policy and RSA-signed image;
- both public identities belong to one release trust record, but private keys remain separate.

If “一致发布信任链” is interpreted as requiring the same key/algorithm, Gate 6 is technically impossible on ESP32-S3 and must return to design.

- [ ] **Step 4: Implement an explicit USB recovery path.**

`verify_bundle.py` validates the P-256 manifest signature, hardware model, version/schema policy, image length/hash and pinned Secure Boot RSA public-key digest before any write. `flash_bundle.py` then speaks a versioned USB CDC recovery protocol to the currently authenticated app; it does not use ROM flashing as the normal recovery path.

Holding the physical recovery chord boots the RSA-authenticated app into a recovery-only mode with BLE, Wi-Fi, Web, macros and HID output disabled. `usb_recovery.cpp` streams the bundle into the inactive OTA slot through `esp_ota_write`, so Flash Encryption Release remains active without retaining the device encryption key on the host. Before `esp_ota_set_boot_partition`, device-side `recovery_policy.cpp` independently verifies the external P-256 release manifest, target model, version/config schema policy, full image SHA-256 and Secure Boot RSA signature/digest, then atomically writes that accepted manifest into the encrypted config slot paired with the inactive OTA slot before changing the boot partition.

Configure Secure ROM Download Mode so arbitrary RAM execution and eFuse manipulation are unavailable; ROM writes are not the supported recovery workflow. Direct `esptool --no-stub` bypass attempts with unsigned, wrong-RSA, same-RSA/wrong-manifest, plaintext-for-encrypted-flash and tampered images are mandatory HIL cases. None may reach normal services. If a signed application recovery path plus Secure ROM restrictions cannot enforce this behavior, Gate 6 is `FAIL` and the design returns for a different partition/recovery architecture.

- [ ] **Step 5: Build a release-probe image with test keys.**

Enable Secure Boot v2 signed binaries, Flash Encryption Release, NVS Encryption, rollback and the `0x12000` partition-table offset. Generate test-only P-256 and RSA-3072 keys in ignored storage, then build and check that the signed bootloader ends before the partition table:

```bash
scripts/phase0/generate_test_release_keys.sh build/phase0/keys
scripts/phase0/idf.sh -C firmware -B build/release-probe \
  -D SDKCONFIG=build/release-probe/sdkconfig \
  -D SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.release-probe.defaults' \
  fullclean
scripts/phase0/idf.sh -C firmware -B build/release-probe \
  -D SDKCONFIG=build/release-probe/sdkconfig \
  -D SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.release-probe.defaults' \
  build
uv run tools/phase0/verify_release_config.py \
  --sdkconfig firmware/build/release-probe/sdkconfig \
  --require secure-boot-v2,flash-encryption-release,nvs-encryption,rollback
uv run tools/phase0/image_budget.py \
  --partitions firmware/partitions.csv \
  --image firmware/build/release-probe/cardputer_codex_phase0.bin
```

Expected: the independent release build proves all required effective Kconfig values and produces a signed image at or below `0x360000`; the same image SHA-256 is used by budget and HIL. Private key paths and contents are absent from logs and Git.

- [ ] **Step 6: Make the destructive checkpoint impossible to bypass.**

`run_security_hil.py` defaults to `--dry-run` and requires both:

```text
--confirm-chip-id <exact-chip-id>
--acknowledge-permanent-efuse-change
```

Before any write it must re-read chip ID, revision, flash size and eFuse summary and compare them to the approved manifest. Mismatch stops without writing.

- [ ] **Step 7: Back up the exact 8MiB device before any ordinary probe flash.**

```bash
CARDPUTER_PORT=/dev/cu.usbmodem-exact \
  scripts/phase0/backup_flash.sh \
  --port /dev/cu.usbmodem-exact \
  --size 0x800000 \
  --output build/phase0/device-backup
```

Expected: full image plus SHA-256 manifest. The execution agent must resolve the actual port first; the example path is never used unverified.

- [ ] **Step 8: Pause for explicit irreversible-hardware approval.**

Present exact chip ID, hardware revision, backup hash and the permanent eFuse actions to the user. Do not continue until the user names this unit as the dedicated security test device and approves permanent changes.

- [ ] **Step 9: Execute and verify the security matrix on that device.**

Verify:

1. Secure Boot key digest and boot state;
2. Flash Encryption Release mode;
3. NVS plaintext is absent from raw flash and encrypted NVS can be read only through the running app;
4. valid signed USB recovery succeeds;
5. unsigned, tampered, wrong manifest P-256 key, wrong RSA key and wrong model recovery images fail;
6. reboot repeats eFuse/runtime-image checks;
7. ordinary development build cannot be loaded as a bypass.

Virtual eFuse and successful dry-run remain supporting evidence only.

- [ ] **Step 10: Commit non-secret source and sanitized results.**

```bash
git add firmware/sdkconfig.release-probe.defaults firmware/main/probe/recovery_policy.hpp firmware/main/probe/recovery_policy.cpp firmware/main/probe/usb_recovery.hpp firmware/main/probe/usb_recovery.cpp protocol/phase0/release-trust-v1.md tools/phase0/ota_manifest.py tools/phase0/tests/test_ota_manifest.py tools/phase0/verify_release_config.py tools/recovery scripts/phase0/generate_test_release_keys.sh scripts/phase0/backup_flash.sh scripts/phase0/run_security_hil.py docs/validation/phase0/security-hil.md
git commit -m "test: validate release security trust chain"
```

### Trace 12: Concurrent Resource, Limit, and Attack-Load HIL

**Files:**

- Modify: `firmware/main/probe/resource_metrics.hpp`, `firmware/main/probe/resource_metrics.cpp`
- Modify: `firmware/main/probe/network_services.hpp`, `firmware/main/probe/network_services.cpp`
- Create: `firmware/main/probe/web_security.hpp`, `firmware/main/probe/web_security.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`
- Create: `firmware/test/host/test_web_security.cpp`
- Create: `protocol/phase0/concurrency-evidence.schema.json`
- Create: `tools/phase0/run_concurrency_hil.py`
- Create: `tools/phase0/stress_device.py`
- Create: `tools/phase0/evaluate_metrics.py`
- Create: `tools/phase0/tests/test_evaluate_metrics.py`
- Modify: `firmware/test/host/test_resource_metrics.cpp`

**Interfaces:**

- Consumes: firmware metrics stream and attack-client attempted/accepted/rejected counts.
- Produces: exact steady/transient heap, largest-block, allocation-failure, stack and HID latency decisions.

- [ ] **Step 1: Write failing threshold tests.**

```python
def test_resource_thresholds_are_inclusive() -> None:
    sample = {
        "free_internal_heap": 64 * 1024,
        "largest_internal_block": 32 * 1024,
        "tls_burst_free_internal_heap": 40 * 1024,
        "allocation_failures": 0,
    }
    assert evaluate_resources(sample).passed

def test_queue_failure_cannot_be_removed_from_p95() -> None:
    metrics = {"generated": 10000, "queued": 9999, "queue_failures": 1}
    assert not evaluate_hid(metrics).passed
```

- [ ] **Step 2: Run and observe missing implementation.**

```bash
uv run pytest tools/phase0/tests/test_evaluate_metrics.py -q
```

Expected: import failure.

- [ ] **Step 3: Instrument internal memory and every task stack.**

Firmware records:

```cpp
struct TaskStackMetric {
  const char* task;
  uint32_t configured_bytes;
  uint32_t high_water_free_bytes;
};

struct ResourceSample {
  uint64_t monotonic_us;
  uint32_t free_internal_heap;
  uint32_t largest_internal_block;
  uint32_t allocation_failures;
  HidLatencyMetrics hid;
};
```

Use `heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`, `heap_caps_get_largest_free_block`, `heap_caps_register_failed_alloc_callback` and ESP-IDF `uxTaskGetStackHighWaterMark2()`. Treat the ESP-IDF value as bytes and add a unit test that prevents a second word-to-byte multiplication. Each task requires free stack `>= max(20% configured, 1024 bytes)`.

- [ ] **Step 4: Implement allocation-before-rejection tests.**

Build a real Phase 0 Web admission/authentication state machine, not an attack-only stub:

```cpp
struct RequestAdmission {
  bool authenticated;
  bool csrf_valid;
  bool host_valid;
  bool origin_valid;
  uint32_t header_bytes;
  uint32_t body_bytes;
  uint8_t json_depth;
};

AdmissionDecision admit_request(
    const RequestAdmission& request,
    ClientRateState& client,
    MonotonicMillis now) noexcept;
```

It must implement the eight-digit five-minute Web pairing window, five-failure closure/ten-minute backoff, five-client ceiling, 30-minute idle expiry, Host/Origin/CSRF checks, authenticated/unauthenticated/health rate classes and all size/depth limits. Host tests snapshot the large-allocation counter before and after every rejection and require no change.

Attack cases:

- four established HTTPS sessions plus one pending TLS handshake; a fifth established session and second simultaneous handshake are rejected;
- header over 8KiB, body over 16KiB, import over 128KiB, JSON depth over 8;
- WebSocket frame over 16KiB;
- authenticated 10/s average and burst 20;
- unauthenticated 1/s average and burst 4;
- `/healthz` global 10/s;
- per-source TLS 3/min, global 6/min, 17th source against the 16-entry table;
- five failed Web pairing codes and 10-minute backoff;
- wrong Host/Origin/CSRF;
- invalid GATT tag/counter/connection replay.
- connection attempts arriving outside the selected local interface/subnet and any unexpected wildcard/public listener.

Every rejection records heap before/after and must occur before allocating request-sized buffers.

- [ ] **Step 5: Run the two required resource scenarios.**

Steady worst case: HID, encrypted GATT, Wi-Fi, WSS, four HTTPS connections, CJK pack lookup and screen refresh active. Require free internal heap `>=64KiB` and largest block `>=32KiB`.

Transient worst case: while four HTTPS sessions remain established, add one pending TLS handshake, a streaming 128KiB import and exactly 100 maximum 16KiB WSS frames over five seconds. The burst contains a 20-session page and four-fragment 64KiB approval detail repeatedly, drives the 16-entry network queue to its measured maximum, and records produced/consumed/resync counts. Require free internal heap `>=40KiB`, zero allocation failures and proof that the load generator reached all declared rates.

During unauthenticated saturation, inject at least 10,000 post-debounce HID events through the real queue path and require `generated == queued`, no stuck modifier/release mismatch and p95 `<=20ms`.

- [ ] **Step 6: Execute the same-firmware concurrency HIL.**

```bash
uv run tools/phase0/run_concurrency_hil.py \
  --port "$CARDPUTER_PORT" \
  --companion-interface "$COMPANION_INTERFACE" \
  --firmware-sha256 "$FIRMWARE_SHA256" \
  --schema protocol/phase0/concurrency-evidence.schema.json \
  --output build/phase0/concurrency/raw.json
```

The runner opens one time window and proves BLE HID, protected GATT, Wi-Fi, four established HTTPS sessions and exporter-authenticated WSS are simultaneously active on the same firmware/Mac. It records exact HID/GATT device ID, bond state, HTTPS responses, WSS exporter proof, Wi-Fi state and monotonic timestamps. Mock, sequential service checks or mixed firmware hashes fail the schema.

- [ ] **Step 7: Execute and cross-check load generation.**

```bash
uv run tools/phase0/stress_device.py \
  --target https://cardputer-codex-device.local \
  --duration 30m \
  --output build/phase0/resource/raw
uv run tools/phase0/evaluate_metrics.py \
  --firmware build/phase0/resource/raw/firmware.jsonl \
  --attacker build/phase0/resource/raw/attacker.json \
  --output build/phase0/resource/result.json
```

Expected: the attacker report proves every requested case reached the target rate and includes attempted/accepted/rejected counts. A load generator that did not reach its target makes the check `BLOCKED`, not `PASS`.

- [ ] **Step 8: Keep the 8-hour criterion outside this gate without losing the harness.**

Phase 0 runs the 30-minute worst-case gate above. The same runner accepts `--duration 8h` for the Phase 5 soak, where monotonic heap loss must stay within 8KiB and final HID p95 degradation within 10%; a shorter run must label itself `smoke` and cannot satisfy that later acceptance criterion.

- [ ] **Step 9: Commit the task.**

```bash
git add firmware protocol/phase0/concurrency-evidence.schema.json tools/phase0/run_concurrency_hil.py tools/phase0/stress_device.py tools/phase0/evaluate_metrics.py tools/phase0/tests/test_evaluate_metrics.py
git commit -m "test: enforce phase zero resource limits"
```

### Trace 13: Execute HIL, Finalize Evidence, and Enforce GO/NO-GO

**Files:**

- Create: `scripts/phase0/run_phase0.sh`
- Modify: `tools/phase0/evidence.py`
- Create: `tests/phase0/test_finalize_report.py`
- Update: `docs/validation/phase0/gate-matrix.json`
- Update: `docs/validation/phase0/flash-budget.md`
- Update: `docs/validation/phase0/macos-unicode-matrix.md`
- Update: `docs/validation/phase0/macos-ble-binding.md`
- Update: `docs/validation/phase0/app-server-capability-matrix.md`
- Update: `docs/validation/phase0/security-hil.md`
- Create: `docs/validation/phase0/go-no-go.md`
- Update: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`

**Interfaces:**

- Consumes: all automated results, HIL results and raw-evidence hashes from Tasks 1–12.
- Produces: one sanitized signed-off report, human-readable gate matrix and a deterministic `GO` or `NO_GO`.

- [ ] **Step 1: Write the failing end-to-end report test.**

The test feeds one complete set of valid measurements and one set with a missing raw hash into `finalize_report()`. The first must exit `0`; the second must exit non-zero and set `decision=NO_GO`. A third fixture combines `"reported_status":"PASS"` with a failing numeric measurement and must be rejected as untrusted input.

- [ ] **Step 2: Run and observe the missing finalizer failure.**

```bash
uv run python -m unittest tests/phase0/test_finalize_report.py -v
```

Expected: import or attribute failure for `finalize_report`.

- [ ] **Step 3: Build the orchestration script without defaulting hardware values.**

Full HIL mode requires explicit Cardputer port, Companion interface, run ID and evidence directory. `--host-only` runs only non-hardware checks and writes all HIL-dependent checks as `BLOCKED`. Missing hardware, app, permission, identity or dedicated security unit writes a `BLOCKED` result and continues collecting independent gates; it never fabricates a pass.

- [ ] **Step 4: Execute all non-destructive checks.**

```bash
scripts/phase0/run_phase0.sh \
  --host-only \
  --run-id "$(date -u +%Y%m%dT%H%M%SZ)" \
  --evidence-root build/phase0
```

Expected on an incomplete environment: valid `NO_GO` report with explicit `BLOCKED/FAIL`, not a runner crash.

- [ ] **Step 5: Execute hardware writes only after exact target checks.**

Resolve the unique serial path, back up 8MiB, compare flash size/hardware revision, flash the ordinary probe, and run Gates 1, 2, 4 and 5. Gate 6 irreversible HIL follows the separate approval checkpoint in Task 11.

Run full non-security HIL only with resolved values:

```bash
scripts/phase0/run_phase0.sh \
  --cardputer-port "$CARDPUTER_PORT" \
  --companion-interface "$COMPANION_INTERFACE" \
  --run-id "$(date -u +%Y%m%dT%H%M%SZ)" \
  --evidence-root build/phase0
```

The runner rejects empty or unresolved port/interface values. Gate 6 remains `BLOCKED` until the separately approved security-HIL report is supplied.

- [ ] **Step 6: Validate every evidence edge.**

For each `PASS`, verify:

- raw file exists outside Git;
- SHA-256 matches;
- source Git commit and firmware hash match the tested build;
- threshold sample count and duration are present;
- mock/host evidence is supplementary, not the sole HIL evidence;
- identifiers and user/Codex data are redacted;
- no gate still contains `NOT_RUN` or `BLOCKED`.
- the gate-specific evaluator, not the probe, computed the stored status from the recorded measurement.

- [ ] **Step 7: Generate final documents.**

`go-no-go.md` must list each gate, status, exact evidence hash, observed metric/capability and any return-to-design reason. It must say `GO` only if `tools/phase0/evidence.py verify` exits `0`.

Run:

```bash
uv run tools/phase0/evidence.py verify \
  --report build/phase0/final-report.json
git diff --check
rg -n 'TO[D]O|T[B]D|FIXM[E]|fill[ -]?in|implement[ ]later' \
  protocol firmware companion web-probe tools scripts docs/validation
```

Expected: verifier exit `0` only for six real passes; formatting check clean; the scan finds no unresolved implementation markers.

- [ ] **Step 8: Apply the phase decision.**

- If `GO`: mark Phase 0 achieved in the progress document and begin a separate Phase 1 implementation-planning session.
- If `NO_GO`: mark the exact failing or blocked gates, stop product implementation and open design review. Do not weaken requirements or add private Codex-file workarounds.

- [ ] **Step 9: Run full verification and commit the evidence summary.**

```bash
uv run python -m unittest discover -s tests/phase0 -v
uv run pytest tools -q
swift test --package-path companion
ctest --test-dir build/phase0/host-tests --output-on-failure
scripts/phase0/npm.sh --prefix web-probe test
scripts/phase0/idf.sh -C firmware build
git diff --check
git add docs/validation/phase0 docs/2026-07-24-cardputer-codex-companion_PROGRESS.md
git commit -m "docs: record phase zero feasibility decision"
```

Expected: automated suites pass; the committed documents match the verified sanitized report. Raw evidence, keys, device backups and generated binaries remain ignored.

## Execution Stop Conditions

Stop immediately and return to design review when:

- macOS cannot prove HID and encrypted GATT belong to the same physical Cardputer;
- any required application cannot receive exact Chinese text without clipboard fallback;
- signed-app TCC continuity cannot be demonstrated;
- current official App Server lacks a required action or safe Approval/Input field;
- the final signed image exceeds `0x360000`;
- any heap, largest-block, stack, allocation, HID sample-count or p95 threshold fails;
- TLS exporter is unavailable through public APIs;
- the dual-key release trust record cannot satisfy the approved trust-chain meaning;
- unsigned or wrongly signed USB recovery is accepted;
- dedicated security hardware is unavailable for the irreversible proof.

No stop condition authorizes an internal Codex database/JSONL workaround, a public-network service, relaxed authentication, reduced Unicode scope, removed Web controls or a silent hardware change.
