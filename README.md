# Cardputer Codex Companion

把 M5Stack Cardputer 变成 Codex 的局域网遥控副屏、可编程蓝牙键盘和中文文本输入端。

## 已实现

- 开机先初始化 240×135 屏幕，并逐项显示 Display、Config、Keyboard、BLE、Wi‑Fi、Web、Companion 状态。
- 按 M5Stack 官方 8 selector × 7 input 电路扫描完整 4×14 / 56 键，20 ms 去抖并发送完整 BLE HID report。
- 从独立 `wifi_cfg` NVS 读取预置 Wi‑Fi；连接失败不影响屏幕、键盘或 BLE。
- 在 `https://设备IP/` 提供 56 键、四层 Profile 编辑器，支持透传、HID 组合键、UTF‑8/中文字符串、最多 16 步输入序列、设备动作和 Codex 动作。
- Web 首次启动生成并持久化设备 P‑256 HTTPS 身份；写操作需要屏幕显示的八位配对码。
- macOS Companion 使用真实 `codex app-server --listen stdio://` 获取会话，向副屏同步 title、cwd、状态、Approval 与 Input 数量。
- 中文字符串通过受保护 BLE GATT 分片发送，由 Companion 使用 `CGEvent.keyboardSetUnicodeString` 注入；不使用剪贴板或 Command‑V。
- Profile 四层均参与运行：Keyboard/Codex 基础层分别为 0/1，按住 Fn 时分别切换到 2/3。

## 刷写

优先使用已经包含 Lynx Vault Wi‑Fi 配置的私有完整镜像：

```bash
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x0 dist/private/cardputer_codex_companion-private-full.bin
```

通用完整镜像位于 `dist/cardputer_codex_companion-full.bin`，没有 Wi‑Fi 凭据。应用分区镜像位于 `firmware/build/cardputer_codex_companion.bin`，它只能写到 `0x20000`，不能代替完整镜像从 `0x0` 刷写。

## 首次使用

1. 开机后屏幕应立即出现 `CARDPUTER CODEX 1.0.3` 启动页。
2. 在 macOS 蓝牙设置中连接 `Cardputer Codex`；需要 PIN 时输入 `123456`。
3. Wi‑Fi 连通后，屏幕显示设备 IP 和八位 Web PIN。浏览器访问 `https://设备IP/`；设备证书是首次启动生成的自签名证书，首次访问需要确认。
4. 构建并启动 Mac Companion：

```bash
scripts/build_companion.sh
dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion doctor
dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  run --device https://设备IP --pairing 屏幕八位PIN
```

5. 在 Web 中把某个按键设为 `text_utf8` 并填入中文，或设为 `hid_chord` 并填写 HID modifier/usages，发布后即可使用。

短按 G0/Home 在 Keyboard 与 Codex 模式间切换；长按两秒释放所有按键并进入 Safe Profile。

## 构建与验证

```bash
PYTHONPATH=. uv run pytest -q
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
scripts/build_web_assets.py --check
scripts/verify_product_release.sh
```

私有打包只读取 `shared.wifi.ssid` 和 `shared.wifi.password`；明文临时文件权限为 `0600` 并在生成后删除。`build/private/`、`dist/private/` 及全部构建目录均被 Git 忽略。

自动化构建不能替代实机验证。本机未连接唯一 Cardputer 时，不会自动刷写，也不会声称完成 30 分钟 HIL。HIL 是在真实 Cardputer 与 Mac 上持续运行 30 分钟，同时验证 BLE HID、Wi‑Fi、Web、Companion、中文注入、延迟、堆栈与断连恢复。

设计、计划与进度：

- [产品化增量设计](docs/superpowers/specs/2026-07-24-product-firmware-companion-implementation-design.md)
- [产品实施计划](docs/superpowers/plans/2026-07-24-product-firmware-companion.md)
- [项目进度](docs/2026-07-24-cardputer-codex-companion_PROGRESS.md)
