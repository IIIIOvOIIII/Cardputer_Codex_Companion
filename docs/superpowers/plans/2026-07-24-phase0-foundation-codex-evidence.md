# Phase 0 Foundation, Codex Contract, and Evidence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立可复现工具链、固定安全协议 fixture、以字段级规则审计当前 Codex App Server，并从原始测量独立重算六项 Phase 0 gate，杜绝探针自报 `PASS`。

**Architecture:** 仓库内只保存锁文件、测试、schema 和脱敏摘要；工具、原始 schema、设备日志和密钥位于被忽略的 `build/phase0/`。`codex` 探针只使用官方 schema 生成器和只读 stdio 协议。最终器读取测量而不是状态，依次调用六个 gate evaluator；`--host-only` 明确将 HIL check 标为 `BLOCKED`。

**Tech Stack:** Python `3.11.11`、uv、标准库 `unittest/json/subprocess/hashlib`、`cryptography==45.0.6`、`jsonschema[format-nongpl]==4.26.0`、`pyserial==3.5`、`check-jsonschema==0.37.4`、ESP-IDF `v5.5.4`、Node `22.14.0`、当前安装的 Swift/Codex CLI。

## Global Constraints

- Parent plan: [`2026-07-24-cardputer-codex-companion-phase0-feasibility.md`](2026-07-24-cardputer-codex-companion-phase0-feasibility.md)。
- 本子计划只负责 foundation、Codex capability 和 evidence/finalizer；固件、macOS、Web/Release 分别按相邻子计划执行。
- 任何进入 gate evaluator 的规范化 measurement 中，`status`、`reported_status` 或 `overall_status` 都视为无效字段；状态只能由 evaluator 生成。子计划原始报告中的操作级 `status` 仅作为被校验的原始事实，必须先经 adapter 转换为无 verdict 字段的 measurement，不能直接进入 finalizer。
- Codex 现场报告不得保存 thread/session ID、标题、cwd、prompt、item 内容或工具输出。
- 当前 Codex CLI 版本是被测对象，不由本计划静默升级或降级。
- 所有 commit 均为本地 commit；仓库没有 remote。

---

## File Structure

```text
.gitignore
.nvmrc
.python-version
pyproject.toml
uv.lock
toolchain.lock.json
scripts/
  __init__.py
  phase0/
    __init__.py
    bootstrap_toolchain.sh
    idf.sh
    node.sh
    npm.sh
    check_toolchain.py
    generate_app_server_schema.sh
    run_app_server_capability_probe.sh
    run_phase0.sh
protocol/phase0/
  README.md
  gates.json
  producer-map.json
  phase0-report.schema.json
  codex-capability.schema.json
  codex-capability-requirements.json
  pairing-v1.md
  gatt-auth-v1.md
  wss-auth-v1.md
  fixtures/
    pairing-v1.json
    gatt-auth-v1.json
    wss-auth-v1.json
tools/
  app_server_probe/
    __init__.py
    audit.py
    live_probe.py
    tests/
      test_audit.py
      test_live_probe.py
      fixtures/
  phase0/
    __init__.py
    adapters.py
    evidence.py
    run_phase0.py
    generate_security_vectors.py
    tests/
      test_adapters.py
      test_evidence.py
tests/phase0/
  test_toolchain.py
  test_protocol_fixtures.py
  test_gate_report.py
  test_finalize_report.py
docs/validation/phase0/
  README.md
  gate-matrix.json
  app-server-capability-matrix.md
  go-no-go.md
```

## Task 1: Pin and Bootstrap the Repo-Local Toolchain

**Files:**

- Modify: `.gitignore`
- Create: `.nvmrc`, `.python-version`, `pyproject.toml`, `uv.lock`, `toolchain.lock.json`
- Create: `scripts/__init__.py`, `scripts/phase0/__init__.py`
- Create: `scripts/phase0/bootstrap_toolchain.sh`, `scripts/phase0/idf.sh`, `scripts/phase0/node.sh`, `scripts/phase0/npm.sh`, `scripts/phase0/check_toolchain.py`
- Create: `tests/phase0/test_toolchain.py`

**Interfaces:**

