# Cardputer BLE 麦克风与 macOS 虚拟输入进度

## 2026-07-27 10:05 HKT

- Current work: 定位 16 kHz 帧稳定但 Mac 输入电平为零的采集根因，并固化
  M5.Mic 全量替换设计和实施计划。
- Expected result: 不改变 Audio v1、BLE、HAL 和 G0 行为，仅替换硬件采集后端，
  复用 M5Unified 的 Cardputer 右 PDM 槽、去直流、oversampling 和增益处理。
- Result: Achieved。当前直接 ESP-IDF mono 宏固定选择左槽，而 M5Unified
  Cardputer 板级实现选择 `input_only_right`；用户已明确选择完全改用
  `M5.Mic`。设计要求固定双缓冲、100 ms 有界等待、无运行期分配和只含聚合电平
  指标的实机门禁。
- Next step: 按 TDD 先固定 M5.Mic 硬件参数与双缓冲完成顺序，再替换后端、
  app-only 刷写并验证 Mac 侧非零输入电平。

## 2026-07-26 20:56 HKT

- Current work: 固化 USB 串口 HIL 麦克风控制设计，并把解析器、固件事件接入、
  PET 动画隔离、Python 自动启停和真实 Core Audio probe 路径拆分为 TDD 实施任务。
- Expected result: 后续调试无需用户反复按 G0；接口仅限物理 USB，不新增局域网或
  BLE 远程录音入口；自动短测能够复现并记录约八秒后的降级/ERR，同时让 HAL
  虚拟麦克风真正收到 probe 音频。
- Result: Achieved。设计提交为 `514ba98`；实施计划明确保留现有加密、绑定、
  subscription、sink-ready、首帧和 metrics-only 门禁，并要求任何异常路径均发送
  STOP。MIC ERR 的修复不预设结论，先由自动短测区分采集、BLE transport、TLS
  竞争和 HAL 路径。
- Next step: 用户选择执行方式后，按 Task 1 从 allocation-free parser 的
  RED 测试开始实施。

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

## 2026-07-26 15:12 HKT

- Current work: 补齐 Task 9 真机门禁所需的固件侧只读运行指标，并为产品 HID
  路径加入从矩阵稳定到入队完成的延迟直方图。
- Expected result: USB 串口每秒输出一条不含音频内容的 JSON，覆盖 internal heap、
  largest free block、allocation failure、HID p95、麦克风计数器，以及 scanner、
  HID、macro、audio、UI 五个任务的 stack high-water；采集不能阻塞音频任务。
- Result: Achieved。先以缺失 API 得到 host RED，再实现指标并通过 GREEN；
  Swift audio/GATT/pet/telemetry tests、Python 143 tests、host normal/sanitizer
  34/34、ESP-IDF 5.5.4 build、分区/DIRAM 检查、release app/package/hash 全部门禁
  通过。应用镜像 `0x186100`，app partition 保留 49%，DIRAM headroom
  113,529 bytes。
- Next step: 提交指标修复，随后只刷写 `0x20000` 应用分区，先做短时 boot/metrics
  smoke，再运行十分钟 24 kHz + concurrent HID 真机门禁。

## 2026-07-26 15:30 HKT

- Current work: Task 9 app-only 刷写后执行真实资源 smoke，并对不满足原门槛的结果
  进行根因分析与最小内存调优设计。
- Expected result: 保持 64/32/40 KiB heap/largest/TLS 门槛不变，找到不改变产品
  行为且可量化的内存回收路径。
- Result: Partial。刷写与独立 `verify_flash` digest matched；设备无 panic、reboot
  或 allocation failure，五个已报告任务满足 stack 门槛。但实测 stable heap/largest
  约 38/21 KiB，HTTPS transient 最低约 25 KiB。历史 Phase 0 HIL 明确记录
  `capture_complete=false`，说明门槛从未在完整产品运行时被证明。主分支对比显示
  麦克风仅增加约 7 KiB 静态 DIRAM；已批准采用宠物逐行绘制回收 19,968 bytes，
  并按 high-water 缩减 scanner/macro/audio 栈再回收 9,216 bytes。
