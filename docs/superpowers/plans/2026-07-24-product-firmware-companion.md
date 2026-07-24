# Cardputer Codex Companion 产品固件与 macOS Companion 实施计划

> **执行要求：** 使用 `executing-plans` 与 `test-driven-development`，逐项完成 RED、GREEN、目标构建和提交。本侧会话禁止子代理，因此由当前会话内联执行。

**Goal:** 把现有 Phase 0 探针升级为可刷写、开机有显示、可扫描 56 键、可作 BLE 键盘、可从私有 NVS 自动连接 Wi-Fi、可在局域网配置按键/组合键/中文字符串，并能显示真实 Codex 会话状态的产品固件，同时交付原生 macOS Companion。

**Architecture:** 保留 ESP-IDF 5.5.4、M5Unified、NimBLE、现有有界 HTTPS/GATT/WSS 安全组件与 SwiftPM 骨架。新增可宿主测试的产品状态、键盘、Profile、宏、Wi-Fi 与 Companion DTO 模块；ESP32 适配层只负责 GPIO、M5GFX、NVS、网络和任务。macOS Companion 使用 Codex App Server stdio、Network.framework、CoreBluetooth、ApplicationServices 与 SQLite。Vault 明文只在私有打包临时目录出现。

**Source of truth:** [`2026-07-24-product-firmware-companion-implementation-design.md`](../specs/2026-07-24-product-firmware-companion-implementation-design.md)

**Hardware references:**

- M5Stack `M5Cardputer` commit `f1392858b9994c3547120e602a57d3553d16ab01`
- `IOMatrixKeyboardReader`: selector GPIO `8,9,11`; input GPIO `13,15,3,4,5,6,7`; eight selector states mapped to the 4×14 key grid
- M5Stack `M5Cardputer-UserDemo` commit `a34d7ebcd508fb903a2b1123be8c8b54abd55d07`
- M5Unified `0.2.17` / M5GFX `0.2.26`

## Global invariants

- 屏幕和键盘必须先于 Wi-Fi、Web、Companion 初始化；网络失败不能阻塞 BLE HID。
- 每次物理状态变化发送完整的六键 HID report；释放、模式切换、异常和宏结束发送 `release all`。
- `text_utf8` 只经认证 GATT 交给 Companion；禁止剪贴板、Command-V 和键盘布局模拟中文。
- 配置最多 8 个 Profile、4 层、56 个实体键；字符串最多 1024 UTF-8 bytes；序列最多 16 步、10 秒。
- Web 只监听局域网地址；沿用物理配对、Host/Origin/CSRF、会话与请求预算。
- Cardputer 只能执行稳定 DTO 和白名单 Codex action，不能携带 shell、任意路径或原始 RPC。
- 私有 NVS、私有完整镜像和临时明文目录均不得进入 Git；日志不得打印 SSID、密码或文本正文。
- 没有 Cardputer、完整 Xcode/TCC 或目标应用时，自动化构建可以交付，但对应真机结果必须标为未验证。

## Task 1: 产品构建目标与分区

**Files:**

- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/sdkconfig.defaults`
- Create: `firmware/partitions_product.csv`
- Modify: `firmware/main/CMakeLists.txt`
- Create: `firmware/main/product/product_types.hpp`
- Create: `firmware/test/host/test_product_types.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`

**RED:** 写测试固定启动阶段、连接状态、输入模式、56 个物理键 ID、产品版本和不含 `phase0-probe` 的名称。

**GREEN:** 新增产品类型和 8 MiB 分区：NVS、otadata、双 OTA、私有 `wifi_cfg` NVS、SPIFFS；项目名改为 `cardputer_codex_companion`，版本改为 `1.0.0`.

**Verify:**

```bash
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host --target test_product_types
ctest --test-dir build/product-host -R product_types --output-on-failure
scripts/phase0/idf.sh reconfigure
scripts/phase0/idf.sh partition-table
```

## Task 2: 启动 UI 与状态模型

**Files:**

- Create: `firmware/main/product/ui_model.hpp`
- Create: `firmware/main/product/ui_model.cpp`
- Create: `firmware/main/product/display.hpp`
- Create: `firmware/main/product/display.cpp`
- Create: `firmware/test/host/test_ui_model.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`

**RED:** 验证七项启动阶段只接受 `OK/OFFLINE/E###`，运行页包含 BLE、Wi-Fi、Companion、Profile、session、cwd、状态、Approval/Input 数。

**GREEN:** 实现纯状态 reducer 和 M5Unified 240×135 renderer；`display_start()` 调用 `M5.begin()`、设置旋转/背光并在返回前画出 `CARDPUTER CODEX / DISPLAY OK`。

**Verify:**

