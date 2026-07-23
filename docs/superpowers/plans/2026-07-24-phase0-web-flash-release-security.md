# Phase 0 Web, Flash, and Release Security Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用真实代表性 Web/CJK 资源证明 8MiB 双 OTA 预算，并在专用 Cardputer 上闭合 P-256 OTA manifest、RSA-3072 Secure Boot v2、Flash/NVS Encryption、应用级签名 USB 恢复和 Secure ROM 下载限制。

**Architecture:** Vue/Vite 的六页代表性 SPA 和 `CardputerCJK16` 字库在构建时生成确定性压缩资产并真正嵌入 app。Release probe 使用独立 `sdkconfig`/build directory；构建先产生完整 RSA-signed app，随后 P-256 manifest 绑定该完整文件的 SHA-256、Secure Boot RSA digest 和资产 manifests，并与 app 组成 release bundle。已接受 manifest 存入与 OTA slot 对应的加密 config slot，避免“镜像内含自身 hash”的循环。普通恢复不是 ROM 明文刷写：已认证 app 在物理启动手势下进入仅 USB CDC 的 recovery mode，通过 OTA API 写入非活动槽并在切换前再次验证两层签名。

**Tech Stack:** Vue `3.5.39`、TypeScript `5.9.3`、Vite `6.4.3`、Vitest `3.2.7`；Noto Sans CJK SC `Sans2.004`（OFL-1.1）；Python 3.11、fontTools/Pillow/cryptography；ESP-IDF `v5.5.4`、TinyUSB CDC、mbedTLS、Secure Boot v2 RSA-PSS、Flash/NVS Encryption、OTA rollback。

## Global Constraints

- Parent plan: [`2026-07-24-cardputer-codex-companion-phase0-feasibility.md`](2026-07-24-cardputer-codex-companion-phase0-feasibility.md)。
- 预算必须读取最终 RSA 签名 `.bin`；ELF section、未签名 image、原始 Vite 目录或字体源文件大小都不能产生 Gate 4 `PASS`。
- 字库源、许可证、字表和生成参数任一变化都必须改变资产 manifest。
- Release probe 只使用 `firmware/build/release-probe/` 和 `firmware/build/release-probe/sdkconfig`；不得复用开发 `firmware/sdkconfig` 或普通 build。
- Secure Boot RSA 测试钥匙和 P-256 manifest 测试钥匙彼此独立，只在被忽略的 `build/phase0/keys/`；真实发布私钥不进入此流程。
- eFuse/Flash Encryption Release 只允许在用户明确指定的专用安全测试机上执行，并需精确 chip ID 与永久变更确认。
- Secure ROM Download Mode 仍允许有限 flash write，不等于它会验证 P-256 manifest；因此正常 USB recovery 必须由 RSA-authenticated app 的设备侧 verifier 执行。
- 如果应用级 recovery + Secure ROM 限制无法满足批准的“签名 USB 恢复”语义，Gate 6 必须 `FAIL`，不得改成保留不受控 ROM 刷写。

---

## File Structure

```text
web-probe/
  package.json
  package-lock.json
  vite.config.ts
  src/
    App.vue
    routes.ts
    pages/
      StatusProbe.vue
      ProfilesProbe.vue
      KeymapProbe.vue
      MacrosProbe.vue
      PairingProbe.vue
      ImportExportProbe.vue
  tests/bundle.test.ts
assets/cjk/
  LICENSE-OFL-1.1.txt
  NOTICE.md
  noto-sans-cjk-sc.lock.json
  ui_strings_zh-Hans.txt
tools/
  cjk/
    build_cjk_pack.py
    tests/test_cjk_pack.py
  web/
    compress_assets.py
    embed_assets.py
    tests/test_compress_assets.py
  phase0/
    image_budget.py
    ota_manifest.py
    verify_release_config.py
    tests/
      test_image_budget.py
      test_ota_manifest.py
      test_verify_release_config.py
      test_release_security_hil.py
  recovery/
    verify_bundle.py
    flash_bundle.py
    protocol.py
    tests/
      test_recovery_bundle.py
      test_recovery_protocol.py
firmware/
  partitions.csv
  sdkconfig.release-probe.defaults
  main/
    generated/.gitkeep
    probe/
      cjk_font_pack.hpp
      cjk_font_pack.cpp
      recovery_policy.hpp
      recovery_policy.cpp
      usb_recovery.hpp
      usb_recovery.cpp
  test/host/
    test_cjk_font_pack.cpp
    test_recovery_policy.cpp
    test_usb_recovery.cpp
protocol/phase0/
  release-image-budget.schema.json
  release-efuse-plan.json
  release-trust-v1.md
  recovery-v1.md
  release-security-evidence.schema.json
scripts/phase0/
  build_release_probe.sh
  generate_test_release_keys.sh
  backup_flash.sh
  prepare_security_hil_approval.py
  run_security_hil.py
docs/validation/phase0/
  flash-budget.md
  security-hil.md
```

## Task 1: Build a Representative Six-Page Web Bundle

**Files:**

- Create: `web-probe/package.json`, `web-probe/package-lock.json`, `web-probe/vite.config.ts`
- Create: `web-probe/src/App.vue`, `web-probe/src/routes.ts`
- Create: `web-probe/src/pages/StatusProbe.vue`, `ProfilesProbe.vue`, `KeymapProbe.vue`, `MacrosProbe.vue`, `PairingProbe.vue`, `ImportExportProbe.vue`
- Create: `web-probe/tests/bundle.test.ts`
- Create: `tools/web/compress_assets.py`, `tools/web/embed_assets.py`, `tools/web/tests/test_compress_assets.py`

