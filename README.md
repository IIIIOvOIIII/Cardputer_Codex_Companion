# Cardputer Codex Companion

把 M5Stack Cardputer 变成 Codex 的局域网遥控副屏、可编程蓝牙键盘和中文文本输入端。

当前公共版本：`1.2.0`。

## 已实现

- 开机先初始化 240×135 屏幕，并逐项显示 Display、Config、Keyboard、BLE、Wi‑Fi、Web、Companion 状态。
- 按 M5Stack 官方 8 selector × 7 input 电路扫描完整 4×14 / 56 键，20 ms 去抖并发送完整 BLE HID report。
- 公共固件不预置 Wi‑Fi 或配对凭据；空白设备由 Cardputer 首次初始化向导扫描、选择并连接 Wi‑Fi。
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

公共发布只提供不含 Wi‑Fi 或配对凭据的通用完整镜像：

```bash
python -m esptool --chip esp32s3 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x0 dist/cardputer_codex_companion-full.bin
```

应用分区镜像位于 `firmware/build/cardputer_codex_companion.bin`，它只能写到 `0x20000`，不能代替完整镜像从 `0x0` 刷写。

## 首次使用

新设备必须在 Cardputer 上依次完成三个门槛。初始化完成前不会开放普通
Web 配置页面，也不会把向导输入发送给 HID。

1. 开机后确认 `CARDPUTER CODEX COMPANION 1.2.0`，进入 Wi‑Fi 向导。
   使用 `;`/`.` 选择扫描到的 SSID，Enter 确认；Hidden Network 可手工输入
   SSID。密码完全在 Cardputer 上输入并以掩码显示，只有取得 IP 后才保存。
   反引号返回上一步。
2. 按屏幕提示在电脑蓝牙设置中配对 `Cardputer Codex`。只有加密、鉴权、
   bonded 且 HID 已订阅的连接才会通过；仅看到广播不会推进向导。
3. 安装当前电脑的 Machine Agent。设备只接受携带当前八位 PIN 的局域网
   HTTPS 请求；首次 Agent 配对在 PIN 鉴权成功后记录设备证书指纹。

macOS：

```bash
scripts/package_mac_installer.sh
dist/CardputerCompanion-mac-installer/install.sh install
dist/CardputerCompanion-mac-installer/install.sh status
```

安装器会要求 Cardputer HTTPS URL，并隐藏输入当前八位 PIN。PIN 仅写入
`0600` 配置，不进入命令行、LaunchAgent 或日志。App 安装到
`~/Applications/CardputerCompanion.app`；HAL、AudioBridge 与系统输入设备
`Cardputer Codex Microphone` 只在 macOS 提供。

Windows x64：

```text
dist\CardputerCompanion-1.2.0-windows-x64-setup.exe
```

Windows ARM64 使用 `CardputerCompanion-1.2.0-windows-arm64.zip`。安装器按
当前用户写入 `%LOCALAPPDATA%\CardputerCodexCompanion`，创建最低权限登录任务，
完成后运行 `cardputer-agent.exe pair`。地址和 PIN 都在交互提示中输入，PIN
使用掩码并由 Windows DPAPI 保护。Windows 1.2.0 支持 Codex 状态/动作和宠物
同步；BLE HID 由 Windows 原生处理，Unicode GATT 注入和蓝牙麦克风暂不提供。

Agent 第一次通过鉴权并发送心跳后，向导才完成。此时浏览器访问
`https://设备IP/`，确认首次生成的自签名证书，再以屏幕真实 PIN 登录 Web。
Pets 状态栏的 `B/W/M` 分别代表 BLE、Wi‑Fi 与鉴权 Machine Agent；
`B+W+M+` 表示三条链路均正常。

4. 在 Web 中点击键位打开弹窗，把按键设为“中文字符串”，或设为“组合键”后
   直接按下 `Alt+V` 等组合键采集。非直通键会显示真实用途；Settings 可修改
   PIN、Wi‑Fi 和 Profile。
5. 主界面为宠物动画。`Fn+;`/`Fn+.` 上下滚动，`Fn+,`/`Fn+/` 切换页面；
   在任何非宠物页面按反引号立即放弃未保存编辑并返回宠物页。首次向导中
   反引号只返回上一步，BLE passkey 输入始终具有最高优先级。
6. macOS 上短按 G0 开始/停止录音。G0 没有长按动作；设备重启或链路断开后
   只恢复 READY，不会自动恢复录音。

仅在诊断 macOS 安装器内部音频步骤时，可直接使用低层命令：

```bash
dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion doctor audio
sudo dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  install-audio-driver
sudo dist/CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  uninstall-audio-driver
```

普通卸载会移除 App、LaunchAgent、HAL 和 AudioBridge，但保留设备配置和日志：

```bash
dist/CardputerCompanion-mac-installer/install.sh uninstall
```

彻底清除配置和日志以进行干净安装验证：

```bash
dist/CardputerCompanion-mac-installer/install.sh uninstall --purge
```

Windows 从“已安装的应用”或 Start Menu 的 Uninstall 执行卸载；Windows
卸载始终移除 Agent、登录任务、配置、日志与快捷方式，不安装也不删除驱动。

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

公开发布门禁会检查所有保留的 Git refs、reflog、不可达对象和最终产物；候选值始终脱敏。`build/private/`、`dist/private/` 与任何 Wi‑Fi NVS/私有完整镜像都不属于公共发布物。

公共发布策略、平台边界和制品清单见
[PUBLIC_RELEASE.md](docs/PUBLIC_RELEASE.md)，Windows 安装与诊断见
[WINDOWS_AGENT.md](docs/WINDOWS_AGENT.md)。

自动化构建不能替代实机验证。本机未连接唯一 Cardputer 时，不会自动刷写，也不会声称完成 30 分钟 HIL。HIL 是在真实 Cardputer 与 Mac 上持续运行 30 分钟，同时验证 BLE HID、Wi‑Fi、Web、Companion、中文注入、延迟、堆栈与断连恢复。

设计、计划与进度：

- [产品化增量设计](docs/superpowers/specs/2026-07-24-product-firmware-companion-implementation-design.md)
- [产品实施计划](docs/superpowers/plans/2026-07-24-product-firmware-companion.md)
- [项目进度](docs/2026-07-24-cardputer-codex-companion_PROGRESS.md)