```bash
cmake --build build/product-host --target test_ui_model
ctest --test-dir build/product-host -R ui_model --output-on-failure
scripts/phase0/idf.sh build
```

## Task 3: 56 键扫描、去抖和 HID 映射

**Files:**

- Create: `firmware/main/product/keyboard_matrix.hpp`
- Create: `firmware/main/product/keyboard_matrix.cpp`
- Create: `firmware/main/product/keymap.hpp`
- Create: `firmware/main/product/input_router.hpp`
- Create: `firmware/main/product/input_router.cpp`
- Modify: `firmware/main/probe/keyboard_probe.hpp`
- Modify: `firmware/main/probe/keyboard_probe.cpp`
- Create: `firmware/test/host/test_keyboard_matrix.cpp`
- Create: `firmware/test/host/test_input_router.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`

**RED:** 用官方 8×7 原始位图逐键验证 4×14 坐标、ASCII/HID、Fn F1–F12/方向/Delete、修饰键和 20 ms 去抖；验证三键组合在释放前保持，模式切换释放全部。

**GREEN:** 实现无动态分配扫描器、20 ms 去抖、10 ms ESP GPIO task、完整 report API 和 Keyboard/Codex Remote/Safe Profile 路由。G0 短按切换模式，长按 2 秒进入 Safe Profile；启动 G0+Fn 禁止网络自动连接。

**Verify:**

```bash
cmake --build build/product-host --target test_keyboard_matrix test_input_router test_keyboard_probe
ctest --test-dir build/product-host -R 'keyboard_matrix|input_router|keyboard_probe' --output-on-failure
scripts/phase0/idf.sh build
```

## Task 4: Profile、宏和双槽持久化

**Files:**

- Create: `firmware/main/product/profile.hpp`
- Create: `firmware/main/product/profile.cpp`
- Create: `firmware/main/product/macro_engine.hpp`
- Create: `firmware/main/product/macro_engine.cpp`
- Create: `firmware/main/product/profile_store.hpp`
- Create: `firmware/main/product/profile_store.cpp`
- Create: `firmware/test/host/test_profile.cpp`
- Create: `firmware/test/host/test_macro_engine.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`

**RED:** 覆盖 safe/default Profile、七类 action、限制校验、chord 按下/释放、UTF-8 转 GATT operation、16 步/10 秒、损坏槽回退和 revision 冲突。

**GREEN:** 实现固定容量模型、cJSON 编解码、CRC32 双槽 `profile_a/profile_b`、active revision 和失败关闭执行器。

**Verify:**

```bash
cmake --build build/product-host --target test_profile test_macro_engine
ctest --test-dir build/product-host -R 'profile|macro_engine' --output-on-failure
scripts/phase0/idf.sh build
```

## Task 5: Wi-Fi NVS 与配网状态机

**Files:**

- Create: `firmware/main/product/wifi_manager.hpp`
- Create: `firmware/main/product/wifi_manager.cpp`
- Create: `firmware/main/product/wifi_credentials.hpp`
- Create: `firmware/main/product/wifi_credentials.cpp`
- Create: `firmware/test/host/test_wifi_manager.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`

**RED:** 验证 `wifi_cfg` 优先、运行 NVS 次之、15 秒连接超时、有限退避、离线状态、恢复模式不自连、凭据永不通过 getter/API 返回。

**GREEN:** 实现 ESP event-loop/netif/Wi-Fi STA；失败后停止等待并保留 BLE；无配置时进入临时 AP，SSID 使用设备短 ID，随机密码只提交给屏幕。

**Verify:**

```bash
cmake --build build/product-host --target test_wifi_manager
ctest --test-dir build/product-host -R wifi_manager --output-on-failure
scripts/phase0/idf.sh build
```

## Task 6: 产品 Web API 与内嵌 SPA

**Files:**

- Create: `firmware/main/product/product_web.hpp`
- Create: `firmware/main/product/product_web.cpp`
- Create: `firmware/main/product/web_assets.hpp`
- Create: `firmware/main/product/web_assets.cpp`
- Create: `web/package.json`
- Create: `web/src/index.html`
- Create: `web/src/app.js`
- Create: `web/src/style.css`
- Create: `scripts/build_web_assets.py`
- Create: `tools/product/tests/test_web_assets.py`
- Modify: `firmware/main/probe/web_handlers.hpp`
- Modify: `firmware/main/probe/web_handlers.cpp`

**RED:** 测试 API manifest、未认证拒绝、GET status/profile、PUT draft、POST validate/publish、409 revision、import/export 不含密码、SPA 含 56 键编辑器与 chord/text/sequence 表单。

**GREEN:** 在现有安全 handler context 上注册产品路由，连接 Profile Store/UI；构建脚本压缩并生成静态 C++ asset。配网 Portal 使用独立最小路由，不能访问管理 API。

