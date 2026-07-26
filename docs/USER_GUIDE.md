# Cardputer Codex Companion 1.0.31 用户指南

## 页面与方向键

主页面按 Pets、Device、Codex、Sync、Settings 循环排列。普通页面使用：

- `Fn+;`：向上滚动
- `Fn+.`：向下滚动
- `Fn+,`：上一页
- `Fn+/`：下一页

Pets 以 2、2.5 或 3 FPS 播放 Mac Companion 同步的当前 Codex 宠物。Device 显示版本、真实 PIN、BLE、Wi‑Fi 和 Agent。Codex 显示活跃会话、Model、Fast、Thinking Level 以及真实返回的 5H/Weekly 限额；无法识别或超过 120 秒的限额直接隐藏。Sync 显示 IP、Heartbeat、Pet Sync 与当前键盘 Profile。

只有 Pets 页面会把普通键盘输入发送给蓝牙 HID 或按键宏。Device、Codex、Sync 和 Settings 页面中的按键只供 Cardputer 本地操作。

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

若设备已有配置，升级时只能把 `firmware/build/cardputer_codex_companion.bin` 写到 `0x20000`。从 `0x0` 写 full image 会替换更多分区，只适合首次安装或明确恢复。