**Interfaces:**

- Consumes: six representative management views and production Vite output.
- Produces: local-only assets, deterministic `.gz` files and an ordered C embedding table with path/content-type/encoding/length/SHA-256.

- [ ] **Step 1: Write RED bundle coverage tests.**

```typescript
import { describe, expect, it } from "vitest";
import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { probeRoutes } from "../src/routes";

const required = [
  "status",
  "profiles",
  "keymap",
  "macros",
  "pairing",
  "import-export",
];

describe("representative bundle", () => {
  it("contains every management surface", () => {
    expect(probeRoutes.map((route) => route.id).sort()).toEqual(required.sort());
  });

  it("contains no external asset URL", () => {
    const files = readdirSync("dist/assets");
    const text = files
      .filter((name) => /\.(js|css)$/.test(name))
      .map((name) => readFileSync(join("dist/assets", name), "utf8"))
      .join("\n");
    expect(text).not.toMatch(/https?:\/\//);
    expect(text).not.toMatch(/sourceMappingURL/);
  });
});
```

Each page must render representative maximum-shape data: 8 profiles, 4 layers, physical key layout, 16-step macro, 1024-byte UTF-8 snippet, five Web clients, pairing status and 128KiB import progress. It does not implement product persistence.

- [ ] **Step 2: Run RED.**

```bash
scripts/phase0/npm.sh --prefix web-probe test
```

Expected: package/routes do not exist.

- [ ] **Step 3: Pin the Web package and route contract.**

`package.json` uses exact versions:

```json
{
  "private": true,
  "scripts": {
    "build": "vite build",
    "test": "vitest run"
  },
  "dependencies": {
    "vue": "3.5.39"
  },
  "devDependencies": {
    "@vitejs/plugin-vue": "5.2.4",
    "typescript": "5.9.3",
    "vite": "6.4.3",
    "vitest": "3.2.7"
  }
}
```

`vite.config.ts` sets `base: "./"`, `build.sourcemap: false`, stable asset names derived from content hash and no CDN/plugin network runtime.

Generate the committed lock once through the pinned Node wrapper, then use only `npm ci`:

```bash
scripts/phase0/npm.sh --prefix web-probe install \
  --package-lock-only --ignore-scripts
```

- [ ] **Step 4: Implement deterministic compression.**

```python
import gzip
import hashlib
from pathlib import Path


def compress_file(source: Path, destination: Path) -> dict[str, object]:
    content = source.read_bytes()
    with destination.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as stream:
            stream.write(content)
    compressed = destination.read_bytes()
    return {
        "source_sha256": hashlib.sha256(content).hexdigest(),
        "gzip_sha256": hashlib.sha256(compressed).hexdigest(),
        "source_bytes": len(content),
        "gzip_bytes": len(compressed),
    }
```

`embed_assets.py` sorts normalized URL paths, rejects `..`, absolute paths and duplicate routes, then writes a C byte array/table into a caller-supplied build directory. Development uses `firmware/build/generated/`; release uses only `firmware/build/release-probe/generated/`. Generated timestamps and source-tree generated files are prohibited.

- [ ] **Step 5: Run GREEN twice and compare hashes.**

```bash
scripts/phase0/npm.sh --prefix web-probe ci
scripts/phase0/npm.sh --prefix web-probe run build
scripts/phase0/npm.sh --prefix web-probe test
uv run tools/web/compress_assets.py \
  --input web-probe/dist \
  --output build/phase0/web-a
uv run tools/web/compress_assets.py \
  --input web-probe/dist \
  --output build/phase0/web-b
diff -r build/phase0/web-a build/phase0/web-b
```

Expected: tests pass, six pages are present, no external URLs/sourcemaps exist and both compressed trees are byte-identical.

- [ ] **Step 6: Commit.**

```bash
git add web-probe tools/web
git commit -m "feat: add representative embedded web bundle"
```

## Task 2: Generate the Licensed Flash-Resident CJK Pack

**Files:**

- Create: `assets/cjk/LICENSE-OFL-1.1.txt`, `assets/cjk/NOTICE.md`, `assets/cjk/noto-sans-cjk-sc.lock.json`, `assets/cjk/ui_strings_zh-Hans.txt`
- Create: `tools/cjk/build_cjk_pack.py`, `tools/cjk/tests/test_cjk_pack.py`
- Create: `firmware/main/probe/cjk_font_pack.hpp`, `firmware/main/probe/cjk_font_pack.cpp`
- Create: `firmware/test/host/test_cjk_font_pack.cpp`
- Modify: `firmware/main/CMakeLists.txt`, `firmware/test/host/CMakeLists.txt`

**Interfaces:**

- Consumes: pinned Noto Sans CJK SC OTF, GB2312 level-1 3,755 characters, UI strings and `U+FFFD`.
- Produces: `CardputerCJK16.cjkpack`, manifest and flash-only ordered lookup; ASCII remains the built-in font.

- [ ] **Step 1: Write RED binary-format tests.**

```python
from pathlib import Path
import hashlib

from tools.cjk.build_cjk_pack import build_pack, parse_pack


def test_pack_is_deterministic_and_complete(tmp_path: Path) -> None:
    first = build_pack(test_font(), "中文状态", tmp_path / "a.cjkpack")
    second = build_pack(test_font(), "态状文中", tmp_path / "b.cjkpack")
    assert first.read_bytes() == second.read_bytes()
    raw = first.read_bytes()
    parsed = parse_pack(raw)
    assert parsed.codepoints == sorted(set(parsed.codepoints))
    assert 0xFFFD in parsed.codepoints
    assert hashlib.sha256(raw[:-32]).digest() == raw[-32:]
    assert parsed.content_sha256 == raw[-32:].hex()
```