**Verify:**

```bash
uv run pytest tools/product/tests/test_web_assets.py -q
scripts/build_web_assets.py --check
scripts/phase0/idf.sh build
```

## Task 7: 设备 HTTPS 身份与启动编排

**Files:**

- Create: `firmware/main/product/device_identity.hpp`
- Create: `firmware/main/product/device_identity.cpp`
- Create: `firmware/main/product/product_controller.hpp`
- Create: `firmware/main/product/product_controller.cpp`
- Replace: `firmware/main/app_main.cpp`
- Create: `firmware/test/host/test_product_controller.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`

**RED:** 验证启动顺序、网络故障隔离、证书/密钥只保存 NVS、pairing 物理回调、受控重启 release-all。

**GREEN:** 首启生成 P-256 设备身份和 self-signed HTTPS 证书，随后按批准顺序启动所有服务；屏幕持续呈现阶段结果，不再输出 `PHASE 0 / NOT FOR RELEASE`。

**Verify:**

```bash
cmake --build build/product-host --target test_product_controller
ctest --test-dir build/product-host -R product_controller --output-on-failure
scripts/phase0/idf.sh fullclean
scripts/phase0/idf.sh build
```

## Task 8: Companion DTO 与设备状态同步

**Files:**

- Create: `protocol/product/companion-v1.schema.json`
- Create: `firmware/main/product/companion_protocol.hpp`
- Create: `firmware/main/product/companion_protocol.cpp`
- Create: `firmware/test/host/test_companion_protocol.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify: `firmware/test/host/CMakeLists.txt`

**RED:** 覆盖 session snapshot、selected session、approval/input counts、stale timeout、白名单 action 和未知消息拒绝。

**GREEN:** 将 WSS payload 解析为固定 DTO 并更新 UI model；普通状态可覆盖，关键序列缺口转 `STALE/resync`；Codex action 只允许 select/new/interrupt/approve/reject/provide-input。

**Verify:**

```bash
cmake --build build/product-host --target test_companion_protocol
ctest --test-dir build/product-host -R companion_protocol --output-on-failure
scripts/phase0/idf.sh build
```

## Task 9: Swift 产品包、配置与 Codex App Server 适配

**Files:**

- Modify: `companion/Package.swift`
- Create: `companion/Sources/ProductContracts/CompanionDTO.swift`
- Create: `companion/Sources/CodexAppServer/JSONRPCProcess.swift`
- Create: `companion/Sources/CodexAppServer/CodexAdapter.swift`
- Create: `companion/Sources/CardputerCompanion/Configuration.swift`
- Create: `companion/Tests/ProductContractsTests/CompanionDTOTests.swift`
- Create: `companion/Tests/CodexAppServerTests/CodexAdapterTests.swift`

**RED:** 用当前 `codex app-server generate-json-schema` 生成的 fixture 验证 initialize、thread/list、thread/read、通知归一化和命令白名单。

**GREEN:** 启动 `codex app-server --listen stdio://`，实现 Content-Length/JSONL 实际 framing 探测与严格 JSON-RPC adapter；对外只输出产品 DTO。

**Verify:**

```bash
cd companion
swift build
swift test --filter ProductContractsTests
swift test --filter CodexAppServerTests
```

若本机 XCTest 仍缺失，`swift build -c release` 必须通过，测试保留为明确 blocker，不能伪造通过。

## Task 10: Companion LAN WSS、mDNS 与配对

**Files:**

- Create: `companion/Sources/CompanionNetwork/LANInterface.swift`
- Create: `companion/Sources/CompanionNetwork/WSSServer.swift`
- Create: `companion/Sources/CompanionNetwork/PairingSession.swift`
- Create: `companion/Tests/CompanionNetworkTests/LANInterfaceTests.swift`
- Create: `companion/Tests/CompanionNetworkTests/PairingSessionTests.swift`

**RED:** 验证只绑定显式 LAN interface、接口改变即停止、`_codex-companion._tcp.local`、SPKI/SAS/exporter/bind challenge 和非配对设备拒绝。

**GREEN:** 使用 Network.framework TLS listener 和 Bonjour；长期身份放 Keychain；复用 Phase 0 canonical pairing vectors，成功后推送 DTO 并接收白名单 action。

**Verify:**

```bash
cd companion
swift build
swift test --filter CompanionNetworkTests
```

## Task 11: CoreBluetooth 中文注入产品链路

**Files:**

- Create: `companion/Sources/ProductGATT/ProductGATTReceiver.swift`
- Create: `companion/Sources/ProductUnicode/AXFocusGuard.swift`
- Create: `companion/Sources/ProductUnicode/UnicodeInjector.swift`
- Create: `companion/Tests/ProductGATTTests/ProductGATTReceiverTests.swift`
- Create: `companion/Tests/ProductUnicodeTests/UnicodeInjectorTests.swift`