- Consumes: arm64 macOS, `git`, `curl`, `shasum`, `uv`, installed `swift` and `codex`.
- Produces: `.tools/esp-idf`, `.tools/espressif`, `.tools/node-v22.14.0-darwin-arm64`, uv Python and `build/phase0/toolchain.json`.

- [ ] **Step 1: Write the failing lock validator test.**

```python
import unittest

from scripts.phase0.check_toolchain import validate_lock


class ToolchainTest(unittest.TestCase):
    def test_exact_phase0_lock(self) -> None:
        lock = {
            "esp_idf": {
                "tag": "v5.5.4",
                "commit": "735507283d5b2f9fb363a1901172dbd9e847945d",
            },
            "node": {
                "version": "22.14.0",
                "sha256": "e9404633bc02a5162c5c573b1e2490f5fb44648345d64a958b17e325729a5e42",
            },
            "python": {"version": "3.11.11"},
        }
        self.assertEqual([], validate_lock(lock))

    def test_moved_idf_tag_is_rejected(self) -> None:
        lock = {
            "esp_idf": {"tag": "v5.5.4", "commit": "wrong"},
            "node": {"version": "22.14.0", "sha256": "wrong"},
            "python": {"version": "3.11.11"},
        }
        self.assertIn("esp_idf.commit", validate_lock(lock))
```

- [ ] **Step 2: Run RED.**

```bash
python3 -m unittest tests/phase0/test_toolchain.py -v
```

Expected: `ModuleNotFoundError` for `scripts.phase0.check_toolchain`.

- [ ] **Step 3: Implement the exact validator and lock.**

```python
EXPECTED = {
    "esp_idf.tag": "v5.5.4",
    "esp_idf.commit": "735507283d5b2f9fb363a1901172dbd9e847945d",
    "node.version": "22.14.0",
    "node.sha256": "e9404633bc02a5162c5c573b1e2490f5fb44648345d64a958b17e325729a5e42",
    "python.version": "3.11.11",
}


def validate_lock(lock: dict[str, object]) -> list[str]:
    failures: list[str] = []
    for dotted, expected in EXPECTED.items():
        section, key = dotted.split(".", 1)
        actual = lock.get(section, {})
        value = actual.get(key) if isinstance(actual, dict) else None
        if value != expected:
            failures.append(dotted)
    return failures
```

`bootstrap_toolchain.sh` must:

1. resolve the repository with `git rev-parse --show-toplevel`;
2. clone ESP-IDF tag `v5.5.4` recursively into `.tools/esp-idf`;
3. compare `git rev-parse HEAD` with the pinned commit;
4. set `IDF_TOOLS_PATH="$repo/.tools/espressif"`;
5. install uv Python `3.11.11` under `.tools/uv-python`;
6. run ESP-IDF `install.sh esp32s3` with that Python;
7. download the pinned Node archive and verify SHA-256 before extraction.

Wrappers must use absolute repo-derived paths:

```bash
#!/usr/bin/env bash
set -euo pipefail
repo_root="$(git rev-parse --show-toplevel)"
export IDF_TOOLS_PATH="$repo_root/.tools/espressif"
. "$repo_root/.tools/esp-idf/export.sh" >/dev/null
exec idf.py "$@"
```

- [ ] **Step 4: Lock Python dependencies and generated state.**

Use the parent plan's exact `pyproject.toml` (including pinned `jsonschema[format-nongpl]`, `pyserial` and `check-jsonschema`), run `uv lock`, and ignore:

```text
.tools/
build/
companion/.build/
.swiftpm/
web-probe/node_modules/
*.sqlite
*.app/
```

- [ ] **Step 5: Run GREEN and bootstrap verification.**

```bash
uv lock
python3 -m unittest tests/phase0/test_toolchain.py -v
scripts/phase0/bootstrap_toolchain.sh
scripts/phase0/check_toolchain.py --json build/phase0/toolchain.json
```

Expected: test passes; JSON records exact IDF/Python/Node plus observed Swift/macOS/Codex versions. Missing codesigning identity or HIL application is a named prerequisite with state `BLOCKED`, not a toolchain exception.

- [ ] **Step 6: Commit.**