C++ host tests open the pack without copying its bitmap payload, find known codepoints and fall back to `U+FFFD`.

- [ ] **Step 2: Run RED.**

```bash
uv run pytest tools/cjk/tests/test_cjk_pack.py -q
cmake --build build/phase0/firmware-host
ctest --test-dir build/phase0/firmware-host -R cjk --output-on-failure
```

Expected: generator/reader sources are missing.

- [ ] **Step 3: Pin source and license.**

```json
{
  "family": "Noto Sans CJK SC",
  "version": "Sans2.004",
  "url": "https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf",
  "bytes": 16437364,
  "sha256": "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b",
  "license_sha256": "6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2",
  "generated_family": "CardputerCJK16"
}
```

The source downloads into `.tools/fonts/` and is rejected before parsing if byte length/hash differs. The generated family is renamed and distributed with the exact OFL license and NOTICE.

- [ ] **Step 4: Implement the fixed pack format.**

Format:

```text
8 bytes  magic "CCPCJK16"
2 bytes  version, big endian, value 1
2 bytes  glyph_count, big endian
32 bytes source OTF SHA-256
N × 8   sorted index: codepoint u32 + bitmap offset u32
N × 64  16×16 pixels, 2 bits per pixel, row-major
32 bytes SHA-256 of all previous pack bytes
```

Core packing:

```python
def pack_2bpp(grayscale: bytes) -> bytes:
    if len(grayscale) != 16 * 16:
        raise ValueError("glyph must be 16x16")
    output = bytearray()
    for offset in range(0, len(grayscale), 4):
        levels = [min(3, value * 4 // 256) for value in grayscale[offset:offset + 4]]
        output.append(
            (levels[0] << 6) | (levels[1] << 4) | (levels[2] << 2) | levels[3]
        )
    return bytes(output)
```

The glyph set is GB2312 level-1 3,755 plus all non-ASCII UI characters and `U+FFFD`, sorted/deduplicated. The firmware reader binary-searches the flash index and returns a span into mapped flash; it never copies the complete index/bitmap into SRAM.

- [ ] **Step 5: Run GREEN and size the actual asset.**

```bash
uv run tools/cjk/build_cjk_pack.py \
  --lock assets/cjk/noto-sans-cjk-sc.lock.json \
  --ui-strings assets/cjk/ui_strings_zh-Hans.txt \
  --output build/phase0/generated/CardputerCJK16.cjkpack \
  --manifest build/phase0/generated/CardputerCJK16.manifest.json
uv run pytest tools/cjk/tests/test_cjk_pack.py -q
ctest --test-dir build/phase0/firmware-host -R cjk --output-on-failure
```

Expected: exactly 3,755 base CJK codepoints plus required additions, deterministic pack/hash, licensed manifest and no runtime full-pack allocation.

- [ ] **Step 6: Commit.**

```bash
git add assets/cjk tools/cjk firmware/main/probe/cjk_font_pack.hpp firmware/main/probe/cjk_font_pack.cpp firmware/main/CMakeLists.txt firmware/test/host/test_cjk_font_pack.cpp firmware/test/host/CMakeLists.txt
git commit -m "feat: add licensed cjk flash pack"
```

## Task 3: Fix the 8MiB Partition Layout and Signed-Image Budget

**Files:**

- Create: `firmware/partitions.csv`
- Create: `protocol/phase0/release-image-budget.schema.json`
- Create: `tools/phase0/image_budget.py`, `tools/phase0/tests/test_image_budget.py`
- Update: `firmware/main/CMakeLists.txt`
- Create: `docs/validation/phase0/flash-budget.md`

**Interfaces:**

- Consumes: actual partition CSV, generated Web/CJK assets and final signed app binary.
- Produces: exact slot/free/max bytes and Gate 4 measurement tied to the signed image SHA-256.
- `release-image-budget.schema.json` requires `git_commit`, `git_tree_clean`, `toolchain_manifest_sha256`, `release_firmware_sha256`, partition-table SHA-256, Web/CJK manifest SHA-256 values and the raw byte counts used by the foundation adapter; complete evidence requires `git_tree_clean=true` and contains no gate verdict field.

- [ ] **Step 1: Write RED boundary tests.**

```python
from tools.phase0.image_budget import evaluate_image_budget


def test_exact_boundary_passes() -> None:
    result = evaluate_image_budget(0x360000, 0x3C0000)
    assert result.required_free_bytes == 0x60000
    assert result.remaining_bytes == 0x60000
    assert result.passed


def test_one_byte_over_fails() -> None:
    assert not evaluate_image_budget(0x360001, 0x3C0000).passed
```

- [ ] **Step 2: Run RED.**

```bash
uv run pytest tools/phase0/tests/test_image_budget.py -q
```

Expected: missing module.

- [ ] **Step 3: Add the fixed partition table.**

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

Partition-table offset is `0x12000`. Each config slot is 144KiB. Each OTA slot is 3,932,160 bytes; required free is 393,216 bytes and maximum signed image is 3,538,944 bytes.

- [ ] **Step 4: Implement exact arithmetic and CSV validation.**

