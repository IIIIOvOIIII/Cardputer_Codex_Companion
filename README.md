# Cardputer Codex Companion

将 M5Stack Cardputer 作为 Codex 的局域网遥控副屏与可编程蓝牙键盘。

## 当前状态

设计进行中：产品边界与总体架构已经确认，尚未进入实现阶段。

## 硬性要求

- 通过蓝牙连接电脑并作为键盘使用。
- 连接 Wi-Fi 时提供设备端 Web 控制页，用于自定义物理按键与快捷键的映射。
- 快捷键支持组合键和字符串输出，并支持中文字符串。
- 副屏联动显示活跃 Codex 会话及其状态。

首版面向 macOS，仅要求同一局域网内控制 Codex，不包含公网访问能力。

## 总体组成

- Cardputer 固件：键盘扫描、配置档与宏、屏幕 UI、BLE HID、UTF-8 BLE 通道、设备端 Web 控制和 Codex 状态显示。
- macOS Companion：以 LaunchAgent 运行，负责设备认证、中文文本注入、Codex App Server 对接、状态归一化及远程控制。

详细确认事项见：

- [确认决策快照](docs/2026-07-24-cardputer-codex-companion_CONFIRMED_DECISIONS.md)
- [项目进度](docs/2026-07-24-cardputer-codex-companion_PROGRESS.md)
