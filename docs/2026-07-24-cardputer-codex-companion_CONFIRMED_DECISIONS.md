# Cardputer Codex Companion 已确认决策快照

状态：保留为 compact 前的历史确认快照。全部设计章节随后已经确认，实施应以[正式设计规格](superpowers/specs/2026-07-24-cardputer-codex-companion-design.md)为准。

本文档用于保留 compact 前的设计上下文，不再作为完整实施依据，也不单独授权开始实现。

## 1. 产品定位

Cardputer Codex Companion 是面向 macOS 的 Codex 遥控副屏，同时也是可编程蓝牙键盘。

首版只在同一局域网内控制 Codex，不要求离开局域网后继续控制，也不引入公网代理或公共消息中继。通信协议可以为未来扩展预留空间，但公网能力不属于 v1 范围。

## 2. 硬性要求

1. Cardputer 可通过蓝牙连接电脑，并作为标准键盘使用。
2. Cardputer 连接 Wi-Fi 时，暴露设备端 Web 控制页，允许用户自定义物理按键与快捷键的对应关系。
3. 快捷键宏支持：
   - 组合键；
   - 输出字符串；
   - 输出中文字符串。
4. Cardputer 副屏联动展示活跃 Codex 会话、运行状态等信息。

## 3. 配置档与安全边界

- 固件内置不可修改的 Safe Profile，确保设备始终有可靠的基础输入和恢复路径。
- 用户可以创建完全可重映射的自定义 Profile。
- Home 键以及开机恢复组合键始终保留，不能被用户映射覆盖。

## 4. Codex 遥控范围

首版按“完整遥控”设计，至少覆盖：

- 列出活跃和近期会话；
- 创建新会话；
- 恢复既有会话；
- 发送自由文本提示词；
- 批准或拒绝 Codex 动作；
- 回答 Codex 发起的用户输入请求；
- 中断当前 turn；
- compact 会话；
- 在预先配置或收藏的工作目录之间切换。

## 5. 总体架构

项目采用两个运行组件：

1. Cardputer 固件；
2. macOS Companion。

项目作为独立仓库建设，不直接扩展 `M5Cardputer_Incident_Monitor`，也不整体复制 `M5Cardputer-UserDemo-main`。现有项目只作为硬件驱动、键盘映射和实现模式的参考。

### 5.1 Cardputer 固件职责

固件负责：

- 物理键盘扫描；
- Profile 和宏解析；
- 小屏 UI；
- BLE HID 键盘；
- 自定义 BLE GATT UTF-8 文本通道；
- 连接 Wi-Fi 后托管本地 Web 控制页；
- 显示 Codex 会话及状态。

键盘扫描和 HID 发送具有最高运行优先级。Web、Codex 状态同步和 UI 更新使用彼此隔离的有界队列，网络异常不得造成按键阻塞或修饰键卡住。

### 5.2 macOS Companion 职责

Companion 以 LaunchAgent 形式运行，负责：

- 设备发现、配对和认证；
- 将设备发送的 UTF-8 文本注入 macOS；
- 对接本机 Codex App Server；
- 将 Codex 事件归一化为适合 240×135 屏幕显示的状态；
- 执行全部 Codex 会话控制动作。

### 5.3 三条独立传输通道

#### BLE HID

用于普通按键和组合键。即使 Wi-Fi 或 Companion 不可用，标准键盘能力仍应保持可用。

#### 自定义 BLE GATT UTF-8 通道

用于字符串，尤其是中文字符串。Cardputer 发送 UTF-8 文本，Companion 使用 macOS 原生 Unicode 输入能力完成注入。

该通道不依赖 Wi-Fi，但依赖 Companion 运行。

#### 经认证的局域网 WebSocket

用于 Codex 会话列表、状态事件和遥控命令。断线后通过事件游标或等价的 resume 机制恢复同步。

### 5.4 设备端 Web 控制

- Cardputer 连接 Wi-Fi 后提供本地 Web 控制页。
- 计划通过本地 mDNS 名称访问，例如 `cardputer-codex.local`。
- Web 控制页直接读写设备配置，不以 Companion 在线作为前提。
- Web 控制页用于配置物理按键、组合键、字符串宏和 Profile。

## 6. Codex 集成与安全原则

- Cardputer 不保存 OpenAI 或 ChatGPT 凭据。
- Cardputer 不直接操作 Codex 内部数据库或 JSONL 文件。
- Companion 通过官方 Codex App Server 执行会话读取和变更。
- 局域网通信必须经过设备配对与认证；“仅限局域网”不等同于“无需认证”。
- BLE HID、UTF-8 文本通道和 Codex 控制通道相互独立，单一通道故障不得破坏基础键盘能力。

## 7. 已确认的工程边界

- 新项目目录：`/Users/nicholasliao/clawd/Cardputer_Codex_Companion`
- 独立 Git 仓库。
- 首要平台：macOS。
- 首版网络范围：同一局域网。
- 当前阶段：只固化已确认设计，不编写生产实现代码。

## 8. 参考边界

后续实现可以参考以下本地工程，但不得默认继承其架构：

- `M5Cardputer-UserDemo-main/main/apps/app_keyboard/ble_keyboard/ble_keyboarad.cpp`：现有 BLE HID 键盘报告实现。
- `M5Cardputer-UserDemo-main/main/hal/keyboard/keymap.h`：Cardputer 键位映射参考。
- `M5Cardputer_Incident_Monitor/src/main.cpp`：现有单体式监控固件，仅作经验参考，不在其上直接扩展。
- `codex_shadow` 与 `Codex_on_Paper`：本地 Codex 会话联动架构参考。
- Codex App Server 官方文档：<https://learn.chatgpt.com/docs/app-server>

## 9. 快照建立时尚未决定的内容

建立本快照时，以下内容尚未确认；这些内容现已在正式设计规格中完成确认：

- Profile、宏的数据模型及详细安全规则；
- 设备端交互流程与各屏幕状态；
- Web 控制页的信息架构、认证和配对流程；
- Cardputer 与 Companion 的协议字段、版本机制和错误恢复细节；
- 配置持久化、迁移、备份和固件更新策略；
- 测试策略与分阶段实施计划。

下一步门禁是用户复核正式设计规格；复核通过后才能编制实施计划。
