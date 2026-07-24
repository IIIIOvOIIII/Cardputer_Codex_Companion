# Cardputer Codex Companion 产品化实施增量设计

**日期：** 2026-07-24  
**状态：** 已批准  
**基线：** `docs/superpowers/specs/2026-07-24-cardputer-codex-companion-design.md`

## 1. 背景与目标

现有 `cardputer_codex_phase0` 是并发和安全边界探针，不是可用产品固件。它没有初始化屏幕、启动物理键盘矩阵、连接 Wi-Fi、启动 Web 服务或运行 Codex Companion 会话链路。

本增量设计把现有探针组件升级为端到端可用版本，同时保留已经审查过的 BLE、HTTPS 限流、Web 安全和协议边界。最终交付包括：

- 可从 `0x0` 刷写的通用完整固件；
- 从 Lynx Vault 注入指定 Wi-Fi 配置的私有完整固件；
- 独立应用分区镜像；
- 可运行的 macOS Companion 与本机安装/授权说明；
- 自动化验证结果和明确的真机证据边界。

## 2. 已批准的实施选择

- 延续现有 ESP-IDF、NimBLE、HTTPS/WSS 和 Swift 架构，不改写为 Arduino 栈。
- 本轮同时完成固件和 macOS Companion；副屏会话、中文注入和 Codex 控制不能以静态演示数据代替。
- Wi-Fi 凭据通过 `lynx-vault` 读取：
  - `shared.wifi.ssid`
  - `shared.wifi.password`
- 凭据不进入源码、Git、日志、测试 fixture 或应用镜像。打包器生成 Git 忽略的私有 NVS 分区并合并成私有完整镜像。
- 未启用 Flash Encryption 的开发设备上，私有完整镜像及其 NVS 分区均视为敏感制品，不能公开分发。

## 3. 固件运行时架构

产品入口按以下顺序启动：

1. 初始化 Cardputer HAL、背光与 240×135 屏幕并显示启动进度；
2. 采样 `Home/G0 + Fn` 恢复组合键；
3. 初始化 NVS、加载设备身份、Wi-Fi 和最后有效 Profile；
4. 启动物理键盘矩阵扫描、去抖、输入路由和独立 HID 发送队列；
5. 初始化 BLE HID 与唯一加密 GATT 服务；
6. 连接预置或已保存 Wi-Fi；失败时保持离线 Keyboard Mode；
7. 启动设备 HTTPS Web 控制端；
8. 发现、认证并连接 macOS Companion；
9. 启动 UI 状态同步、指标和看门狗。

屏幕和键盘必须先于任何网络等待可用。Wi-Fi、Web、Companion 或 Codex 失败不得停止 BLE HID。

## 4. 屏幕与输入

### 4.1 启动与常驻屏幕

启动页逐项显示：

- Display
- Config
- Keyboard
- BLE
- Wi-Fi
- Web
- Companion

每项只能显示 `OK`、`OFFLINE` 或明确错误码。进入运行态后，常驻屏幕显示 BLE、Wi-Fi、Companion、当前 Profile、选中会话、工作目录简称、统一会话状态和 Approval/Input 数量。

### 4.2 键盘路径

物理键经过固定容量扫描和去抖队列进入 Input Router：

- Keyboard Mode：执行 Profile 绑定；
- Codex Remote Mode：普通键只操作设备 UI；
- Home/G0：在两种模式间切换；
- Home/G0 长按两秒：启用 Safe Profile、发送 `release all` 并返回 Keyboard Mode；
- 开机 `Home/G0 + Fn`：Safe Profile、禁止网络自动连接并打开恢复菜单。

HID 报告、组合键、宏结束、异常、模式切换和断连都必须发送完整 `release all`。

## 5. Wi-Fi 与私有 NVS 制品

### 5.1 正常配置

固件从 NVS 读取 SSID 与密码并使用显式超时连接 Wi-Fi。失败后：

- 屏幕显示 `WIFI OFFLINE`；
- BLE HID 保持可用；
- 用户可从设备菜单开启重新配网；
- 不进行无限阻塞重试。

### 5.2 首次与恢复配网

没有有效配置或用户主动触发时，设备开启临时 AP：

- SSID 含设备短 ID；
- 随机 AP 密码显示在屏幕；
- Captive Portal 只允许扫描、选择 SSID 和提交密码；
- 成功连接并持久化后关闭 AP；
- API、导出和日志不能读回密码。

