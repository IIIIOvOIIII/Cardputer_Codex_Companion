# Implementation Status

当前发布目标：`1.2.0`

已完成：

- 五页设备 UI、较大正文、滚动提示和无中间清屏的宠物动画。
- 统一 Companion snapshot 中的活跃会话、Model、Thinking、Fast 与可选限额。
- SAFE 加四个自定义 Profile 的双 bank 事务目录，以及兼容旧单 Profile 的迁移。
- PIN 鉴权 Web Profile 管理和设备端 Profile 二级菜单。
- 设备端 PIN 双次确认、五分钟窄迁移授权和 Mac Agent 原子配置更新。
- Wi‑Fi 异步扫描、候选试连、成功提交、失败回滚和 Hidden Network。
- CRC 保护的亮度、自动返回和 2/2.5/3 FPS 设置。
- BLE 配对、Settings 编辑、Settings 浏览、Fn 导航、宏/HID 的固定输入优先级。
- 首次启动向导依次完成设备端 Wi-Fi、已认证 BLE HID 配对和
  Machine Agent 心跳验证。
- Windows Machine Agent 支持 Codex 状态/动作、局域网认证、宠物同步、
  用户级自启动、安装与卸载。

边界：

- 仅支持局域网控制，不提供离开局域网后的远程通道。
- 设备不调用 Codex；macOS 或 Windows Machine Agent 是唯一
  app-server 客户端。
- Windows `1.2.0` 不提供麦克风或 Unicode GATT 输入。
- 缺失或无法可靠映射的限额不显示为 `N/A`。
- SAFE 不可修改或删除；目录最多包含四个自定义 Profile。
- Wi‑Fi 与 PIN 不通过一般状态 API 返回，也不写日志。