```python
from dataclasses import dataclass


@dataclass(frozen=True)
class BudgetResult:
    slot_bytes: int
    signed_image_bytes: int
    required_free_bytes: int
    remaining_bytes: int
    passed: bool


def evaluate_image_budget(image_bytes: int, slot_bytes: int) -> BudgetResult:
    required = max((slot_bytes + 9) // 10, 256 * 1024)
    remaining = slot_bytes - image_bytes
    return BudgetResult(
        slot_bytes=slot_bytes,
        signed_image_bytes=image_bytes,
        required_free_bytes=required,
        remaining_bytes=remaining,
        passed=image_bytes >= 0 and remaining >= required,
    )
```

The parser rejects overlap, gaps beyond the explicit pre-OTA reserve, misaligned app offsets, end beyond `0x800000`, slot inequality, config slots below `0x24000` and a bootloader ending at/after `0x12000`.

- [ ] **Step 5: Embed real assets and run preliminary GREEN.**

`firmware/main/CMakeLists.txt` depends on `${CMAKE_BINARY_DIR}/generated/web_assets.c`, `${CMAKE_BINARY_DIR}/generated/web-assets.manifest.json` and `${CMAKE_BINARY_DIR}/generated/CardputerCJK16.cjkpack`; a missing generated asset is a build error. It never falls back to another build directory.

```bash
scripts/phase0/npm.sh --prefix web-probe run build
uv run tools/web/compress_assets.py --input web-probe/dist --output build/phase0/web
uv run tools/web/embed_assets.py --input build/phase0/web --output firmware/build/generated
uv run tools/cjk/build_cjk_pack.py \
  --lock assets/cjk/noto-sans-cjk-sc.lock.json \
  --ui-strings assets/cjk/ui_strings_zh-Hans.txt \
  --output firmware/build/generated/CardputerCJK16.cjkpack
scripts/phase0/idf.sh -C firmware build
uv run pytest tools/phase0/tests/test_image_budget.py -q
```

Expected: development image budget is reported only as preliminary. Gate 4 remains `NOT_RUN` until Task 4 supplies the independent final signed binary.

- [ ] **Step 6: Commit.**

```bash
git add firmware/partitions.csv firmware/main/CMakeLists.txt protocol/phase0/release-image-budget.schema.json tools/phase0/image_budget.py tools/phase0/tests/test_image_budget.py docs/validation/phase0/flash-budget.md
git commit -m "test: fix phase zero flash budget"
```

## Task 4: Build and Verify the Dual-Key Release Record

**Files:**

- Create: `protocol/phase0/release-trust-v1.md`
- Create: `protocol/phase0/release-efuse-plan.json`
- Create: `tools/phase0/ota_manifest.py`, `tools/phase0/tests/test_ota_manifest.py`
- Create: `tools/phase0/verify_release_config.py`, `tools/phase0/tests/test_verify_release_config.py`
- Create: `firmware/sdkconfig.release-probe.defaults`
- Create: `firmware/main/probe/recovery_policy.hpp`, `firmware/main/probe/recovery_policy.cpp`
- Modify: `firmware/main/app_main.cpp`, `firmware/main/CMakeLists.txt`
- Create: `scripts/phase0/generate_test_release_keys.sh`, `scripts/phase0/build_release_probe.sh`

**Interfaces:**

- Consumes: P-256 manifest test key, RSA-3072 Secure Boot test key, assets, fixed partitions and release metadata.
- Produces: a full independently configured RSA-signed image first, then a canonical external P-256-signed manifest, slot-config record and immutable release bundle that bind that exact image without a self-hash cycle.

- [ ] **Step 1: Write RED manifest/config tests.**

```python
from tools.phase0.ota_manifest import ReleaseManifest, verify_manifest
from tools.phase0.verify_release_config import verify_release_config


def test_changed_secure_boot_digest_fails(keys) -> None:
    manifest = ReleaseManifest.sample(
        secure_boot_key_digest="11" * 32,
        image_sha256="22" * 32,
    )
    signed = manifest.sign(keys.manifest_private)
    changed = signed.with_field("secure_boot_key_digest", "33" * 32)
    assert not verify_manifest(changed, keys.manifest_public)


def test_release_config_requires_every_security_switch(tmp_path) -> None:
    sdkconfig = tmp_path / "sdkconfig"
    sdkconfig.write_text("CONFIG_SECURE_BOOT=y\n")
    assert verify_release_config(sdkconfig).missing == {
        "secure_boot_v2",
        "flash_encryption_release",
        "nvs_encryption",
        "rollback",
        "secure_rom_download",
    }
```

- [ ] **Step 2: Run RED.**

```bash
uv run pytest \
  tools/phase0/tests/test_ota_manifest.py \
  tools/phase0/tests/test_verify_release_config.py -q
```

Expected: missing modules.

- [ ] **Step 3: Define canonical manifest bytes.**

```python
def canonical_bytes(fields: dict[str, object]) -> bytes:
    allowed = {
        "manifest_version",
        "hardware_model",
        "firmware_version",
        "secure_version",
        "protocol_min",
        "protocol_max",
        "config_schema_min",
        "config_schema_max",
        "image_length",
        "image_sha256",
        "secure_boot_key_digest",
        "web_assets_manifest_sha256",
        "cjk_manifest_sha256",
        "release_efuse_plan_sha256",
    }
    if set(fields) != allowed:
        raise ValueError("release manifest field set mismatch")
    return json.dumps(
        fields,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
```

