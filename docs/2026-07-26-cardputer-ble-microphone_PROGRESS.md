# Cardputer BLE 麦克风与 macOS 虚拟输入进度

## 2026-07-26 14:14 HKT

- Current work: 完成现有 1.0.31 架构、SPM1423 硬件、ESP32-S3
  Bluetooth 能力和 macOS Core Audio 虚拟设备路径的只读调研，并逐段确认产品设计。
- Expected result: 固定音频传输、G0、隐私、安全、恢复、验证门禁和首发范围，避免把 ESP32-S3
  错误设计成不支持的 HFP 或标准 LE Audio 设备。
- Result: Achieved。用户确认自定义加密 BLE Audio GATT、24 kHz IMA-ADPCM、G0
  短按开关、系统级 Core Audio 虚拟麦克风、原 G0 功能全部迁入 Settings，以及分阶段硬件门禁。
- Next step: 完成设计规格自检并提交文档，等待用户复核后再编写实施计划；本阶段不修改代码。

## 2026-07-26 14:42 HKT

- Current work: 将已批准设计拆分为协议、固件采集与 BLE、Mac 音频管线、共享环形缓冲、HAL
  驱动、XPC、安装、产品集成和 HIL 的 TDD 实施任务。
- Expected result: 每个任务具备精确文件、接口、RED/GREEN 命令、提交边界和硬件停止门禁。
- Result: Achieved。计划以 `31fea44` 为基线，要求新建 `feat/ble-microphone`
  隔离工作树；24 kHz 十分钟真机门禁位于 HAL 扩展之前，失败时只能固定降为 16 kHz，不能放宽
  HID、堆、栈或丢帧阈值。
- Next step: 自检实施计划的规格覆盖、占位符和跨任务类型一致性，提交后由用户选择执行方式。

## 2026-07-26 15:31 HKT

- Current work: 完成发布基线门禁，并按 TDD 落地 Audio v1 传输协议、共享 fixture、
  Python 验证器和固件端无分配编解码器。
- Expected result: 严格接受 132-byte 24 kHz 与 92-byte 16 kHz 帧，拒绝未知版本、
  flags、rate、长度和任何远程启动 opcode。
- Result: Achieved。基线 Python 134/134、host 28/28、sanitizer 28/28、
  ESP-IDF 与 Companion 发布构建通过；新增 Python 5/5 和 C++ `audio_protocol`
  测试通过，控制面仅允许五个非启动 opcode。
- Next step: Task 2 先加入 C++/Swift 失败测试，再实现共享 golden vector 驱动的
  IMA-ADPCM 跨语言编解码。

## 2026-07-26 15:45 HKT

- Current work: 完成 Task 2 的无分配 C++ IMA-ADPCM 编码器、Swift 严格帧解析与
  解码器，以及共享 golden vectors。
- Expected result: 160/240 样本分别固定输出 84/124 bytes，跨语言按 low-nibble
  first 逐样本一致，非法 sample count、块长度、step index 和保留字节被拒绝。
- Result: Achieved。C++ 正常与 ASan/UBSan 测试通过；Swift debug/release 可执行
  测试均通过。当前仅安装 CommandLineTools，不提供可导入 XCTest/Testing module，
  因此沿用仓库既有 executable-test 模式，避免让所有历史 XCTest targets 阻塞验证。
- Next step: Task 3 用纯状态转换测试固定本地 G0 唯一启动权限、断连强制停止、
  重连不恢复，以及 24 kHz 到 16 kHz 的一次性降级。
