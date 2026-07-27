# Cardputer Codex Companion

把 M5Stack Cardputer 变成 Codex 的局域网遥控副屏、可编程蓝牙键盘和中文文本输入端。

## 已实现

- 开机先初始化 240×135 屏幕，并逐项显示 Display、Config、Keyboard、BLE、Wi‑Fi、Web、Companion 状态。
- 按 M5Stack 官方 8 selector × 7 input 电路扫描完整 4×14 / 56 键，20 ms 去抖并发送完整 BLE HID report。
- 从独立 `wifi_cfg` NVS 读取预置 Wi‑Fi；连接失败不影响屏幕、键盘或 BLE。
- 在 `https://设备IP/` 提供 56 键、四层 Profile 编辑器，支持透传、HID 组合键、UTF‑8/中文字符串、最多 16 步输入序列、设备动作和 Codex 动作。
- Web 首次启动生成并持久化设备 P‑256 HTTPS 身份；进入控制台前必须先输入屏幕显示的八位 PIN，Settings 中可修改 PIN 和 Wi‑Fi。
- macOS Companion 使用真实 `codex app-server --listen stdio://` 获取会话，向副屏同步 title、cwd、状态、Approval 与 Input 数量。
- 可由标准 US HID 表示的 ASCII 字符串直接通过 BLE HID 逐键发送；中文及其他 Unicode 字符串通过受保护 BLE GATT 分片发送，由 Companion 使用 `CGEvent.keyboardSetUnicodeString` 注入；不使用剪贴板或 Command‑V。
- Profile 四层均参与运行：Keyboard/Codex 基础层分别为 0/1，按住 Fn 时分别切换到 2/3。
- 主屏为 2–3 FPS 的 Codex 宠物；`Fn+;`/`Fn+.` 上下滚动，`Fn+,`/`Fn+/` 在 Pets、Device、Codex、Sync、Settings 五页间切换。
- Device 显示版本及连接状态；Codex 显示活跃会话、Model、Fast、Thinking 和可用限额（缺失项隐藏）；Sync 显示 IP、心跳、宠物同步与当前 Profile。
- Profile Catalog 支持不可删除的 SAFE 和最多四个自定义 Profile，使用双 bank 事务存储；Web 和设备端切换立即生效并跨重启保持。
- Settings 使用裸 `; . , /` 选择和进入二级菜单，可切换 Profile、轮换 PIN、扫描/绑定 Wi‑Fi，并设置亮度、自动返回和宠物帧率。
- Cardputer 的 SPM1423 麦克风通过加密 BLE Audio v1 发送 24 kHz IMA-ADPCM；Mac Companion 解码、抖动缓冲并重采样为 48 kHz mono float，写入系统级 `Cardputer Codex Microphone` 虚拟输入设备。
- 短按 G0 是唯一录音开关。断开 BLE、退出 Companion、Core Audio producer 失效或设备重启都会立即停录；恢复连接后只回到 READY，不会自动恢复录音。
- 每页状态栏、PET 红色录音标识、DEVICE 页和 Web Settings 均显示只读麦克风状态；Web、Profile、Wi-Fi、Codex 和宠物操作均不能启动录音。

## 刷写

优先使用已经包含 Lynx Vault Wi‑Fi 配置的私有完整镜像：

```bash
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x0 dist/private/cardputer_codex_companion-private-full.bin
```

通用完整镜像位于 `dist/cardputer_codex_companion-full.bin`，没有 Wi‑Fi 凭据。应用分区镜像位于 `firmware/build/cardputer_codex_companion.bin`，它只能写到 `0x20000`，不能代替完整镜像从 `0x0` 刷写。

## 首次使用

1. 开机后屏幕应立即出现当前版本的 `CARDPUTER CODEX` 启动页。
2. 在 macOS 蓝牙设置中连接 `Cardputer Codex`；若系统保留了旧的 `nimble`/`Cardputer Codex` 配对记录，先删除旧设备后重新连接。
3. Wi‑Fi 连通后，屏幕显示设备 IP 和八位 Web PIN。浏览器访问 `https://设备IP/`；设备证书是首次启动生成的自签名证书，首次访问需要确认。页面首先显示 PIN 鉴权屏，PIN 正确后才进入键盘配置。
4. 构建 Mac Companion 和独立安装包：

```bash
scripts/package_mac_installer.sh
dist/CardputerCompanion-mac-installer/install.sh install
dist/CardputerCompanion-mac-installer/install.sh status
```

   安装器会要求输入 Cardputer 的 HTTPS URL，并隐藏输入 DEVICE 页当前八位 PIN。
   PIN 只写入权限 `0600` 的配置文件，不进入命令行、LaunchAgent plist 或日志。
   App 固定安装到 `~/Applications/CardputerCompanion.app`，LaunchAgent 使用
   `RunAtLoad` 与 `KeepAlive` 自动登录启动；安装器也会通过一次 `sudo` 部署 HAL、
   AudioBridge 和 LaunchDaemon，并重启 Core Audio。PET 页 `B/W/M` 分别表示
   BLE、Wi‑Fi 和经过 PIN 鉴权的 Mac Agent，三项正常时显示 `B+W+M+`。系统输入
   设备为 `Cardputer Codex Microphone`。

   仅在诊断安装器内部音频步骤时，可直接使用低层命令：

```bash
dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion doctor audio
sudo dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  install-audio-driver
sudo dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  uninstall-audio-driver
```

5. 在 Web 中点击键位打开弹窗，把某个按键设为“中文字符串”并填入中文，或设为“组合键”后直接按下 `Alt+V` 这类组合键采集；非直通键会在键帽上显示真实用途，发布后即可使用。Settings 选项卡可修改 PIN 和 Wi‑Fi 信息。

6. 短按 G0 开始录音，再短按一次停止。G0 没有长按动作。Keyboard/Codex Input Mode 和不可变 SAFE Profile 均在 Cardputer 的 Settings 中选择；选择 SAFE 会先发送 HID Release All。

普通卸载会移除 App、LaunchAgent、HAL 和 AudioBridge，但保留设备配置和日志：

```bash
dist/CardputerCompanion-mac-installer/install.sh uninstall
```

彻底清除配置和日志以进行干净安装验证：

```bash
dist/CardputerCompanion-mac-installer/install.sh uninstall --purge
```

从旧版本升级且需要保留 PIN、Wi‑Fi、Profile、宠物与 BLE 配对时，只写应用分区：

```bash
python -m esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX \
  --before default_reset --after hard_reset \
  write_flash 0x20000 firmware/build/cardputer_codex_companion.bin
```

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