P-256 signatures are fixed-width 64-byte `r || s`. The P-256-signed fields bind the full already-signed app bytes, the RSA public-key digest, both asset manifests and the exact eFuse plan. RSA-3072 Secure Boot signs bootloader/app. These are two private keys under one release trust record; a demand for one identical key/algorithm is impossible and produces Gate 6 `FAIL`.

`release-efuse-plan.json` is created here, before any release build, with the exact sorted allowlisted eFuse field/value set and expected pre/post states. Task 6 consumes this immutable file; it may not generate or rewrite the plan after the manifest is signed.

- [ ] **Step 4: Use an independent release configuration and build directory.**

`build_release_probe.sh` removes only the two exact generated roots `firmware/build/release-probe/` and `build/phase0/release-bundle/`, recreates the release build root, and runs:

```bash
scripts/phase0/npm.sh --prefix web-probe ci
scripts/phase0/npm.sh --prefix web-probe run build
uv run tools/web/compress_assets.py \
  --input web-probe/dist \
  --output firmware/build/release-probe/generated/web
uv run tools/web/embed_assets.py \
  --input firmware/build/release-probe/generated/web \
  --output firmware/build/release-probe/generated \
  --manifest firmware/build/release-probe/generated/web-assets.manifest.json
uv run tools/cjk/build_cjk_pack.py \
  --lock assets/cjk/noto-sans-cjk-sc.lock.json \
  --ui-strings assets/cjk/ui_strings_zh-Hans.txt \
  --output firmware/build/release-probe/generated/CardputerCJK16.cjkpack \
  --manifest firmware/build/release-probe/generated/CardputerCJK16.manifest.json
scripts/phase0/idf.sh -C firmware -B build/release-probe \
  -D SDKCONFIG=build/release-probe/sdkconfig \
  -D SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.release-probe.defaults' \
  reconfigure
scripts/phase0/idf.sh -C firmware -B build/release-probe build
uv run tools/phase0/ota_manifest.py create \
  --app firmware/build/release-probe/cardputer_codex_phase0.bin \
  --web-manifest firmware/build/release-probe/generated/web-assets.manifest.json \
  --cjk-manifest firmware/build/release-probe/generated/CardputerCJK16.manifest.json \
  --efuse-plan protocol/phase0/release-efuse-plan.json \
  --secure-boot-public-key build/phase0/keys/secure-boot-rsa-public.pem \
  --output firmware/build/release-probe/generated/release-manifest.json
uv run tools/phase0/ota_manifest.py sign \
  --manifest firmware/build/release-probe/generated/release-manifest.json \
  --private-key build/phase0/keys/manifest-p256-private.pem \
  --signature firmware/build/release-probe/generated/release-manifest.sig
uv run tools/phase0/ota_manifest.py assemble \
  --manifest firmware/build/release-probe/generated/release-manifest.json \
  --signature firmware/build/release-probe/generated/release-manifest.sig \
  --app firmware/build/release-probe/cardputer_codex_phase0.bin \
  --output build/phase0/release-bundle
```

The order is normative: generate release-local assets, build and RSA-sign the complete app, hash that immutable signed app plus both release-local asset manifests and eFuse plan, sign the external manifest, then assemble the bundle. `assemble` copies the exact app bytes to `build/phase0/release-bundle/app.bin`, creates a deterministic config-slot record containing manifest bytes/signature/slot metadata and writes a hash-indexed `bundle-index.json`; it rejects an existing/non-empty output or any post-manifest hash change. The script does not read `firmware/build/generated/`, a development `sdkconfig`, or artifacts from a prior release-probe directory.

`sdkconfig.release-probe.defaults` enables Secure Boot v2 signed binaries, RSA scheme, Flash Encryption Release, NVS Encryption, OTA rollback, partition table offset `0x12000`, 8MiB/no-PSRAM and Secure ROM Download Mode. `verify_release_config.py` parses the effective generated sdkconfig, bootloader/app signature blocks and bootloader end offset; defaults text alone is never evidence.

- [ ] **Step 5: Verify the active slot's external release record before normal services.**

```cpp
enum class ReleasePolicyResult {
  accepted,
  invalid_manifest_signature,
  wrong_model,
  wrong_secure_boot_digest,
  incompatible_schema,
  image_hash_mismatch,
};

ReleasePolicyResult verify_running_release(
    std::span<const uint8_t> canonical_manifest,
    std::span<const uint8_t, 64> p256_signature,
    const RunningImageFacts& facts) noexcept;
```

`app_main` maps the running OTA partition to exactly one encrypted `config_a`/`config_b` slot, reads the accepted manifest/signature record from that slot, validates `image_length` against the public ESP image parser, then streams exactly that many logical decrypted partition bytes through SHA-256 (including the Secure Boot signature block represented in `app.bin`) and calls this before BLE/Wi-Fi/Web/HID service initialization. It also compares the manifest's Web/CJK hashes with the embedded asset-manifest hashes. The app image contains only the pinned P-256 public key and expected Secure Boot RSA digest, never its own release manifest/hash. A missing, torn, wrong-slot or non-accepted config record exposes only recovery status over display/USB and never normal services.

- [ ] **Step 6: Run GREEN and final Gate 4 budget.**

```bash
scripts/phase0/generate_test_release_keys.sh build/phase0/keys
scripts/phase0/build_release_probe.sh
uv run tools/phase0/verify_release_config.py \
  --sdkconfig firmware/build/release-probe/sdkconfig \
  --bootloader firmware/build/release-probe/bootloader/bootloader.bin \
  --app firmware/build/release-probe/cardputer_codex_phase0.bin
uv run tools/phase0/ota_manifest.py verify-bundle \
  --bundle build/phase0/release-bundle \
  --manifest-public-key build/phase0/keys/manifest-p256-public.pem
uv run tools/phase0/image_budget.py \
  --partitions firmware/partitions.csv \
  --image build/phase0/release-bundle/app.bin \
  --json build/phase0/release-image-budget.json
```