```bash
git add .gitignore .nvmrc .python-version pyproject.toml uv.lock toolchain.lock.json scripts/__init__.py scripts/phase0 tests/phase0/test_toolchain.py
git commit -m "chore: bootstrap phase zero toolchain"
```

## Task 2: Fix Protocol Encodings and Deterministic Security Vectors

**Files:**

- Create: `protocol/phase0/README.md`, `protocol/phase0/pairing-v1.md`, `protocol/phase0/gatt-auth-v1.md`, `protocol/phase0/wss-auth-v1.md`
- Create: `protocol/phase0/fixtures/pairing-v1.json`, `protocol/phase0/fixtures/gatt-auth-v1.json`, `protocol/phase0/fixtures/wss-auth-v1.json`
- Create: `tools/phase0/__init__.py`, `tools/phase0/generate_security_vectors.py`
- Create: `tests/phase0/test_protocol_fixtures.py`

**Interfaces:**

- Consumes: fixed test-only P-256 private scalars, public identities, nonces, connection IDs, counter, fragment and TLS exporter bytes.
- Produces: canonical hex plus SHA-256/HKDF/HMAC/ECDSA values for Swift CryptoKit and firmware mbedTLS.

- [ ] **Step 1: Write RED fixture invariants.**

```python
import hashlib
import json
from pathlib import Path
import unittest


class SecurityVectorTest(unittest.TestCase):
    def test_pairing_fixture_is_self_consistent(self) -> None:
        fixture = json.loads(
            Path("protocol/phase0/fixtures/pairing-v1.json").read_text()
        )
        transcript = bytes.fromhex(fixture["transcript_hex"])
        self.assertEqual(
            fixture["transcript_sha256"],
            hashlib.sha256(transcript).hexdigest(),
        )
        self.assertNotEqual(fixture["pairing_root_hex"], fixture["gatt_auth_hex"])
        self.assertRegex(fixture["sas"], r"^[0-9]{6}$")

    def test_gatt_tag_is_16_bytes(self) -> None:
        fixture = json.loads(
            Path("protocol/phase0/fixtures/gatt-auth-v1.json").read_text()
        )
        self.assertEqual(32, len(fixture["tag_hex"]))
```

- [ ] **Step 2: Run RED.**

```bash
uv run python -m unittest tests/phase0/test_protocol_fixtures.py -v
```

Expected: missing fixture files.

- [ ] **Step 3: Implement canonical encoders.**

```python
def lp16(value: bytes) -> bytes:
    if len(value) > 0xFFFF:
        raise ValueError("length exceeds uint16")
    return len(value).to_bytes(2, "big") + value


def pairing_transcript(fields: PairingFields) -> bytes:
    return b"CCP-PAIR" + b"\x00\x01" + b"".join((
        lp16(fields.device_id.encode()),
        lp16(fields.companion_instance_id.encode()),
        lp16(fields.protocol_version.encode()),
        fields.device_identity_sec1,
        fields.companion_identity_sec1,
        fields.device_ephemeral_sec1,
        fields.companion_ephemeral_sec1,
        fields.device_nonce,
        fields.companion_nonce,
    ))
```

The docs must fix the field sizes/ordering, three distinct HKDF labels, GATT HMAC-SHA256 truncated to 16 bytes, 32-entry window, Companion-generated 128-bit connection ID, and fixed-width P-256 `r || s` WSS signature over the TLS exporter tuple. TLS exporter derivation uses label `EXPORTER-Cardputer-Codex-Companion-v1`, exactly 32 output bytes and an explicitly empty context (`use_context=0` in mbedTLS; zero context bytes in Network.framework); the WSS fixture records `exporter_context_hex=""`, and both language tests assert it. Six-digit SAS uses HKDF-SHA256 with info `cardputer-codex/sas/v1 || uint32_be(attempt)` and 4-byte output for attempts `0..255`; interpret each output as an unsigned big-endian word, accept only values below `4_294_000_000`, then render `word % 1_000_000` with leading zeroes. Exhaustion is an error. The post-SAS bond is accepted only when the same fresh 32-byte bind challenge arrives through authenticated WSS and encrypted GATT.

- [ ] **Step 4: Generate and independently hash fixtures.**