### 5.3 私有制品生成

私有打包器：

1. 检查 Vault health 和 seal 状态；
2. 使用 service-account bearer 读取两个精确 scalar ref；
3. 在权限 `0600` 的临时目录生成 NVS 输入；
4. 生成固定大小的 NVS 分区；
5. 删除临时明文输入；
6. 合并 bootloader、partition table、NVS、application；
7. 输出制品大小与 SHA-256，不打印 SSID 或密码。

## 6. Profile、宏与 Web

Profile Store 使用 Safe Profile 加最多七个用户 Profile、最多四层、revision 和双槽校验。支持：

- `passthrough`
- `hid_chord`
- `text_utf8`
- `input_sequence`
- `device_action`
- `codex_action`
- `disabled`

UTF-8 段不超过 1024 bytes，序列不超过 16 步，总延时不超过十秒。文本操作只能通过加密 GATT 请求 Companion 注入，不得转成键盘布局序列。

设备 Web 控制端托管本地 SPA 和版本化 API，提供：

- 连接与设备状态；
- Profile、层和实体键位映射；
- 组合键、宏和 UTF-8 snippet 编辑；
- 草稿、校验、发布和 revision 冲突；
- 配置导入、导出与恢复；
- Companion 与 Web 管理客户端状态。

Web 继续使用设备唯一 HTTPS 身份、物理配对窗口、八位一次性码、物理确认、Host/Origin/CSRF、固定连接和请求预算。未认证客户端不能读取配置。

## 7. macOS Companion

现有 SwiftPM 骨架升级为一个可安装的本机 Companion：

- 使用本机 Codex App Server 的受支持接口；
- 规范化会话、turn、Approval、Input Request 和可用命令；
- 在用户选定的 LAN 接口发布 `_codex-companion._tcp.local`；
- 与 Cardputer 完成 P-256/SAS、WSS TLS exporter 和 BLE/GATT 双通道绑定；
- 通过 CoreBluetooth 接收 UTF-8 操作；
- 使用 `CGEvent.keyboardSetUnicodeString` 向前台应用注入文本；
- 使用 SQLite 账本去重，并返回 `completed`、`partial`、`failed` 或 `indeterminate`；
- Accessibility 缺失、Secure Input 或焦点变化时失败关闭；
- 不使用剪贴板、Command-V、任意 Shell、任意路径或原始 Codex RPC。

Cardputer 只接收稳定的 Companion DTO 和白名单命令。未知审批类型只能拒绝或转到 Mac 处理。

## 8. 故障与恢复

- 屏幕在网络和 BLE 前初始化；启动失败必须显示错误。
- Wi-Fi 断开：BLE 保持可用，Codex 状态为 `STALE`。
- Companion 或 Codex 断开：文本宏和写操作明确失败，HID 保持可用。
- Profile 损坏：使用上一有效槽；两槽都失败则使用 Safe Profile。
- Web revision 冲突：返回 `409`，不得覆盖。
- 队列溢出：普通状态可合并；关键状态无法保证时要求 resync。
- 任何宏异常、断连或受控重启：发送 `release all`。

## 9. 验收与证据

自动化验证包括：

- 固件 host tests：扫描、去抖、路由、宏、Profile、Wi-Fi 状态机和 UI model；
- ESP32-S3 `fullclean` 构建、镜像大小、分区和秘密扫描；
- Web API、静态页面、认证和 revision 测试；
- Swift 合约、Codex adapter、Unicode 分块、焦点检查和 SQLite 账本测试；
- Companion release build、CLI/LaunchAgent smoke test；
- 私有制品中 NVS 键存在但构建日志和 Git 不含凭据。

真机功能验收按顺序执行：

1. 开机立即有可见启动页；
2. 预置 Wi-Fi 自动连接，失败时仍可使用 BLE；
3. Web 控制页可在 LAN 访问并完成配对；
4. 实体键、修饰键和组合键真实输入；
5. 中文 UTF-8 字符串通过 Companion 真实注入；
6. 副屏真实显示活跃 Codex 会话和状态；
7. 之后才执行三十分钟 HIL 并发/资源测试。

没有 Cardputer、Accessibility 授权、完整 Xcode/XCTest 或目标应用时，对应证据必须标记为未验证，不能用结构测试代替。