- Next step: 按已批准的内存调优设计执行 RED→GREEN，实现后重新完成全门禁、
  app-only 刷写与资源 smoke；原门槛全部通过后才开始十分钟音频门禁。

## 2026-07-26 16:30 HKT

- Current work: 完成已批准的最小运行时内存调优、全量发布验证，并将最终构建重新
  刷入 Cardputer 后执行串口与 HTTPS 并发烟测。
- Expected result: 宠物逐行解码与绘制不保留整帧；按实测 high-water 缩减静态栈；
  证书临时缓冲在 HTTPS 启动后释放；启动顺序与一次性连续堆保留块避免 BLE/TLS
  碎片化；稳态、TLS 窗口、栈和 allocation gate 全部通过。
- Result: Achieved。Python 148 项、host normal 35/35、ASan/UBSan 35/35、
  ProductAudio/ProductGATT/ProductPet/ProductTelemetry 和 ESP-IDF release build
  全部通过；静态 DIRAM headroom 为 145,185 bytes。60 秒真实压力窗口中稳态
  free heap 最低 65,764、largest block 最低 36,864；TLS 窗口 free heap 最低
  60,780；allocation failures 为 0；各任务 stack free 均不低于 1,104 bytes。
  最终 app 镜像 `0x186670` 已 app-only 刷入，独立 verify_flash digest matched；
  额外 HTTPS 烟测无 reboot、panic 或 allocation failure。
- Next step: 提交调优改动；为十分钟音频 HIL runner 增加自动 HTTPS 请求，随后执行
  G0 激活的 24 kHz 音频、并发 HID 与 TLS 真机门禁。

## 2026-07-26 17:20 HKT

- Current work: 补齐十分钟 HIL 的自动 HTTPS 压力、局域网 URL 约束和首帧启动门禁，
  并准备 24 kHz 真机音频验证。
- Expected result: 先保留八秒 steady 资源窗口，再每十五秒触发一次 TLS；只有收到
  G0 启动后的首个音频帧才开始 600 秒计时，120 秒未启动则明确失败。
- Result: Partial。Python HIL tests 15/15 和 ProductAudio executable tests 通过，
  Companion release app 已重建。首次尝试中 BLE 保持连接但约两分钟收到 0 帧；
  检查同时发现旧 probe 错误地在首帧前计时，已修复并提交。用户选择稍后执行实体
  G0 门禁，因此本轮未生成可行性通过证据。常驻 LaunchAgent 已恢复，Cardputer
  Codex 为 BLE connected，状态 API 返回 HTTP 200，宠物同步恢复。
- Next step: 用户设备在手边时重新运行十分钟 HIL，短按一次 G0，并在录音期间输入
  普通按键；通过全部 loss/gap/reconnect/HID/heap/TLS/stack gate 后再进入 Task 10。

## 2026-07-26 17:52 HKT

- Current work: 按用户要求将实体门禁移到最后，完成 Task 10 跨进程固定容量原子音频环。
- Expected result: C17 SPSC 环使用 acquire/release 原子计数，容量固定 16,384 帧；
  溢出丢弃最新输入，欠载精确补零；支持 reset、producer heartbeat、匿名 FD mmap，
  并作为 Swift `AudioSampleSink` 接入。
- Result: Achieved。C ABI 测试覆盖容量、回绕、partial read/write、overflow、
  underflow、reset、heartbeat 和 100,000 帧并发 producer/consumer，ASan/UBSan
  通过；Swift debug/release executable tests 均通过匿名已 unlink 文件映射、480 帧
  零拷贝边界读写和 header 校验。release 测试还定位并修正了 assert 中副作用会被优化
  删除的问题。