```bash
uv run tools/phase0/generate_security_vectors.py \
  --write protocol/phase0/fixtures \
  --emit-transcript build/phase0/transcript.bin
openssl dgst -sha256 build/phase0/transcript.bin
uv run python -m unittest tests/phase0/test_protocol_fixtures.py -v
```

Expected: OpenSSL digest equals `transcript_sha256`; Python tests pass. Mutating role, identity, nonce, version, exporter, SPKI, counter, connection ID or payload changes the derived output or produces the defined rejection.

- [ ] **Step 5: Commit.**

```bash
git add protocol/phase0 tools/phase0/generate_security_vectors.py tests/phase0/test_protocol_fixtures.py
git commit -m "docs: fix phase zero protocol vectors"
```

## Task 3: Audit Codex App Server at Method and Required-Field Level

**Files:**

- Create: `protocol/phase0/codex-capability-requirements.json`
- Create: `protocol/phase0/codex-capability.schema.json`
- Create: `tools/app_server_probe/__init__.py`, `tools/app_server_probe/audit.py`, `tools/app_server_probe/live_probe.py`
- Create: `tools/app_server_probe/tests/test_audit.py`, `tools/app_server_probe/tests/test_live_probe.py`
- Create: `tools/app_server_probe/tests/fixtures/complete/`, `missing_available_decisions/`, `missing_network_port/`, `experimental_input/`
- Create: `scripts/phase0/generate_app_server_schema.sh`, `scripts/phase0/run_app_server_capability_probe.sh`
- Create: `docs/validation/phase0/app-server-capability-matrix.md`

**Interfaces:**

- Consumes: stable/experimental JSON schema directories plus read-only stdio `initialize` and `thread/list`.
- Produces: a schema-valid capability record containing `git_commit`, `toolchain_manifest_sha256`, Codex/schema hashes, `present`, `required_fields`, `stability`, `live_result` and evidence paths; no Codex content or gate verdict.

- [ ] **Step 1: Write RED semantic tests.**

```python
import unittest

from tools.app_server_probe.audit import audit_capabilities


class CapabilityAuditTest(unittest.TestCase):
    def test_method_without_available_decisions_fails(self) -> None:
        result = audit_capabilities(
            schema_root="tools/app_server_probe/tests/fixtures/missing_available_decisions",
            requirements_path="protocol/phase0/codex-capability-requirements.json",
        )
        self.assertEqual("FAIL", result["approval.exec"]["state"])
        self.assertIn("availableDecisions", result["approval.exec"]["missing_fields"])

    def test_network_without_port_fails(self) -> None:
        result = audit_capabilities(
            schema_root="tools/app_server_probe/tests/fixtures/missing_network_port",
            requirements_path="protocol/phase0/codex-capability-requirements.json",
        )
        self.assertEqual("FAIL", result["approval.network"]["state"])

    def test_experimental_input_requires_capability(self) -> None:
        result = audit_capabilities(
            schema_root="tools/app_server_probe/tests/fixtures/experimental_input",
            requirements_path="protocol/phase0/codex-capability-requirements.json",
        )
        self.assertTrue(result["input.free_text"]["requires_experimental_api"])
```

- [ ] **Step 2: Run RED.**

```bash
uv run python -m unittest discover -s tools/app_server_probe/tests -v
```

Expected: `ModuleNotFoundError` for the audit module.

- [ ] **Step 3: Encode requirements as exact semantic records.**

Each requirement uses this shape:

```json
{
  "capability_id": "approval.network",
  "method": "item/networkAccess/requestApproval",
  "required_fields": [
    "threadId",
    "turnId",
    "itemId",
    "requestId",
    "networkApprovalContext.host",
    "networkApprovalContext.port",
    "networkApprovalContext.protocol",
    "reason",
    "availableDecisions"
  ],
  "allowed_stability": ["stable"]
}
```

Include all thread/list/start/resume, turn/start/interrupt, compact start/lifecycle, command/file/network/permission Approval, option/free-text Input, request IDs/state version and lifecycle events from the parent plan. Experimental Input explicitly allows `experimental` only with `initialize.capabilities.experimentalApi=true`.

- [ ] **Step 4: Implement local `$ref` resolution and required-field search.**

