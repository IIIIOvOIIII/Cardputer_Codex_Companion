# Cardputer Codex Companion 1.3.0 用户指南

## 首次初始化

全新或恢复出厂设备按 Wi‑Fi、蓝牙、Machine Agent 三步初始化：

1. Cardputer 扫描 Wi‑Fi；使用 `;`/`.` 选择，Enter 确认，密码在设备端掩码输入。
2. 在电脑端发起 `Cardputer Codex` 蓝牙配对，并在 Cardputer 完成验证码输入。
3. 安装 macOS 或 Windows Agent，输入设备 HTTPS 地址与当前八位 PIN。只有
   鉴权 Agent 心跳到达后，设备才进入 Pets 主界面。

向导中反引号返回上一步。已完成初始化后可在 Settings 选择 Run Setup Again；
该动作要求 PIN 鉴权和明确确认，不会由普通 Web 请求触发。

## 页面与方向键

主页面按 Pets、Device、Codex、Sync、Settings 循环排列。普通页面使用：

- `Fn+;`：向上滚动
- `Fn+.`：向下滚动
- `Fn+,`：上一页
- `Fn+/`：下一页
- 反引号：在任意非 Pets 页面立即取消未保存输入并返回 Pets。

Pets 以 2、2.5 或 3 FPS 播放 Mac Companion 同步的当前 Codex 宠物。Device 显示版本、真实 PIN、BLE、Wi‑Fi 和 Agent。Codex 显示活跃会话、Model、Fast、Thinking Level 以及真实返回的 5H/Weekly 限额；无法识别或超过 120 秒的限额直接隐藏。Sync 显示 IP、Heartbeat、Pet Sync 与当前键盘 Profile。

只有 Pets 页面会把普通键盘输入发送给蓝牙 HID 或按键宏。Device、Codex、Sync 和 Settings 页面中的按键只供 Cardputer 本地操作。

BLE passkey 输入优先于页面导航；配对验证码期间数字不会发送给 HID。

## Settings

进入 Settings 后，不按 Fn 使用：

- `;`/`.`：上下选择。
- `,`/`/`：减小/增大当前值，或在子菜单选择上一个/下一个值。
- Enter：进入、确认或保存。
- 反引号：取消或返回；在根菜单返回 Pets。

文本编辑时 Enter 保存、反引号取消、Backspace 删除。

菜单支持：

- Keyboard Profile：在 SAFE 与最多四个自定义 Profile 间切换。
- Change PIN：连续输入两次相同的八位数字。旧 PIN 仅在五分钟内用于 Mac Agent 迁移，新 PIN 一旦成功鉴权即提前撤销旧 PIN。
- Bind Wi-Fi：扫描并按信号强度显示最多 12 个唯一 SSID，另提供 Hidden Network。新凭据仅在取得 IP 后提交；失败或超时会回连原网络。
- Brightness：25%、50%、75%、100%。
- Return to Pet：Off、15、30、60 秒；编辑、扫描和连接期间暂停计时。
- Pet FPS：2、2.5、3。

PIN 与 Wi‑Fi 密码输入均使用掩码。亮度、自动返回、FPS、PIN、Wi‑Fi override 和当前 Profile 会持久化。

## Profile 与升级

Profile Catalog 使用 storage 分区末尾两个 64 KiB bank 做 CRC 和序列号保护。SAFE 固定存在且不可修改或删除；首次从旧固件升级时，旧配置会导入自定义 Profile。

若设备已使用官方 Factory 分区并需要保留配置，升级时把
`Cardputer-Codex-Companion-1.3.0-app.bin` 写到 `0x20000`。不要把固定偏移
应用镜像用于 M5Launcher 或未知分区布局。Factory 完整镜像从 `0x0` 写入并会
移除 Launcher 与设备配置；需要保留 Launcher 时，先升级到 M5Launcher 2.8.0+
再通过 Launcher 安装 `Cardputer-Codex-Companion-1.3.0l-launcher.bin`。

## Machine Agent

- macOS 1.3.0：Codex 状态/动作、宠物同步、Unicode GATT、BLE 麦克风和
  `Cardputer Codex Microphone`。
- Windows 1.3.0：Codex 状态/动作和宠物同步。BLE HID 由系统原生处理；
  Unicode GATT 与蓝牙麦克风不在此版本范围。

macOS 使用 `CardputerCompanion-mac-installer/install.sh`；彻底清理时执行
`uninstall --purge`。Windows 使用 x64 安装器或 ARM64 可移植包，PIN 由 DPAPI
保护；Windows 卸载会同时删除登录任务、配置和日志。
