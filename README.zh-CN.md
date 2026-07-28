[English](README.md)

# Cardputer Codex Companion

Cardputer Codex Companion 将 [M5Stack Cardputer](https://docs.m5stack.com/zh_CN/core/Cardputer) 变成仅限局域网使用的 Codex 遥控副屏、可编程蓝牙键盘，以及 macOS 可选无线麦克风。

当前版本：**1.2.1**

## 项目介绍

固件同时提供：

- 标准 BLE HID 键盘；
- 240×135 Codex 宠物动画与会话状态副屏；
- 带 PIN 鉴权的局域网 HTTPS 配置控制台；
- 与 Machine Agent 联动的 Codex 状态、动作、宠物同步、Unicode 输入，以及 macOS 麦克风。

公共固件不包含 Wi-Fi 凭据、设备 PIN、配对材料、私有证书或公网远控服务。新设备完全通过 Cardputer 本机完成初始化，并仅在同一局域网内工作。

## 主要功能

- 完整 56 键矩阵扫描及 BLE HID 输出。
- 四层键盘 Profile，支持直通、组合键、UTF-8 字符串、多步骤序列、设备动作与 Codex 动作。
- `Alt+V` 等组合键和 HID 可表达的 ASCII 字符串直接走 HID，不依赖 Machine Agent。
- Web 端可配置按键、Wi-Fi、PIN、Profile、宠物、屏幕和超时。
- 八位 PIN 持久化保存，只有用户主动 rotate 时才改变。
- 副屏显示 Session、Model、Fast、Thinking、审批、输入和可用限额。
- Pets、Device、Codex、Sync、Settings 五个页面。
- 设备端首次 Wi-Fi、BLE 和 Agent 初始化向导。
- macOS 支持中文/Unicode GATT 输入和加密蓝牙麦克风。
- 公共发布门禁覆盖可复现构建、测试、内存、制品白名单、校验和与全历史凭据审计。

## 平台边界

| 组件 | 支持平台 | 说明 |
| --- | --- | --- |
| 固件 | M5Stack Cardputer / ESP32-S3 | 必需 |
| BLE 键盘 | macOS、Windows、iPadOS、iOS 等 BLE HID 主机 | 系统原生 HID |
| Machine Agent | macOS 14 或更高 | 完整功能 |
| Machine Agent | Windows 10 22H2 / Windows 11 | Codex 状态/动作及宠物同步 |
| 蓝牙麦克风 | 仅 macOS | 安装 HAL 与 AudioBridge |
| Unicode GATT 注入 | 仅 macOS | Windows 1.2.1 暂不提供 |

## Cardputer 固件刷入

需要 Cardputer、可传输数据的 USB-C 线、Python 3 和 [esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/installation.html)。

先校验发布制品：

```bash
shasum -a 256 -c 1.2.1-SHA256SUMS
```

新设备或恢复出厂设备使用通用完整镜像：

```bash
python3 -m esptool --chip esp32s3 \
  --port /dev/cu.usbmodemXXXX -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x0 cardputer_codex_companion-full.bin
```

Windows 将串口改为 `COM5` 等实际端口，并使用 `py -m esptool`。
原厂固件、M5Launcher 及其它第三方固件可能采用不同分区表；在使用下方
app-only 升级命令前，必须至少完成一次从 `0x0` 写入完整镜像。

如需保留 PIN、Wi-Fi、Profile、宠物、初始化进度及 BLE bond，只刷应用分区：

```bash
python3 -m esptool --chip esp32s3 \
  --port /dev/cu.usbmodemXXXX -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x20000 cardputer_codex_companion.bin
```

不要把应用分区镜像写到 `0x0`。

## 首次初始化

空白设备必须依次通过三道门槛；完成前不会开放普通 HID 输出和 Web 控制台。

1. **在 Cardputer 连接 Wi-Fi。** 使用 `;`/`.` 移动、Enter 选择、反引号返回。密码在 Cardputer 输入并以掩码显示，只有成功取得 IP 后才保存；支持隐藏 SSID。
2. **从电脑发起蓝牙配对。** 在系统蓝牙设置中配对 **Cardputer Codex**。需要验证码时，在 Cardputer 输入并按 Enter。只有建立鉴权 bond 且 HID 已订阅后才进入下一步。
3. **安装并配对 Machine Agent。** 输入屏幕上的设备 IP 和八位 PIN。设备收到首次鉴权心跳后完成初始化。

随后访问 `https://设备IP/`，接受设备本地生成的证书，并用同一 PIN 登录。

## Agent 安装指引

Agent 在本机运行 `codex app-server --listen stdio://`，同步 Codex 状态并转发经过鉴权的动作。首次配对会固定 Cardputer 证书指纹。

### macOS

从源码仓库安装时，直接使用项目根目录的安装器：

```bash
./install.sh install
./install.sh status
```

如果 `dist/CardputerCompanion.app` 不存在，`install` 会先完成构建，再进入受保护的
Python 安装器；`status` 和 `uninstall` 不会触发构建。

预构建的 `CardputerCompanion-mac-installer` 发布目录提供相同命令：

```bash
cd CardputerCompanion-mac-installer
./install.sh install
./install.sh status
```

安装器交互询问 `https://设备IP` 和 PIN；PIN 使用掩码，只写入权限为 `0600` 的配置，不进入命令行、LaunchAgent 或日志。

安装内容包括用户 App、LaunchAgent、`Cardputer Codex Microphone` HAL 以及 AudioBridge。

仅在诊断音频安装时使用底层命令：

```bash
CardputerCompanion.app/Contents/MacOS/cardputer-companion doctor audio
sudo CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  install-audio-driver
sudo CardputerCompanion.app/Contents/MacOS/cardputer-companion \
  uninstall-audio-driver
```

普通卸载保留配对配置和日志：

```bash
./install.sh uninstall
```

干净卸载同时删除配对配置和日志：

```bash
./install.sh uninstall --purge
```

### Windows

Windows x64 运行：

```text
CardputerCompanion-1.2.1-windows-x64-setup.exe
```

安装器写入 `%LOCALAPPDATA%\CardputerCodexCompanion`，创建当前用户最低权限登录任务，并添加 Pair Device、Status、Doctor 与 Uninstall 菜单。它不安装驱动或系统服务，也不要求管理员权限。

Windows ARM64 解压 `CardputerCompanion-1.2.1-windows-arm64.zip` 后运行：

```text
cardputer-agent.exe pair
cardputer-agent.exe status
cardputer-agent.exe doctor
```

PIN 由当前 Windows 用户的 DPAPI 保护。可从“已安装的应用”或开始菜单卸载。Windows 1.2.1 不包含 Unicode GATT 注入和蓝牙麦克风。

## Web 与设备操作

访问 `https://设备IP/` 并使用设备 PIN 登录。点击实体键位可修改真实动作；组合键输入框会直接采集按下的组合；中文等 Unicode 字符串由 macOS Agent 注入。

- `Fn+;` / `Fn+.`：上下；
- `Fn+,` / `Fn+/`：前后页面；
- Settings 内使用裸 `; . , /` 与 Enter；
- 反引号：取消编辑或从任意非宠物页面返回；
- 短按 G0：在音频 READY 后启动/停止 macOS 麦克风。

只有 Pets 页面会把普通键盘输入发送到 HID；BLE 验证码输入优先级最高。
短按 G0 是唯一录音开关。设备重启、链路断开或 Agent 退出后，麦克风只恢复到
READY，不会自动恢复录音。Pets 顶部的 `B/W/M` 分别表示 BLE、Wi-Fi 和已鉴权
Machine Agent 状态。

## 构建与验证

需要 ESP-IDF 5.5.4、CMake、Python 3.11/`uv`、Swift 6、Go 和 NSIS。

完整公共发布门禁：

```bash
scripts/verify_product_release.sh
```

常用聚焦测试：

```bash
PYTHONPATH=. uv run pytest -q
cmake -S firmware/test/host -B build/product-host
cmake --build build/product-host -j
ctest --test-dir build/product-host --output-on-failure
scripts/build_web_assets.py --check
```

公共制品包括完整/应用固件、macOS 安装器、Windows x64 安装器、amd64/ARM64 ZIP 和 `1.2.1-SHA256SUMS`。

安全与制品边界见 [PUBLIC_RELEASE.md](docs/PUBLIC_RELEASE.md)，Windows 细节见 [WINDOWS_AGENT.md](docs/WINDOWS_AGENT.md)。

## 安全与隐私

- 设备只在局域网内工作，并要求 PIN 鉴权。
- 公共固件不含 Wi-Fi、PIN、配对信息或私有证书。
- Wi-Fi 密码与 PIN 不写日志。
- HTTPS 身份由设备首次启动生成。
- Agent 固定设备证书指纹。
- 发布门禁扫描所有 Git refs、reflog、保留不可达对象、当前文件和制品，但不会打印疑似 secret。
- `build/private/`、`dist/private/`、私有 NVS、音频采样和含凭据固件禁止发布。

## 作者

**Lynx**（[hi@iam.lc](mailto:hi@iam.lc)）

## 协议

Copyright 2026 Lynx.

本项目使用 [Apache License 2.0](LICENSE)。
