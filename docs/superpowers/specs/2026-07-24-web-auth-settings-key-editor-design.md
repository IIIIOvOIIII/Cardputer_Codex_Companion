# Web 鉴权、Settings 与键位编辑设计

## 目标

将 Cardputer Codex Companion 的设备端 Web 控制台从“配置页内填写配对码”改为“先鉴权、后配置”的产品流程，并补齐 Settings、中文动作表述、组合键录入和非直通键用途展示。

## 已批准方案

采用最小可交付方案 A：保留现有 HTTPS、屏幕 PIN、`/api/v1/profile` 和 `/api/v1/wifi` 模型，新增一个受当前 PIN 保护的 PIN 修改接口，重构嵌入式 Web 前端。

不引入多用户、长期 bearer token、云端服务、外部服务器部署或新的 Web 框架。

## 功能设计

1. 首页首先显示 PIN 鉴权屏。用户输入当前 8 位 PIN 并通过 `/api/v1/profile` 验证后，才展示键盘配置页面。
2. 鉴权后显示两个选项卡：
   - 键盘配置：Profile 名称、Layer、56 键键盘、发布按钮。
   - Settings：修改 Web PIN、写入 Wi‑Fi SSID/密码。
3. 动作在 Web 中以中文展示，但 profile JSON 继续使用既有英文枚举值，保证固件运行时和已保存配置兼容。
4. 组合键编辑使用浏览器 `keydown` 捕获用户输入，例如 `Alt+V`，转换为 HID `modifiers` 和 `usages` 后写入 `hid_chord` 动作。
5. 非直通键在键帽上显示真实用途摘要，例如 `组合键 Alt+V`、`文本 请检查...`、`设备 切换模式`、`Codex 批准`、`禁用`。点击键帽后在页面内弹窗编辑。

## 固件 API 设计

新增：

```text
POST /api/v1/pin
Header: X-Cardputer-Pairing: 当前 8 位 PIN
Body: {"pin":"12345678"}
Response: {"saved":true}
```

PIN 必须是 8 位数字。固件启动时优先从 NVS `product/web_pin` 读取持久化 PIN；不存在或无效时生成随机 8 位 PIN。修改 PIN 后立刻更新内存中的鉴权码并持久化，后续请求必须使用新 PIN。Wi‑Fi 密码仍只写不读。

## 验收

- Web 源码和生成的 `web_assets.hpp` 必须一致。
- Python Web asset tests 覆盖鉴权屏、Settings、中文动作、组合键捕获、键帽用途摘要和 modal 编辑入口。
- Host firmware test 覆盖 `/api/v1/pin` 路由、PIN 长度/数字校验和版本号。
- Release gate 通过后刷入当前连接的 Cardputer。
- 实机启动后通过 HTTPS 访问设备 IP，输入 PIN 后可进入键盘配置页；Settings 可保存新 PIN 和 Wi‑Fi；非直通键键帽显示用途。