Expected: effective security settings all pass; signed bootloader ends below `0x12000`; final signed app is at most `0x360000`; bundle verification proves `app.bin` equals the build output and the config record contains the signed manifest; the budget report's `release_firmware_sha256` is the same digest security HIL consumes.

- [ ] **Step 7: Commit.**

```bash
git add protocol/phase0/release-trust-v1.md protocol/phase0/release-efuse-plan.json tools/phase0/ota_manifest.py tools/phase0/tests/test_ota_manifest.py tools/phase0/verify_release_config.py tools/phase0/tests/test_verify_release_config.py firmware/sdkconfig.release-probe.defaults firmware/main/probe/recovery_policy.hpp firmware/main/probe/recovery_policy.cpp firmware/main/app_main.cpp firmware/main/CMakeLists.txt scripts/phase0/generate_test_release_keys.sh scripts/phase0/build_release_probe.sh
git commit -m "feat: bind release manifest to secure boot"
```

## Task 5: Implement the Signed Application USB Recovery Path

**Files:**

- Create: `protocol/phase0/recovery-v1.md`
- Create: `tools/recovery/protocol.py`, `tools/recovery/verify_bundle.py`, `tools/recovery/flash_bundle.py`
- Create: `tools/recovery/tests/test_recovery_bundle.py`, `tools/recovery/tests/test_recovery_protocol.py`
- Create: `firmware/main/probe/usb_recovery.hpp`, `firmware/main/probe/usb_recovery.cpp`
- Create: `firmware/test/host/test_recovery_policy.cpp`, `firmware/test/host/test_usb_recovery.cpp`
- Modify: `firmware/main/app_main.cpp`, `firmware/main/CMakeLists.txt`, `firmware/test/host/CMakeLists.txt`
- Modify: `firmware/sdkconfig.release-probe.defaults`

**Interfaces:**

- Consumes: physical `Home/G0 + Fn` boot chord, USB CDC frames and a verified release bundle.
- Produces: inactive-slot OTA write, device-side two-layer verification and explicit result without enabling other services.

- [ ] **Step 1: Write RED host and firmware state-machine tests.**

```python
from tools.recovery.protocol import Frame, FrameType, RecoverySequence


def test_out_of_order_chunk_is_rejected() -> None:
    sequence = RecoverySequence()
    assert sequence.accept(Frame(FrameType.HELLO, 0, b"")).accepted
    assert sequence.accept(Frame(FrameType.MANIFEST, 1, signed_manifest())).accepted
    result = sequence.accept(Frame(FrameType.IMAGE_CHUNK, 3, b"x" * 1024))
    assert not result.accepted
    assert result.error == "sequence_mismatch"


def test_bundle_with_same_rsa_but_wrong_p256_is_rejected(keys) -> None:
    bundle = rsa_signed_bundle(keys.rsa_private, invalid_p256_manifest())
    assert verify_bundle(bundle, keys.manifest_public).error == "invalid_manifest_signature"
```

C++ tests require recovery chord to disable HID/BLE/Wi-Fi/Web, refuse active-slot writes, refuse commit before exact byte count/hash/signature and keep the previous OTA slot bootable after any error.

- [ ] **Step 2: Run RED.**

```bash
uv run pytest tools/recovery/tests -q
cmake --build build/phase0/firmware-host
ctest --test-dir build/phase0/firmware-host -R recovery --output-on-failure
```

Expected: recovery modules are missing.

- [ ] **Step 3: Fix the USB protocol.**

Frame encoding:

```text
8 bytes magic "CCPREC01"
1 byte  type: HELLO=1, MANIFEST=2, IMAGE_CHUNK=3, COMMIT=4, ABORT=5, RESULT=6
4 bytes sequence, big endian
2 bytes payload length, big endian, maximum 4096
N bytes payload
4 bytes CRC32 of type through payload, big endian
```

HELLO negotiates protocol/hardware ID without revealing chip ID. MANIFEST must pass P-256/model/schema/RSA-digest policy before OTA erase. Chunks are exactly ordered and streamed; COMMIT is accepted only after exact `image_length` and SHA-256.

Core decoder:

```python
def decode_frame(raw: bytes) -> Frame:
    if len(raw) < 19 or raw[:8] != b"CCPREC01":
        raise RecoveryProtocolError("invalid_header")
    frame_type = FrameType(raw[8])
    sequence = int.from_bytes(raw[9:13], "big")
    length = int.from_bytes(raw[13:15], "big")
    if length > 4096 or len(raw) != 19 + length:
        raise RecoveryProtocolError("invalid_length")
    expected = int.from_bytes(raw[-4:], "big")
    if zlib.crc32(raw[8:-4]) != expected:
        raise RecoveryProtocolError("invalid_crc")
    return Frame(frame_type, sequence, raw[15:-4])
```

- [ ] **Step 4: Implement fail-closed device recovery.**

```cpp
enum class RecoveryState {
  waiting_manifest,
  receiving_image,
  verifying_image,
  ready_to_boot,
  failed,
};

struct RecoveryContext {
  RecoveryState state;
  esp_ota_handle_t ota;
  const esp_partition_t* inactive_partition;
  uint32_t next_sequence;
  uint32_t received_bytes;
  mbedtls_sha256_context sha256;
};
```

