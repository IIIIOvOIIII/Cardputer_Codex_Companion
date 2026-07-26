# Cardputer UI、输入隔离与 Settings 保存修复进度

## 2026-07-26 00:00 HKT

- Current work: 完成问题定位与交互设计，准备测试驱动实施。
- Expected result: 固定页面布局、输入隔离、Settings 操作和保存事务的实施边界。
- Result: Achieved。设计已由用户确认并提交为 `64019af`。
- Next step: 新增失败测试并采集 Settings 保存链路的精确错误证据。

## 2026-07-26 11:52 HKT

- Current work: 完成显示、页面输入隔离、Settings 新按键语义和保存快照实现。
- Expected result: 目标 host tests 先按预期失败，再在实现后通过；确认显示设置保存失败的具体根因。
- Result: Achieved。RED 分别捕获旧 PIN 掩码、缺失页面输入策略、缺失设置快照和无效存储键；GREEN 为 `ui_model`、`ui_navigation`、`settings_menu`、`device_settings` 4/4。显示设置原 NVS 键 `display_settings` 长 16 字符，超过 ESP NVS 15 字符上限，已更名为 `display_cfg` 并加入编译期约束。其余设置路径已改用 Enter 确认并增加非敏感错误码日志。
- Next step: 运行完整 host tests 和 ESP-IDF 编译，随后刷机采集真机保存与 HID 隔离证据。

## 2026-07-26 12:56 HKT

- Current work: 完成 1.0.31 编译、应用分区刷写、物理按键验收、重启持久化与最终发布检查。
- Expected result: 设备无重启循环，五项修复在真机成立，保存值跨重启存在，BLE/Wi-Fi/Agent 自动恢复。
- Result: Achieved。Python 134/134、普通 host 28/28、ASan/UBSan host 28/28、ESP-IDF 5.5.4 编译、Web 资源、分区布局与 `git diff --check` 均通过。最终应用镜像已写入 `/dev/cu.usbmodem21201` 的 `0x20000`，独立 `verify_flash` 返回 digest matched；用户物理验证首行对齐、真实 PIN、非 PET 零 HID、PET HID 和 Settings Enter 保存/反引号返回全部符合。重启后 NVS 解析确认 `display_cfg` 一项存在，运行状态为 version `1.0.31`、BLE/Wi-Fi/Agent 全部 `OK`。应用镜像 SHA-256 `06386af0a18d7a6db76c067d71f5a0ae0c98463035bdb175170b7e64bfa8481f`；generic full image SHA-256 `4744728dcd5d90241e2738f2bf0067b1617687201292a4ee5330f9131bf9c6cf`。
- Next step: 提交代码与文档，保留功能分支供用户决定后续集成。
