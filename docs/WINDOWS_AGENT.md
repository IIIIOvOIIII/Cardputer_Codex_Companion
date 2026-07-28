# Windows Machine Agent 1.2.1

## 支持范围

Windows Agent 提供：

- 通过局域网 HTTPS 与 Cardputer 双向同步 Codex 会话状态和动作；
- 活跃 session、model、Thinking、Fast 与可用限额；
- 与 macOS 相同的宠物选择、WebP 转码、CCPT v1 和断点续传；
- Windows 原生 BLE HID 键盘路径；
- 当前用户 DPAPI 配对材料、证书指纹固定、登录自动启动和诊断命令。

本版本不提供 Windows Unicode GATT 注入和 Cardputer 蓝牙麦克风。它不安装
驱动、服务，不要求管理员权限。

## 安装

x64 运行：

```text
CardputerCompanion-1.2.1-windows-x64-setup.exe
```

ARM64 解压 `CardputerCompanion-1.2.1-windows-arm64.zip`，在目录中运行：

```text
cardputer-agent.exe pair
```

安装器写入 `%LOCALAPPDATA%\CardputerCodexCompanion`，创建当前用户最低权限的
登录 Scheduled Task，并在 Start Menu 添加 Pair Device、Status、Doctor 和
Uninstall。

首次 pairing 会交互询问 `https://设备IP` 和八位 PIN。PIN 不接受命令行参数；
控制台中使用掩码。首次 PIN 鉴权成功后保存设备叶证书 SHA-256，之后证书变化
会被拒绝。PIN 使用当前 Windows 用户的 DPAPI 加密。

## 运行和诊断

```text
cardputer-agent.exe status
cardputer-agent.exe doctor
cardputer-agent.exe --version
```

`status` 验证配置、固定证书下的设备连通性和 Scheduled Task。`doctor` 只输出
DPAPI、Codex、配置、设备和自动启动的状态，不输出 PIN、设备地址、证书指纹或
HTTP body。Agent 日志最多保留当前 2 MiB 文件和一个轮换文件。

## 卸载

从 Windows“已安装的应用”或 Start Menu 的 Uninstall 卸载。卸载会删除：

- `Cardputer Codex Companion` Scheduled Task；
- Agent、任务模板、注册脚本和 uninstaller；
- `%LOCALAPPDATA%\CardputerCodexCompanion` 下的 DPAPI 配置和日志；
- Start Menu 快捷方式与当前用户卸载注册项。

卸载不触碰 Codex、本机蓝牙配对或其他应用。

## 验证边界

公共构建会在 macOS 上完成 Go 单元/race 测试、amd64/ARM64 交叉编译、PE 架构
检查、可复现 ZIP 和 NSIS 静态安装策略测试。Windows 实机首次安装、Scheduled
Task 运行和卸载仍必须在 Windows HIL 上完成后才能标记为运行时已验证。