```python
def resolve_local_ref(root: dict, ref: str) -> dict:
    if not ref.startswith("#/"):
        raise ValueError("only local schema refs are accepted")
    node: object = root
    for token in ref[2:].split("/"):
        key = token.replace("~1", "/").replace("~0", "~")
        if not isinstance(node, dict) or key not in node:
            raise KeyError(ref)
        node = node[key]
    if not isinstance(node, dict):
        raise TypeError(ref)
    return node


def method_variants(node: dict, root: dict) -> list[dict]:
    if "$ref" in node:
        return method_variants(resolve_local_ref(root, node["$ref"]), root)
    variants: list[dict] = []
    method = node.get("properties", {}).get("method", {}).get("const")
    if isinstance(method, str):
        variants.append(node)
    for key in ("oneOf", "anyOf", "allOf"):
        for child in node.get(key, []):
            variants.extend(method_variants(child, root))
    return variants
```

For each matched method, recursively collect declared properties and `required` membership through local refs. A field passes only when every non-null applicable variant defines the safe field shape. Method existence alone never passes.

- [ ] **Step 5: Implement a content-denying live probe.**

The probe writes exactly:

```json
{"id":1,"method":"initialize","params":{"clientInfo":{"name":"cardputer-phase0-readonly-probe","version":"0.0.1"},"capabilities":{"experimentalApi":true}}}
{"method":"initialized"}
{"id":2,"method":"thread/list","params":{"limit":1}}
```

Before disk, retain only:

```python
ALLOWED_TOP_LEVEL = frozenset({"id", "method", "error", "result_type"})


def redact_response(message: dict) -> dict:
    result = {key: message[key] for key in ALLOWED_TOP_LEVEL if key in message}
    raw_result = message.get("result")
    if isinstance(raw_result, dict):
        result["result_keys"] = sorted(raw_result)
        result["item_count"] = len(raw_result.get("data", []))
    return result
```

The test injects sentinel title, cwd, prompt and item strings and asserts none appears in serialized output.

- [ ] **Step 6: Generate current schemas and run GREEN.**

```bash
scripts/phase0/generate_app_server_schema.sh build/phase0/app-server
scripts/phase0/run_app_server_capability_probe.sh \
  --schema-root build/phase0/app-server \
  --requirements protocol/phase0/codex-capability-requirements.json \
  --output build/phase0/app-server/capability.json
uv run python -m unittest discover -s tools/app_server_probe/tests -v
```

Expected: unit tests pass. The live report records current `codex --version`, schema hashes, method/field states and no content. Any missing `availableDecisions`, network port, safe permission response or required Input field yields `FAIL` for Gate 3.

- [ ] **Step 7: Commit.**

```bash
git add protocol/phase0/codex-capability-requirements.json protocol/phase0/codex-capability.schema.json tools/app_server_probe scripts/phase0/generate_app_server_schema.sh scripts/phase0/run_app_server_capability_probe.sh docs/validation/phase0/app-server-capability-matrix.md
git commit -m "test: audit codex app server contract"
```

## Task 4: Recompute Gate Statuses from Measurements

**Files:**

- Create: `protocol/phase0/gates.json`, `protocol/phase0/producer-map.json`, `protocol/phase0/phase0-report.schema.json`
- Create: `tools/phase0/adapters.py`, `tools/phase0/tests/test_adapters.py`
- Create: `tools/phase0/evidence.py`, `tools/phase0/tests/test_evidence.py`
- Create: `tests/phase0/test_gate_report.py`
- Create: `docs/validation/phase0/README.md`, `docs/validation/phase0/gate-matrix.json`

**Interfaces:**

- Consumes: five child raw reports covering all six gates through schema-specific adapters, their raw evidence hashes, tested Git/firmware/toolchain identities and mode (`host-only` or HIL).
- Produces: evaluator-created checks/statuses and final `GO` only when six gates pass.

- [ ] **Step 1: Write RED anti-forgery tests.**