- Next step: 提交 Task 10；Task 11 实现 48 kHz mono input-only Core Audio HAL
  device、纯 C render adapter、确定性 bundle 构建与签名验证。

## 2026-07-26 17:57 HKT

- Current work: 完成 Task 11 input-only Core Audio AudioServerPlugIn HAL driver。
- Expected result: 系统仅发布一个 48 kHz mono float 输入流，无输出流；稳定 object
  IDs；多 client start/stop 计数；无 producer 时精确静音；render 路径不含锁、
  allocation、日志、文件系统或 HAL client API；bundle 可确定性构建和验证。
- Result: Achieved。纯 C device tests 在 ASan/UBSan 下通过 stream/cardinality、
  client count、ring data 和 underflow silence；bundle manifest tests 3/3 通过；
  `CardputerAudioDriverFactory` 为导出符号，ad-hoc codesign strict/deep 验证通过。
  CoreAudio/CoreFoundation/Security 与 libSystem 依赖已由 `otool` 验证。计划中
  `XPC.framework` 假设经 SDK 证实不存在，已更正为由 libSystem 提供 XPC C API。
- Next step: 提交 Task 11；Task 12 实现 audit-token policy、匿名 shm、单 producer
  两秒 lease、XPC FD transfer 和 Swift 500 ms heartbeat client。

## 2026-07-26 18:03 HKT

- Current work: 完成 Task 12 authenticated XPC producer lease、共享 FD transfer
  与 Swift `AudioDriverConnection`。
- Expected result: development build 仅接受准确 Companion bundle ID、ad-hoc
  签名和当前 console UID；release policy 还要求 Team ID；协议版本不符不返回 FD；
  两秒单 producer lease、500 ms heartbeat、stale replacement、release/reset/silence
  全部成立，首个 heartbeat 前不发布 sink-ready。
- Result: Achieved。纯 C IPC policy/lease tests 在 ASan/UBSan 下通过错误 bundle、
  UID、Team ID、protocol、busy/stale lease、heartbeat 与 release 清环；Swift
  debug/release tests 通过 `hello→claim→heartbeat→ready`、共享映射写入、reset、
  release 和 heartbeat failure 不 ready。driver bundle 重建、签名和 tests 3/3
  通过，`nm` 验证实际引用 public XPC 与 Security signing APIs。当前 SDK 不公开
  audit-token getter，因此 runtime 采用 XPC 建连时冻结的 EUID/PID 加 Security
  signing information；development 再绑定构建时 console UID。
- Next step: 提交 Task 12；Task 13 实现 driver install/uninstall/doctor、Companion
  bundle resource，以及固件 microphone UI/Settings/Web 只读状态集成。

## 2026-07-26 18:14 HKT

- Current work: 完成 Task 13 HAL 安装/卸载、`doctor audio`、Companion 运行时桥接，
  以及固件麦克风 UI、Settings 与 Web 只读状态集成。
- Expected result: 仅管理员变更 `/Library/Audio/Plug-Ins/HAL`；应用只安装自身
  Resources 中的签名 driver；普通运行不提权；所有页面显示稳定 MIC 状态，PET
  录音时有红色标识，DEVICE 增加 MIC 行；Input Mode 与 SAFE 迁入 Settings；
  Web 只读显示状态/采样率/丢帧率/错误且不存在启停路由。
- Result: Achieved。新增 staged/validated/rollback-safe helper、三条 CLI 命令、
  HAL 枚举/XPC/共享静音环/GATT subscription 诊断和正常 LaunchAgent 音频桥；
  非 root 变更以退出码 77 明确要求 sudo。固件 host 目标 3/3、Web/installer/
  packaging Python 37/37、ProductConfiguration/ProductAudio/ProductGATT、
  C ring、HAL ASan/UBSan、ad-hoc codesign 与 ESP-IDF 5.5.4 target build
  全部通过，应用分区仍保留 49%。