On boot chord, initialize display and TinyUSB CDC only. After external manifest/signature acceptance, map the inactive OTA partition to its inactive config slot, call `esp_ota_begin` and `esp_ota_write` per chunk. On COMMIT: finalize SHA-256, call `esp_ota_end`, verify the complete image, external manifest and Secure Boot signature/digest with public ESP-IDF image APIs, then write/read-back/verify the deterministic manifest record in the paired encrypted config slot. Only after both inactive records are durable may it call `esp_ota_set_boot_partition`. Any error aborts the OTA handle, invalidates only the incomplete inactive config record, erases only the incomplete inactive OTA range, returns a stable code and preserves the current OTA/config pair.

- [ ] **Step 5: Configure Secure ROM behavior and document the boundary.**

Release eFuse policy enables Secure ROM Download Mode (`ENABLE_SECURITY_DOWNLOAD`) only after all other eFuse work. This prevents arbitrary RAM loader execution and eFuse manipulation but still permits basic ROM flash writes. The app recovery protocol is therefore the supported signed recovery path. Each released app has a secure-version baseline and a P-256 release manifest in its paired encrypted config slot; direct ROM writes cannot reach normal services unless Secure Boot and startup release policy both pass.

The Phase 0 result must explicitly state that recovery assumes at least one RSA-authenticated OTA slot remains bootable, which is also protected by OTA rollback. If the approved requirement instead demands recovery after both slots are destroyed without retaining a per-device Flash Encryption key, this layout cannot meet it and Gate 6 is `FAIL`.

- [ ] **Step 6: Run GREEN.**

```bash
uv run pytest tools/recovery/tests -q
ctest --test-dir build/phase0/firmware-host -R recovery --output-on-failure
scripts/phase0/idf.sh -C firmware -B build/release-probe build
```

Expected: protocol/state tests pass; normal services are unreachable in recovery; incomplete/invalid writes preserve the active image; only a fully verified inactive image is selected.

- [ ] **Step 7: Commit.**

```bash
git add protocol/phase0/recovery-v1.md tools/recovery firmware/main/probe/usb_recovery.hpp firmware/main/probe/usb_recovery.cpp firmware/main/app_main.cpp firmware/main/CMakeLists.txt firmware/sdkconfig.release-probe.defaults firmware/test/host/CMakeLists.txt firmware/test/host/test_recovery_policy.cpp firmware/test/host/test_usb_recovery.cpp
git commit -m "feat: add signed usb recovery protocol"
```

## Task 6: Run Irreversible Release Security HIL

**Files:**

- Create: `protocol/phase0/release-security-evidence.schema.json`
- Consume: `protocol/phase0/release-efuse-plan.json`
- Create: `scripts/phase0/backup_flash.sh`, `scripts/phase0/prepare_security_hil_approval.py`, `scripts/phase0/run_security_hil.py`
- Create: `tools/phase0/tests/test_release_security_hil.py`
- Create: `docs/validation/phase0/security-hil.md`

**Interfaces:**

- Consumes: exact dedicated chip ID, 8MiB backup, final signed image/release bundle and explicit irreversible approval.
- Produces: typed Gate 6 release-security measurements; it never emits a trusted status.
- `release-security-evidence.schema.json` requires the same `git_commit`, `git_tree_clean=true`, `toolchain_manifest_sha256` and `release_firmware_sha256` as the release-budget record plus the signed manifest/config-slot digests, dedicated-device digest, approval facts hash, eFuse facts and every positive/negative case; it contains no gate verdict field.

- [ ] **Step 1: Write RED approval/evidence tests.**

```python
def test_irreversible_run_needs_live_chip_and_external_approval(runner) -> None:
    result = runner.parse([
        "--port", "/dev/cu.test",
        "--confirm-chip-id", "abc123",
    ])
    assert result.error == "security_approval_artifact_required"


def test_expired_or_mismatched_approval_is_rejected(runner, approval) -> None:
    approval["expires_at"] = "2026-07-23T00:00:00Z"
    assert runner.validate_approval(approval).error == "security_approval_expired"
    approval["facts_sha256"] = "00" * 32
    assert runner.validate_approval(approval).error == "security_approval_mismatch"


def test_virtual_efuse_cannot_be_hardware_evidence(evaluator) -> None:
    evidence = complete_security_evidence()
    evidence["virtual_efuse"] = True
    assert evaluator(evidence).state == "BLOCKED"
```

- [ ] **Step 2: Run RED.**

```bash
uv run pytest tools/phase0/tests/test_release_security_hil.py -q
```

Expected: HIL runner/schema are missing.

- [ ] **Step 3: Implement the approval artifact and fail-closed HIL harness.**

`release-efuse-plan.json` contains the exact sorted eFuse fields/values and the expected pre/post security states; the runner rejects unknown fields, already-conflicting bits or a plan hash that does not equal the signed external manifest's `release_efuse_plan_sha256`. `prepare_security_hil_approval.py` and `run_security_hil.py` share one canonical facts encoder. The run parser has no `--force`, generic acknowledgement or approval-bypass option.

Run GREEN:

```bash
uv run pytest tools/phase0/tests/test_release_security_hil.py -q
uv run check-jsonschema \
  --schemafile https://json-schema.org/draft/2020-12/schema \
  protocol/phase0/release-security-evidence.schema.json
```

Expected: approval expiry/mismatch/reuse tests, virtual-eFuse rejection, every negative case and schema validation pass without accessing hardware.