```python
import unittest

from tools.phase0.evidence import EvidenceError, finalize_report


class EvidenceTest(unittest.TestCase):
    def test_claimed_pass_cannot_hide_hid_loss(self) -> None:
        inputs = complete_inputs()
        inputs["measurements"]["gate5"]["reported_status"] = "PASS"
        inputs["measurements"]["gate5"]["hid"]["generated"] = 10_000
        inputs["measurements"]["gate5"]["hid"]["queued"] = 9_999
        with self.assertRaises(EvidenceError):
            finalize_report(inputs)

    def test_host_only_cannot_satisfy_hil(self) -> None:
        inputs = complete_inputs()
        inputs["mode"] = "host-only"
        report = finalize_report(inputs)
        self.assertEqual("BLOCKED", report.gates["P0-G1-CONCURRENCY"].status)

    def test_wrong_firmware_hash_is_rejected(self) -> None:
        inputs = complete_inputs()
        inputs["measurements"]["gate1"]["probe_firmware_sha256"] = "0" * 64
        with self.assertRaises(EvidenceError):
            finalize_report(inputs)
```

- [ ] **Step 2: Run RED.**

```bash
uv run python -m unittest discover -s tools/phase0/tests -v
uv run python -m unittest tests/phase0/test_gate_report.py -v
```

Expected: missing `evidence` implementation.

- [ ] **Step 3: Fix every required check and evaluator in `gates.json`.**

Each check declares:

```json
{
  "check_id": "g5.hid.p95",
  "evidence_kind": "target_hil",
  "evaluator": "hid_latency",
  "minimum_samples": 10000,
  "maximum_p95_us": 20000
}
```

Gate definitions contain no mutable status. Required check IDs match the normalized adapter outputs exactly.

`producer-map.json` is the only assembly contract:

| Gate | Check ID | Producer raw schema/path | Adapter |
|---|---|---|---|
| G1 | `g1.concurrent_services` | `firmware-concurrency-report.schema.json` / `build/phase0/firmware-concurrency/report.json` | `adapt_firmware_concurrency` |
| G1 | `g1.same_physical_device` | firmware concurrency report with same-window Companion event | `adapt_firmware_concurrency` |
| G2 | `g2.five_app_unicode` | `macos-hil.schema.json` / same raw file | `adapt_macos_hil` |
| G2 | `g2.focus_secureinput_crash_tcc` | `macos-hil.schema.json` / same raw file | `adapt_macos_hil` |
| G3 | `g3.codex_contract` | `codex-capability.schema.json` / `build/phase0/app-server/capability.json` | `adapt_codex_capability` |
| G4 | `g4.signed_image_budget` | `release-image-budget.schema.json` / `build/phase0/release-image-budget.json` | `adapt_release_budget` |
| G4 | `g4.real_web_cjk_assets` | same release budget record | `adapt_release_budget` |
| G5 | `g5.heap_and_largest_block` | firmware concurrency report | `adapt_firmware_concurrency` |
| G5 | `g5.stack_and_allocations` | firmware concurrency report | `adapt_firmware_concurrency` |
| G5 | `g5.hid_p95_under_attack` | firmware concurrency report | `adapt_firmware_concurrency` |
| G6 | `g6.web_precondition` | firmware concurrency report result `P0-G6-WEB-PRECONDITION` facts | `adapt_firmware_concurrency` |
| G6 | `g6.sas_wss_gatt` | macOS HIL raw facts | `adapt_macos_hil` |
| G6 | `g6.release_security` | `release-security-evidence.schema.json` / `build/phase0/security-hil/raw.json` | `adapt_release_security` |
| G6 | `g6.signed_usb_recovery` | same release-security record | `adapt_release_security` |

The map fixes schema URI, adapter name, default path pattern, required artifact hashes and gate/check membership. A glob resolving to zero or more than one run is `BLOCKED`; the runner never chooses the “latest” report. `P0-G6-WEB-PRECONDITION` is an intermediate producer result only and cannot appear as a seventh final gate.

- [ ] **Step 4: Implement schema adapters, strict normalized-input rejection and Gate 5 calculation.**