- Next step: 提交 Task 13；Task 14 增加恢复/版本门禁，统一升级到 1.1.0，
  扩展完整 release gate，安装当前 Mac 的开发 HAL/Companion 并 app-only
  刷写；按用户要求把实体 G0/30 分钟 HIL 留到最后。

## 2026-07-26 18:20 HKT

- Current work: 完成 Task 14 的 1.1.0 版本统一、HAL producer 故障恢复、发布文档
  和完整自动化 release gate；部署前复核冷启动恢复路径。
- Expected result: driver 失联时 500 ms 内撤销 sink-ready；恢复后只回到 READY，
  不自动录音；若 Companion 启动时 HAL 尚不可用，后续 HAL 恢复仍能把现有
  Unicode-only GATT 会话升级为 Audio 会话；固件、Companion、driver 和文档版本
  全部为 1.1.0。
- Result: Achieved for development and automated release gate。首轮自动门禁通过 Python 167/167 加音频显式 16/16、host
  normal/sanitizer 35/35、ESP-IDF 5.5.4 clean build、ProductAudio、
  ProductGATT、ProductConfiguration、C ring、HAL/IPC、签名和 private packaging。
  部署前代码复核发现主循环以首次 XPC 结果固化 `audioEnabled`，会令 HAL 后到达时
  永久不重试；新增回归测试先得到缺失策略 RED，再实现线程安全恢复策略并使
  ProductAudio、ProductGATT 和 release build GREEN。修复后完整自动门禁再次全部
  通过；应用镜像仍保留 49%，DIRAM headroom 145,089 bytes。最终 app image
  SHA-256 为 `8a1ac0dc6659af1826bbe1b6d11b0120c9bf349f9250dc66fee45c909bcf96c6`，
  private full image 为
  `9410d5b5410891805dac77a3dfe267be0e2aac7d81649c389e5f4e9a0438165b`。
- Next step: 自动门禁再次通过后提交 Task 14，安装开发 HAL、重启 Core Audio、
  app-only 刷写并恢复 LaunchAgent；最后才执行实体 G0/HID/30 分钟 HIL。

## 2026-07-26 19:09 HKT

- Current work: 修正 HAL 进程内 Mach listener 架构、完成当前 Mac 安装，并排查
  `doctor audio` 无法发现新增 BLE 音频特征的问题。
- Expected result: launchd 系统服务持有匿名共享环；Companion producer 与
  `coreaudiod` consumer 均通过签名策略连接；旧配对升级后主动刷新 GATT cache；
  常驻 Agent 恢复时只回到 READY，不自动录音。
- Result: Achieved。driver 1.1.0、root-owned helper 与 LaunchDaemon 已安装且签名
  校验通过，Core Audio 枚举 48 kHz mono 输入。原 HAL 内 listener 违反 launchd
  Mach service 注册模型，已改为独立桥。第二个根因为 Audio v1 在既有 service
  内增加 0005/0006/0007 时未发布 Service Changed；新增独立 `gatt_db_v` 迁移，
  并在 macOS 恢复 indication 订阅后发送再落盘。真机串口确认 schema 2，随后
  `doctor audio` 的 driver、Core Audio、XPC、静音环、BLE characteristics、
  subscriptions、protocol v1 和 24 kHz 全部通过。停止 Agent 后麦克风变为
  UNAVAILABLE，重新加载第一次轮询即恢复 READY，未进入 RECORDING。
- Next step: 更新发布产物并重新跑完整自动门禁，提交架构与 GATT cache 修复；
  所有非实体工作完成后才开始 G0/HID/30 分钟最终 HIL。

## 2026-07-26 19:13 HKT

- Current work: 完成最终 clean release gate、精确 app-only 重刷和部署后音频诊断。
- Expected result: 所有自动测试、sanitizer、ESP-IDF clean build、Swift、HAL、
  helper、签名和打包通过；系统安装内容与 `dist` 一致；刷入镜像 digest 匹配；
  重启后 BLE/Wi-Fi/Agent/MIC READY。