**RED:** 验证认证/重放/重组顺序、UTF-8 分块、焦点 PID/AX 元素复验、Secure Input、completed/partial/failed/indeterminate 和 ledger 去重。

**GREEN:** 使用 CoreBluetooth notify receiver 和 `CGEvent.keyboardSetUnicodeString`；不调用 Pasteboard，不合成 Command-V；响应只含 hash、前缀长度和状态。

**Verify:**

```bash
cd companion
swift build -c release
rg -n 'NSPasteboard|pasteboard|cmd.?v|Command.?V' Sources
```

## Task 12: Companion 可执行程序与安装

**Files:**

- Create: `companion/Sources/cardputer-companion/main.swift`
- Create: `companion/AppBundle/Info.plist`
- Create: `companion/AppBundle/CardputerCompanion.entitlements`
- Create: `scripts/build_companion.sh`
- Create: `scripts/install_companion.sh`
- Create: `scripts/com.lynx.cardputer-companion.plist`
- Create: `tools/product/tests/test_companion_packaging.py`

**RED:** 验证 `--version`、`doctor`、`run`、LaunchAgent 参数、固定安装路径和签名检查。

**GREEN:** 交付 release CLI/app bundle、doctor 输出 Codex/Accessibility/Bluetooth/LAN 状态，安装脚本只写用户 Library 路径且不自动批准 TCC。

**Verify:**

```bash
uv run pytest tools/product/tests/test_companion_packaging.py -q
scripts/build_companion.sh
dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion --version
dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion doctor
```

## Task 13: Vault 私有 NVS 与完整镜像

**Files:**

- Create: `scripts/package_private_firmware.sh`
- Create: `tools/product/generate_wifi_nvs.py`
- Create: `tools/product/merge_product_image.py`
- Create: `tools/product/tests/test_private_packaging.py`
- Modify: `.gitignore`

**RED:** 用非秘密 fixture 验证 NVS namespace/key、0600 临时文件、清理 trap、分区边界、merged image offset、日志脱敏和拒绝空值。

**GREEN:** 包装脚本在同一进程内调用 Vault service-account helper，读取两个精确 ref 的 scalar `value`，生成 `build/private/wifi_cfg.bin`，再与 bootloader、partition table、otadata、app 合并为 `dist/private/cardputer_codex_companion-private-full.bin`。

**Verify:**

```bash
uv run pytest tools/product/tests/test_private_packaging.py -q
scripts/package_private_firmware.sh
git status --short
git grep -n -I -e 'shared.wifi.password' -- ':!docs/**' ':!scripts/package_private_firmware.sh'
```

`dist/private` 及 `build/private` 必须被 Git 忽略；最终只报告路径、size、SHA-256。

## Task 14: 完整回归、制品与证据

**Files:**

- Create: `scripts/verify_product_release.sh`
- Create: `docs/validation/product-release.md`
- Modify: `README.md`
- Modify: `docs/2026-07-24-cardputer-codex-companion_PROGRESS.md`

**Steps:**

1. 运行所有 firmware host tests 与 ASan/UBSan。
2. ESP-IDF `fullclean` 后构建并 `merge_bin` 通用镜像。
3. Swift release build 与可运行 smoke。
4. Web/packaging/Python tests。
5. 扫描源码、Git tracked files、日志和通用镜像，确认无 Vault secret。
6. 生成通用完整镜像、应用镜像、私有完整镜像、私有 NVS、Companion app 的 size/SHA-256 清单。
7. 若发现唯一 Cardputer 串口，先只报告并等待刷机目标确认；没有真机则不进行 30 分钟 HIL。
8. 更新进度、当日 memory、提交全部非秘密改动；若无 remote，明确说明不能 push。

**Verify:**

```bash
scripts/verify_product_release.sh
git diff --check
git status --short
```

## Completion criteria

- 从 `0x0` 刷入通用或私有完整镜像后，第一可见行为是启动屏幕，不是串口日志。
- 56 键扫描任务真实启动并向现有 NimBLE HID 发送带修饰键的完整 report。
- 私有镜像含 Wi-Fi NVS，源码和 Git 不含 SSID/密码。
- Wi-Fi 失败时屏幕明确显示 OFFLINE，BLE HID 继续运行。
- LAN Web 提供可发布的 Profile/keymap/chord/text/sequence 配置。
- 中文字符串经 GATT/Companion 原生 Unicode 路径执行。
- Companion 从真实 Codex App Server 产生会话 DTO，Cardputer 运行页显示它。
- 所有可在当前主机执行的测试与 release build 通过；真机/TCC 未具备的证据明确列为未验证。