Each adapter first validates the child report and referenced artifact hashes, then copies only typed measurements needed by `gates.json`. It must discard child `overall_status`, result summaries and prose. Operation-level outcomes such as Unicode `completed/partial/failed` are converted into explicit booleans and byte counts (`exact_match`, `posted_bytes`, `verified_bytes`, `secure_input_detected`); no key named `status`, `reported_status` or `overall_status` survives normalization. Mutation tests must prove that changing a child summary without changing raw facts cannot change a gate, while changing a raw fact does.

```python
ALLOWED_INPUT_KEYS = frozenset({
    "schema_version",
    "mode",
    "git_commit",
    "git_tree_clean",
    "probe_firmware_sha256",
    "release_firmware_sha256",
    "toolchain_manifest_sha256",
    "started_at",
    "ended_at",
    "measurements",
    "evidence",
})


def reject_status_fields(value: object, path: str = "$") -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            if key in {"status", "reported_status", "overall_status"}:
                raise EvidenceError(f"untrusted status at {path}.{key}")
            reject_status_fields(child, f"{path}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            reject_status_fields(child, f"{path}[{index}]")


def nearest_rank_p95_us(buckets: list[dict[str, int]], count: int) -> int:
    rank = (95 * count + 99) // 100
    seen = 0
    for bucket in buckets:
        seen += bucket["count"]
        if seen >= rank:
            return bucket["upper_us"]
    raise EvidenceError("histogram count is incomplete")


def evaluate_hid(value: dict) -> CheckResult:
    count = value["generated"]
    passed = (
        count >= 10_000
        and value["queued"] == count
        and value["queue_failures"] == 0
        and nearest_rank_p95_us(value["buckets"], count) <= 20_000
    )
    return CheckResult("PASS" if passed else "FAIL")
```

Implement separate Gate 1–6 functions. Gate 4 reparses `partitions.csv` and hashes the final signed binary. Gate 6 requires `hardware_kind="dedicated_security_device"` and rejects `virtual_efuse=true`.

- [ ] **Step 5: Run GREEN and mutation tests.**

```bash
uv run python -m unittest discover -s tools/phase0/tests -v
uv run python -m unittest tests/phase0/test_gate_report.py -v
```

Expected: all tests pass. Mutation cases for threshold minus one, sample count 9,999, missing application, missing negative security case, wrong probe/release firmware hash and probe-supplied verdict all return `FAIL/BLOCKED` or raise `EvidenceError`; child summary mutations alone never alter the recomputed result.

- [ ] **Step 6: Commit.**

```bash
git add protocol/phase0/gates.json protocol/phase0/producer-map.json protocol/phase0/phase0-report.schema.json tools/phase0/adapters.py tools/phase0/tests/test_adapters.py tools/phase0/evidence.py tools/phase0/tests/test_evidence.py tests/phase0/test_gate_report.py docs/validation/phase0
git commit -m "test: derive phase zero gate decisions"
```

## Task 5: Host-Only/Full Runner and Final GO/NO-GO

**Files:**

- Create: `tools/phase0/run_phase0.py`, `scripts/phase0/run_phase0.sh`
- Create: `tests/phase0/test_finalize_report.py`
- Update: `docs/validation/phase0/gate-matrix.json`
- Create: `docs/validation/phase0/go-no-go.md`
- Update: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`

**Interfaces:**

- Consumes: five explicit child-report paths from `producer-map.json`, explicit mode, run ID, exact Cardputer port and Companion interface in HIL mode.
- Produces: raw manifest, sanitized final report and documents whose decision is identical to `evidence.finalize_report`.

- [ ] **Step 1: Write RED runner-mode tests.**

```python
import unittest

from tools.phase0.run_phase0 import parse_args


class RunnerArgsTest(unittest.TestCase):
    def test_host_only_needs_no_hardware(self) -> None:
        args = parse_args([
            "--host-only",
            "--run-id", "20260724T010000Z",
            "--evidence-root", "build/phase0",
        ])
        self.assertTrue(args.host_only)

    def test_full_hil_requires_port_and_interface(self) -> None:
        with self.assertRaises(SystemExit):
            parse_args([
                "--run-id", "20260724T010000Z",
                "--evidence-root", "build/phase0",
            ])