- Result: Achieved。Python 168/168、音频专项 17/17、host normal 35/35、
  ASan/UBSan 35/35、ESP-IDF 5.5.4 clean build、ProductAudio/ProductGATT/
  ProductConfiguration、C ring/device/IPC、driver/helper/app 签名及 private
  packaging 全部通过；应用分区保留 49%，DIRAM headroom 145,089 bytes。
  driver/helper/LaunchDaemon 与最终发布 payload 字节一致。最终 app image
  SHA-256 `858cbbb5fcf39fe6ca6f30940a31b301d6c1e55fbb11d7c0a768b570d7036a37`
  已写入 `0x20000` 且独立 `verify_flash` digest matched；private full image
  SHA-256 `7d724a14e53d128e459b700c7c7f725ae4f0d626074a22ea949443adc309fa71`。
  冷启动第三次两秒轮询返回 1.1.0、BLE/Wi-Fi/Agent OK、MIC READY；隔离
  LaunchAgent 后最终 `doctor audio` 八项再次全部通过，恢复 Agent 第一次轮询
  回到 READY。
- Next step: 提交最终修复；最后执行实体 G0、并发 HID 与 30 分钟 HIL。

## 2026-07-26 23:37 HKT

- Current work: 用 USB 串口自动启动麦克风后，对短时音频 HIL 的 BLE 丢帧与重连
  进行逐层定位，并修复 bonded security 事件乱序、CoreBluetooth 回调调度、任务栈
  余量和 HIL 计数基线。
- Expected result: 30 秒 TLS 并发压力下无 BLE 重连，回调间隔不超过 150 ms，
  稳态 heap/largest、TLS heap 和所有任务 stack 门槛保持不变；剩余丢帧定位到
  单一可复现路径。
- Result: Partial。最新真机结果无重连、最大回调间隔 75 ms、稳态 heap 65,780、
  largest 45,056、TLS heap 57,644，任务栈全部通过。剩余 12 个 transport drop
  均对应 TLS 突发时 `ble_hs_mbuf_from_flat` 的短时空池，采集端仅 3 个 overrun。
  已先加入 host RED/GREEN，约束发送 credit 持有期间以 1 ms 间隔最多重试 12 次，
  总等待低于 19 ms 音频帧周期。
- Next step: 构建并 app-only 刷写重试修复，运行短时 HIL 验证丢帧；通过后增加
  串口触发的真实 HID 队列压力，最后执行完整 release gate 和 30 分钟 HIL。

## 2026-07-27 00:57 HKT

- Current work: 修复 16 kHz 采集 DMA cadence 和 BLE 长时通知吞吐，保持 Audio v1
  单通知完整帧与 150 ms 硬门槛。
- Expected result: I2S DMA EOF 与每次读取样本数一致；16 kHz 长时通知率低于 Mac
  BLE 持续吞吐上限；无 mbuf 耗尽断言、协议模式错误或重启。
- Result: Achieved。采集 backend 改为按活动采样率使用真实 frame samples；
  16 kHz 改为 448 samples / 28 ms / 228-byte ADPCM payload / 236-byte packet，
  从约 53 notifications/s 降至约 36 notifications/s，仍保持单 ATT notification。
  60 秒真机得到 2,184 received、0 sequence gap、0 transport drop、0 reconnect；
  旧 19 ms 方案曾在约 2–3 分钟线性耗尽通知资源并触发 NimBLE mbuf assert。
- Next step: 消除首次 TLS 握手造成的 177 ms 单次采集间隔，然后执行 10 分钟门禁。

## 2026-07-27 01:23 HKT

- Current work: 提升音频采集任务优先级并修复 USB HIL 的重复运行清理和 TLS
  资源采样分类。
