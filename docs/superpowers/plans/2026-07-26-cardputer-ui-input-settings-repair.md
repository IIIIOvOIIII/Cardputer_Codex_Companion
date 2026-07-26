# Cardputer UI、输入隔离与 Settings 保存修复实施计划

> **For Codex:** Use test-driven development and verification-before-completion. Execute tasks in order and keep the progress document current.

**Goal:** 修复状态页显示、页面级 HID 隔离、Settings 本地操作和所有设置保存失败，并向当前连接的 Cardputer 部署已验证固件。

**Architecture:** 在 UI 模型中提供可测试的显示与输入策略；Settings 菜单只产生类型化命令快照；产品控制器负责持久化、运行时应用和结果回传。保留现有存储格式与 BLE 配对路径。

**Tech Stack:** ESP-IDF/C++、M5Cardputer、NimBLE HID、NVS、CMake/CTest、串口 HIL。

---

## Task 1：锁定显示行为

**Files:**
- Modify: `firmware/test/host/test_ui_model.cpp`
- Modify: `firmware/main/product/ui_model.cpp`
- Modify: `firmware/main/product/display.cpp`

1. 添加失败测试，要求 DEVICE 页包含真实 PIN，并按 Version、PIN、BLE、Wi-Fi、Agent 排列。
2. 运行目标测试，确认旧实现因 `PIN:********` 失败。
3. 修改 UI 模型并将正文绘制起点改为 `x=0`。
4. 重跑目标测试。

## Task 2：实现页面级 HID 隔离

**Files:**
- Modify: `firmware/test/host/test_ui_navigation.cpp`
- Modify: `firmware/main/product/ui_navigation.hpp`
- Modify: `firmware/main/product/ui_navigation.cpp`
- Modify: `firmware/main/product/product_controller.cpp`

1. 添加失败测试，覆盖仅 PET 页面允许主机输入。
2. 增加纯页面输入策略函数。
3. 在控制器中保留配对和页面导航优先级；非 PET 页消费其余事件，不入宏队列、不发送 HID。
4. 离开 PET 时释放 HID；返回后不重放被捕获按键。
5. 运行导航与控制器相关测试。

## Task 3：重构 Settings 页面按键语义

**Files:**
- Modify: `firmware/test/host/test_settings_menu.cpp`
- Modify: `firmware/main/product/settings_menu.hpp`
- Modify: `firmware/main/product/settings_menu.cpp`
- Modify: `firmware/main/product/product_controller.cpp`

1. 添加失败测试，覆盖 `;` 上、`.` 下、`,` 减/前项、`/` 加/后项、Enter 确认、反引号返回。
2. 添加根菜单反引号返回 PET 的菜单动作。
3. 保证文本编辑状态中 Enter 保存、反引号取消、Backspace 删除。
4. 实现滚动选择不越界。
5. 运行 Settings 菜单测试。

## Task 4：定位并修复 Settings 保存共性故障

**Files:**
- Modify: `firmware/test/host/test_settings_menu.cpp`
- Modify: `firmware/test/host/test_device_settings.cpp`
- Modify: `firmware/main/product/settings_menu.hpp`
- Modify: `firmware/main/product/settings_menu.cpp`
- Modify: `firmware/main/product/product_controller.cpp`
- Modify as required: existing Profile/PIN/Wi-Fi/device settings persistence adapters

1. 用主机测试确认命令携带保存时的完整值快照，且菜单不提前声明成功。
2. 通过串口复现至少一个失败项，记录失败命令类型和底层错误码，不记录敏感值。
3. 修复确认的共性根因；若存在独立后端错误，逐条修复但不迁移存储格式。
4. 成功后更新运行时状态；失败保留编辑上下文并显示具体失败项。
5. 运行 Settings、存储和控制器相关测试。

## Task 5：完整验证与发布

**Files:**
- Modify: `docs/2026-07-26-cardputer-ui-input-settings-repair_PROGRESS.md`
- Modify: `memory/2026-07-26.md`（workspace task memory）

1. 运行全部 host tests。
2. 编译固件和完整刷写镜像，记录产物路径与 SHA-256。
3. 刷入当前串口设备，监控启动日志并确认无重启循环。
4. 真机验证五个页面首行、真实 PIN、非 PET 页面零 HID、PET 正常 HID。
5. 逐项验证 Profile、PIN、Wi-Fi、Brightness、Return、Pet FPS 保存与重启持久性。
6. 更新进度与当日 memory，提交全部代码和文档。
7. 检查 git 状态与远端；若无远端，明确报告未推送原因。
