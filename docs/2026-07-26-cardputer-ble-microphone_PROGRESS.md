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

## 2026-07-26 16:05 HKT

- Current work: 完成 Task 3 纯麦克风状态机，并执行首批协议/codec/state 集成门禁。
- Expected result: 启动仅来自 READY 状态下的本地 G0 click；hold/repeat 无效；断连与
  reset 强制停止且不自动恢复；连续两个坏窗口只允许 24→16 kHz 降级一次，16 kHz
  再失败则停止并进入 ERROR。
- Result: Achieved。普通 host 31/31、ASan/UBSan 31/31、Python fixture 5/5、
  Swift debug/release audio tests 和 ESP-IDF 5.5.4 目标构建均通过；固件镜像仍有
  50% app partition 空间。
- Next step: 提交 Task 3 并在首批检查点等待复核；下一批从 SPM1423 PDM capture
  adapter 开始，随后接入加密 Audio GATT 和 G0 controller。

## 2026-07-26 16:20 HKT

- Current work: 完成 Task 4 SPM1423 PDM capture adapter 和 host/target 生命周期验证。
- Expected result: GPIO43 clock、GPIO46 data、16-bit mono PCM，24/16 kHz 固定
  10 ms 帧；启用前关闭 Speaker；启动后不再配置或分配，stop 幂等并累计 overrun。
- Result: Achieved。host `audio_capture` 测试通过，ESP-IDF 5.5.4 依照 pinned
  `i2s_pdm_rx_config_t` ABI 完成 reconfigure/build；镜像大小 `0x17f120`，app
  partition 仍有 50% 空间。内建 `esp_driver_i2s` 通过 CMake component dependency
  声明，不需要向 `idf_component.yml` 添加外部 registry 依赖。
- Next step: Task 5 扩展现有 Companion service 的三个加密 Audio characteristics，
  并实现单次尝试、压力即丢帧的 HID-priority transport。

## 2026-07-26 16:40 HKT

- Current work: 完成 Task 5 加密 Audio GATT characteristics 与非阻塞 transport。
- Expected result: Audio Data/Control/Status 使用 UUID 0005/0006/0007；notify/write
  均要求加密，Control 还要求当前 Companion；Data 和 Status subscription 与 HID、
  Unicode 及彼此独立；发送只尝试一次，buffer pressure 直接计为 drop。
- Result: Achieved。`ble_manifest` 与 `ble_audio_transport` host 测试通过，未知或
  remote-start control opcode 在进入 handler 前被 Audio v1 parser 拒绝；ESP-IDF
  目标构建通过，镜像 `0x17f790`，app partition 仍有 50% 空间。
- Next step: Task 6 实现 controller 的 capture→ADPCM→packet→transport 固定路径，
  把 G0 从旧 mode 切换中移除并接入短按麦克风开关，同时扩展只读 runtime metrics。

## 2026-07-26 17:00 HKT

- Current work: 完成 Task 6 麦克风 controller、G0 运行时、固定静态音频任务与
  有界事件队列，并接入 BLE subscription + Mac sink-ready 双重就绪门禁。
- Expected result: G0 仅在一秒内的完整短按释放后切换录音；采集帧固定经
  PCM→IMA-ADPCM→Audio v1 packet→单次 notify；断连优先停录且重连不恢复；
  24 kHz 持续丢帧只降级一次，16 kHz 再失败进入 ERROR；指标包含 capture、
  overrun、transport drop 和 fallback。
- Result: Achieved。新增 controller 测试覆盖 unavailable、24/16 kHz、132/92-byte
  packet、sequence wrap、drop、overrun、discontinuity、fallback、error、断连清空、
  重连不恢复和长按忽略；host 34/34、Task 6 ASan/UBSan 2/2、ESP-IDF 5.5.4
  目标构建均通过。镜像大小 `0x185860`，app partition 仍有 49% 空间。
- Next step: 提交 Task 6；Task 7 在 Mac 侧实现 ADPCM frame decode 后的 jitter
  buffer、24/16→48 kHz 自适应重采样和无锁音频 pipeline。

## 2026-07-26 17:20 HKT

- Current work: 完成 Task 7 Mac 音频核心，包括固定容量 jitter buffer、自适应
  48 kHz mono resampler、专用串行 AudioPipeline 和只含非内容计数的 metrics。
- Expected result: 60–100 ms 目标深度；缺一帧补精确 10 ms 静音；重复、迟到帧
  丢弃且不重传；sequence wrap 正确；start/discontinuity/rate change flush；24/16 kHz
  一秒输入均精确产生 48,000 samples，watermark 修正限制在 ±500 ppm。
- Result: Achieved。新增 executable tests 覆盖顺序、gap、duplicate、late、wrap、
  flush、bit-exact silence、ppm 边界、fixture decode、sink write size 和 metrics；
  Swift debug 与 release 构建均输出 `ProductAudio tests passed`。当前
  CommandLineTools 环境继续采用仓库既有 executable-test 方式。
- Next step: 提交 Task 7；Task 8 将 Unicode 与 Audio 收敛到同一个
  `CBCentralManager`/peripheral owner，并在真机 HAL 扩展前执行十分钟 BLE
  可行性门禁。

## 2026-07-26 17:45 HKT

- Current work: 完成 Task 8 统一 ProductGATT owner、BLE 音频接收、metrics-only
  `audio-probe` CLI 和 Python HIL gate runner。
- Expected result: 仅一个 `CBCentralManager` 与一个 peripheral；Unicode 和 Audio
  characteristics 同次发现；BIND1 成功后才启用 Audio notify，两个 subscription
  生效后依次写 hello/sink-ready；主动退出先写 sink-not-ready；断连 reset pipeline
  且仅意外断连计 reconnect；报告禁止 PCM/ADPCM/payload/sample 内容。
- Result: Achieved。ProductGATT executable tests debug/release 通过，Python HIL
  tests 4/4 通过；Companion release App 构建、ad-hoc codesign、`--version` 和
  `doctor` 通过；`audio-probe --duration 9` 以 EX_USAGE 64 拒绝且未创建报告。
  为保留兼容性，无 audio sink 的常规 Unicode 模式仍只要求原有 0002/0003
  characteristics。XCTest 仍受本机 CommandLineTools 缺少 module 的既有边界限制。
- Next step: 提交 Task 8；Task 9 执行 pre-flash 全门禁、app-only 刷写和十分钟
  24 kHz + concurrent HID 真机可行性验证，失败时仅允许固定降为 16 kHz。