- Expected result: TLS 握手不能抢占 I2S 采集；每轮 HIL 显式停止残留 HID burst；
  TLS 低水位不被误标为 steady；不降低任何发布门槛。
- Result: Achieved for code and short HIL。音频任务从 priority 2 提升到 6，
  高于 ESP-IDF HTTPS 默认 priority 5；60 秒结果为 2,197 received、
  `max_gap_ms=106`、0 source overrun、0 transport drop、0 sequence gap、
  0 reconnect，heap/stack 门槛全过。600 秒原始数据为 22,161/22,161 received、
  0 sequence gap、`max_gap_ms=105`、0 reconnect、1000/1000 HID、0 HID failure。
  该轮最终报告发现采样器把紧邻 `performing session handshake` 的低水位误标
  steady，且结束后合法的 `HIL MIC STOP NOOP` 未列入 ACK；两项均已先加
  RED/GREEN 回归，再用 1.25 秒 TLS settle 窗口和完整 cleanup ACK 修正。
- Next step: 运行完整自动 release gate，重建/部署最终 payload；最后使用修正后的
  runner 执行 30 分钟真机门禁并恢复正常 LaunchAgent。

## 2026-07-27 02:09 HKT

- Current work: 修复首次 30 分钟 HIL 暴露的探针计时漂移、BLE watchdog 栈余量
  与 TLS 握手前资源采样分类问题。
- Expected result: 1800 秒探针按单调绝对时钟准时结束；watchdog 高水位余量不低于
  1024 bytes；握手开始前两秒内的实际设备资源样本归入 TLS 窗口，且所有硬门槛
  保持不变。
- Result: Achieved for code and focused tests。首次长跑中音频计数约 65,359、
  0 gap/drop/reconnect，但逐秒相对 sleep 累积约 30 秒漂移而被 runner 超时；
  watchdog 实测 1004 bytes。现改为绝对 deadline，栈由 1792 增至 1920 bytes，
  HIL 依据 ESP-IDF 实际 `performing session handshake` 事件回溯标记相邻采样。
  ProductAudio 与 Python firmware/HIL 定向测试均通过。
- Next step: 执行完整 clean release gate、app-only 刷写并重跑最终 30 分钟 HIL。

## 2026-07-27 02:54 HKT

- Current work: 完成最终 release gate、app-only 部署、30 分钟实体 HIL，并恢复
  常驻 Mac Agent。
- Expected result: 所有自动门禁和硬件门槛通过；最终镜像与设备同源；HIL 后
  LaunchAgent 回到 BLE/Wi-Fi/Agent OK、MIC READY。
- Result: Achieved。完整门禁通过 Python 190/190、音频专项 17/17、普通和
  ASan/UBSan host 各 36/36、ESP-IDF clean build、Swift ProductAudio/
  ProductGATT/ProductConfiguration、C ring/device/IPC、签名和 private packaging。
  app-only 镜像写入 `0x20000` 且独立 `verify_flash` digest matched。1800 秒报告
  `build/hil/cardputer-audio-final-1800s-16k.json` 为 captured 64,293、
  received 64,287、0 source overrun/drop/gap/reconnect/allocation failure、
  `max_gap_ms=107`；HID 1000/1000、0 failure、p95 100 us；steady heap
  70,208、largest 43,008、TLS heap 56,880，全部 task stack 通过。
  新 ad-hoc CDHash 使旧 macOS Bluetooth TCC 授权失配；重置并由用户允许新版
  App 后，LaunchAgent PID 66787 连续五次返回 version 1.1.1、BLE/Wi-Fi/Agent
  OK、MIC READY。app SHA-256
  `662c82033442f0a07017894101930a8a583a2d1ec86023eabc9fe96d4a8f8bce`，
  private full SHA-256
  `463cbff6425ff72cd556b963be2be37c651edf7c13da0da5c5b7a699eaa99467`。
- Next step: 提交最终代码、验证记录和制品哈希；仓库无 remote，保留本地分支
  供后续合并。