```

- [ ] **Step 2: Run RED.**

```bash
uv run python -m unittest tests/phase0/test_finalize_report.py -v
```

Expected: missing runner module.

- [ ] **Step 3: Implement explicit mode parsing and child result collection.**

```python
def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host-only", action="store_true")
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--evidence-root", required=True)
    parser.add_argument("--cardputer-port")
    parser.add_argument("--companion-interface")
    parser.add_argument("--firmware-concurrency-report")
    parser.add_argument("--macos-hil-report")
    parser.add_argument("--codex-capability-report")
    parser.add_argument("--release-budget-report")
    parser.add_argument("--release-security-report")
    args = parser.parse_args(argv)
    hil_required = (
        "cardputer_port",
        "companion_interface",
        "firmware_concurrency_report",
        "macos_hil_report",
        "codex_capability_report",
        "release_budget_report",
        "release_security_report",
    )
    if not args.host_only and any(
        not getattr(args, name) for name in hil_required
    ):
        parser.error("full HIL requires exact hardware and child-report paths")
    return args
```

The runner invokes host tests and the Codex audit. In host-only mode it emits measurements only for host checks; evaluator-required HIL evidence is absent and therefore `BLOCKED`. In full mode it accepts no globs and no implicit “latest” selection: every explicit path must match the producer map/schema, then adapters remove verdict fields. All HIL records must have `git_tree_clean=true` and share the frozen `git_commit` (`HIL_BASE_COMMIT`) and toolchain-manifest hash; the firmware-concurrency record owns `probe_firmware_sha256`, while release-budget and release-security must share a distinct `release_firmware_sha256`. The macOS record must match the concurrency record's `probe_firmware_sha256`, runtime `app_elf_sha256` and probe device-ID digest; only same-window Gate 1 records must overlap in time. Development and release image hashes are expected to differ and must never be compared as one field.

- [ ] **Step 4: Run host-only GREEN.**

```bash
scripts/phase0/run_phase0.sh \
  --host-only \
  --run-id "$(date -u +%Y%m%dT%H%M%SZ)" \
  --evidence-root build/phase0
uv run tools/phase0/evidence.py verify \
  --report build/phase0/final-report.json
```

Expected: runner completes and writes `NO_GO` with HIL gates `BLOCKED`; verifier exits non-zero because the report is correctly not `GO`.

- [ ] **Step 5: Define the full-HIL call without guessed targets.**

```bash
scripts/phase0/run_phase0.sh \
  --cardputer-port "$CARDPUTER_PORT" \
  --companion-interface "$COMPANION_INTERFACE" \
  --firmware-concurrency-report build/phase0/firmware-concurrency/report.json \
  --macos-hil-report "build/phase0/macos-hil/$MACOS_HIL_RUN_ID/raw.json" \
  --codex-capability-report build/phase0/app-server/capability.json \
  --release-budget-report build/phase0/release-image-budget.json \
  --release-security-report build/phase0/security-hil/raw.json \
  --run-id "$(date -u +%Y%m%dT%H%M%SZ)" \
  --evidence-root build/phase0
```

Expected: empty variables are rejected. With resolved prerequisites, the runner collects all child reports, recomputes all statuses and exits `0` only for six `PASS` gates.

`MACOS_HIL_RUN_ID` is the exact UUID supplied to the macOS HIL runner; the finalizer rejects a report whose JSON `run_id` does not equal its parent directory name.

- [ ] **Step 6: Generate documents from the verified report.**

`go-no-go.md` lists each check's measured value, threshold, evidence SHA-256 and evaluator result. It contains no hand-edited status. A `NO_GO` document names exact return-to-design reasons; a `GO` document links the same signed image/hash tested by flash budget and security HIL.

- [ ] **Step 7: Verify and commit.**

```bash
uv run python -m unittest discover -s tests/phase0 -v
uv run python -m unittest discover -s tools/app_server_probe/tests -v
uv run python -m unittest discover -s tools/phase0/tests -v
git diff --check
git add tools/phase0/run_phase0.py scripts/phase0/run_phase0.sh tests/phase0/test_finalize_report.py docs/validation/phase0 docs/2026-07-24-cardputer-codex-companion_PROGRESS.md
git commit -m "docs: record phase zero feasibility decision"
```

Expected: all tests pass; raw evidence remains ignored; committed decision matches a freshly recomputed report.