- [ ] **Step 4: Commit the validated harness before any irreversible work.**

```bash
git add protocol/phase0/release-security-evidence.schema.json scripts/phase0/backup_flash.sh scripts/phase0/prepare_security_hil_approval.py scripts/phase0/run_security_hil.py tools/phase0/tests/test_release_security_hil.py
git commit -m "test: add release security hil harness"
```

Wait until the macOS and firmware HIL harness commits also exist. Require a clean tree and record the shared HEAD as `HIL_BASE_COMMIT`; build the release probe from that commit.

From that unchanged clean tree, rebuild and remeasure the exact release cohort:

```bash
test -z "$(git status --porcelain)"
test "$(git rev-parse HEAD)" = "$HIL_BASE_COMMIT"
scripts/phase0/build_release_probe.sh
uv run tools/phase0/image_budget.py \
  --partitions firmware/partitions.csv \
  --image build/phase0/release-bundle/app.bin \
  --json build/phase0/release-image-budget.json
```

Both the budget record and later security record must store this `HIL_BASE_COMMIT`, the same toolchain-manifest hash and the same `release_firmware_sha256`; any source/docs change or rebuild between them invalidates the cohort.

- [ ] **Step 5: Back up and create a chip-bound approval challenge before irreversible work.**

Resolve one exact USB port, verify chip/revision/8MiB/no-PSRAM, then:

```bash
scripts/phase0/backup_flash.sh \
  --port "$SECURITY_CARDPUTER_PORT" \
  --size 0x800000 \
  --output build/phase0/security-device-backup
```

`prepare_security_hil_approval.py` hashes the exact chip ID, hardware revision, current eFuse summary, backup SHA-256, release build SHA-256 and sorted planned permanent bits, then displays those facts plus a fresh 128-bit challenge. It reads the operator's confirmation phrase from `/dev/tty`, never argv/stdin piping, and only after the user has explicitly designated this unit and approved permanent changes writes a mode-`0600` artifact under `build/phase0/security-approval/`. The artifact includes the facts hash, challenge, creation/expiry times (15 minutes) and `consumed=false`; it contains no secret and is never committed.

After that explicit approval, generate the exact artifact:

```bash
uv run scripts/phase0/prepare_security_hil_approval.py \
  --port "$SECURITY_CARDPUTER_PORT" \
  --confirm-chip-id "$SECURITY_CARDPUTER_CHIP_ID" \
  --hardware-manifest build/phase0/hardware-manifest.json \
  --efuse-summary build/phase0/security-device-backup/efuse-before.json \
  --backup-sha256-file build/phase0/security-device-backup/flash.bin.sha256 \
  --release-build firmware/build/release-probe \
  --planned-bits protocol/phase0/release-efuse-plan.json \
  --output build/phase0/security-approval/approval.json
```

Before any permanent write, run `run_security_hil.py --preflight-only` with the same arguments as Step 6 and require it to print the live chip ID digest, facts SHA-256, approval expiry and `approval_match=true`.

- [ ] **Step 6: Execute only with the chip-bound one-time approval.**

```bash
uv run scripts/phase0/run_security_hil.py \
  --port "$SECURITY_CARDPUTER_PORT" \
  --confirm-chip-id "$SECURITY_CARDPUTER_CHIP_ID" \
  --approval-file build/phase0/security-approval/approval.json \
  --release-build firmware/build/release-probe \
  --bundle build/phase0/release-bundle \
  --schema protocol/phase0/release-security-evidence.schema.json \
  --output build/phase0/security-hil/raw.json
```

The runner must verify:

0. the approval artifact is unexpired, unconsumed and exactly matches the live chip/eFuse/backup/release/planned-bit hash; `--preflight-only` never consumes it, while the real run atomically marks it consumed immediately before the first permanent write, and no flag can bypass this check;

1. before irreversible enablement, factory provisioning writes the exact bundle `app.bin` and matching `config-slot.bin` to one OTA/config pair; after enablement, Secure Boot v2 RSA digest and boot state match the signed manifest;
2. Flash Encryption Release and NVS Encryption;
3. Secure ROM Download Mode and restricted `--no-stub` command behavior;
4. valid application USB recovery to the inactive slot;
5. rejection of invalid P-256, wrong model/schema, wrong RSA, unsigned, tampered and wrong-length bundles;
6. direct ROM writes of unsigned, wrong-RSA, same-RSA/wrong-P-256 and plaintext images never reach normal services;
7. failed recovery, including power loss after app write or config write but before boot selection, preserves/rolls back to the old valid OTA/config pair;
8. reboot repeats active config-slot manifest signature, full running-image hash, eFuse and Secure Boot checks before normal services.

- [ ] **Step 7: Recompute Gate inputs and create only sanitized summaries.**

```bash
uv run tools/phase0/evidence.py evaluate-gate \
  --gate P0-G4-FLASH \
  --measurement build/phase0/release-image-budget.json
uv run tools/phase0/evidence.py evaluate-gate \
  --gate P0-G6-SECURITY \
  --measurement build/phase0/security-hil/raw.json
git diff --check
```

Expected: evaluators hash/reparse the signed image and case set. Missing dedicated hardware, approval, raw hash or any negative case yields `BLOCKED/FAIL`; virtual evidence cannot pass.

- [ ] **Step 8: Commit only sanitized summaries after all HIL runs finish.**

```bash
git add docs/validation/phase0/flash-budget.md docs/validation/phase0/security-hil.md
git commit -m "docs: record release security evidence"
```
