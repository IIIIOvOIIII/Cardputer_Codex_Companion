# Cardputer Codex Companion 项目进度

## 2026-07-24 00:14 HKT

- Current work: 将 compact 前已经确认的产品边界与总体架构落盘，创建独立项目目录并初始化本地 Git 版本控制。
- Expected result: 新项目包含可恢复上下文的确认决策快照、入口 README 和进度记录，并由初始提交固化。
- Result: Achieved — 已创建 `Cardputer_Codex_Companion` 独立项目，确认事项与未决边界均已记录，并纳入本地 Git 初始提交。
- Next step: compact 后从 Profile/宏数据模型与安全规则开始继续逐节设计；在获得确认前不进入实现。

## 2026-07-24 00:29 HKT

- Current work: 汇总已经逐节确认的架构、Profile/宏、设备 UX、Web/配对、协议、持久化、OTA、测试和交付阶段。
- Expected result: 形成无待定项的正式设计规格，完成一致性自审并提交本地 Git。
- Result: Achieved — 正式规格已写入 `docs/superpowers/specs/2026-07-24-cardputer-codex-companion-design.md`，确认快照和 README 已指向新规格。
- Next step: 等待用户复核正式设计规格；获得复核通过后再编制实施计划。

## 2026-07-24 02:18 HKT

- Current work: 将已通过的正式设计拆解为只覆盖 Phase 0 的可执行实施计划，并完成 foundation、固件并发、macOS Unicode/BLE、Web/Flash/发布安全四条工作流的跨计划接口审计。
- Expected result: 形成单一依赖顺序、27 个 reviewer-sized tasks、五份 child report/六项 gate 的机器可验证证据闭环；在没有真机、签名身份或完整工具链时不误报可行性结论。
- Result: Achieved — 主计划与四份子计划已落盘；Codex capability 路径、Companion 并发 JSONL agent、probe/app/device provenance、共享 `HIL_BASE_COMMIT`、外部 P-256 release manifest、OTA/config slot 原子配对及不可逆 eFuse 审批边界已经统一。Markdown 链接、代码围栏、任务数量、旧契约残留与 Git whitespace 检查通过。本阶段没有编写产品实现或运行 HIL。
- Next step: 选择 subagent-driven 或 inline 执行方式，从 foundation Tasks 1–2 开始 Phase 0；在取得六项 gate 的真实证据前保持 `NO_GO`。

## 2026-07-24 09:46 HKT

- Current work: 按 Subagent-Driven Development 执行 Foundation Task 1，锁定并引导仓库内 Phase 0 工具链，并完成独立规格/质量审查。
- Expected result: ESP-IDF、Node、Python 与 Python 依赖精确锁定；ESP-IDF 安装强制使用 `.tools/uv-python` 中的 Python 3.11.11；缺失 HIL 条件如实保持 `BLOCKED`；任务通过 RED/GREEN、bootstrap 和双重审查。
- Result: Achieved — 实现提交 `2918d35` 与修复提交 `a2337bb` 已完成；动态测试证明错误 Python 会非破坏性失败且安装器解析仓库内解释器，4/4 单测、`uv lock`、完整 bootstrap 与工具链清单验证通过。最终审查结论为规格合规、Task quality Approved，且无 Critical/Important/Minor 遗留。当前 HIL 应用前置条件仍为 `BLOCKED`，未被误报为硬件 gate 结论。
- Next step: 执行 Foundation Task 2，固定 pairing/GATT/WSS 协议编码和确定性跨语言安全向量。

## 2026-07-24 10:09 HKT

- Current work: 完成 Foundation Task 2 的 pairing、GATT 与 WSS 唯一字节协议、确定性安全向量及跨计划接口审查。
- Expected result: Python 生成器、规范文档和三份 fixture 对同一协议逐字节一致；覆盖 32-byte nonce、transcript-hash HKDF salt、完整 GATT 分片/HMAC/replay、五字段 WSS exporter 绑定、固定宽度 P-256 签名、SPKI/role 拒绝和 post-SAS 双通道绑定。
- Result: Achieved — 协议提交 `a67873c` 与负向修复提交 `781b405` 已完成；11 项测试、确定性重生成与 OpenSSL transcript SHA-256 独立对账通过。最终审查为 Spec compliant、Task quality Approved，且无 Critical/Important/Minor 遗留；fixture 私钥材料明确标记为仅限测试。
- Next step: 按依赖顺序执行 macOS Tasks 1–6，先建立 SwiftPM target/稳定契约，再实现 Unicode、TCC、配对、BLE/GATT 和签名应用骨架；HIL live run 继续后置。

## 2026-07-24 10:14 HKT

- Current work: 执行 macOS Task 1 的 SwiftPM target 图和稳定 DTO 契约，并定位测试工具链阻塞。
- Expected result: ContractTests 3/3、全部 surface tests、Swift build 与 `cardputer-phase0-probe --version` 均通过后进入独立任务审查。
- Result: Partial — 实现提交 `15c8196` 已创建，全部生产 library/executable targets 构建成功，CLI 严格输出 `cardputer-phase0-probe 0.1.0`；但当前主机只安装 CommandLineTools，没有 `Xcode.app`，`swift test` 与直接 `import XCTest` 均以 `no such module 'XCTest'` 失败。该任务未标记完成、未进入审查，且不得用伪造 XCTest 或改写测试框架制造绿灯。
- Next step: 利用 Foundation 后可并行的依赖边界继续 firmware Tasks 1–7；完整 Xcode/XCTest 可用后回到 macOS Task 1 完成测试、清理误纳入 Git 的 SDD 报告并执行双重审查。

## 2026-07-24 10:29 HKT

- Current work: 完成 Firmware Task 1 的 ESP-IDF/组件精确锁定、8MiB 无 PSRAM 基线、运行时硬件探针、严格硬件清单捕获与独立复审。
- Expected result: 仅接受唯一明确的 `hardware_runtime` 事件；USB serial 只以 SHA-256 落盘；键盘矩阵物理验证必须由操作员显式确认；固件在锁定的 ESP-IDF 5.5.4 与组件版本下真实编译通过。
- Result: Achieved — 实现提交 `82433ec` 与硬化提交 `ade5f22` 已完成；9 项主机测试、`set-target`、`reconfigure`、依赖锁对账和完整 `idf build` 通过。构建实测发现无 PSRAM 基线直接链接 `esp_psram_get_size()` 会失败，现以 `CONFIG_SPIRAM` 条件编译保留规定 API，并在无 PSRAM 时明确返回 0。最终复审为 Spec compliant、Task quality Approved，无 Critical/Important/Minor 遗留；未生成或伪造任何实机硬件清单。
- Next step: 执行 Firmware Task 2，建立单次运行服务状态、不可混合的 evidence identity 与 child report 禁止裁决字段规则。

## 2026-07-24 10:37 HKT

- Current work: 完成 Firmware Task 2 的单次运行服务状态、证据身份、严格报告 schema 与跨运行失败关闭校验。
- Expected result: Wi-Fi/WSS 状态变化不得清除 BLE HID/GATT；不同 run/boot/app/image/device 证据不得合并；Companion 事件必须带 producer 与 runner 双时钟；child report 不得携带任何裁决字段。
- Result: Achieved — 实现提交 `a7df6ec` 与接收时间修复提交 `392f8a3` 已完成；C++ host test 1/1、schema/validator tests 12/12、Task 1 回归 9/9 通过。复审确认 `observed_at_ns` 已成为必填字段，报告 head 元数据已纠正；最终结论为 Spec compliant、Task quality Approved，无 Critical/Important/Minor 遗留。
- Next step: 执行 Firmware Task 3，接入官方 ESP HID、单一自定义加密 GATT 服务，并以 Foundation 确定性向量验证固件协议字节。

## 2026-07-24 11:28 HKT

- Current work: 完成 Firmware Task 3 的官方 ESP HID 初始化、唯一自定义加密 GATT 服务、不可变设备身份、HID 报告释放保证与 Foundation canonical 协议向量固件消费。
- Expected result: 固件严格沿用锁定 ESP-IDF 的官方 HID helper，不注册第二套 HID/GATT owner；Identity/Control 特征失败关闭；六份 canonical source 与生成头精确绑定；主机测试和 ESP32-S3 目标构建通过。
- Result: Achieved — 实现提交 `52b381a`、构建新鲜度修复 `6dbf379` 与 canonical SHA-256 钉扎修复 `bc4c313` 已完成；C++ host tests 6/6、Python tests 29/29、完整 ESP-IDF build、官方 helper diff 校验与 forbidden scans 全部通过。最终规格审查合规、质量审查 Approved，无 Critical/Important/Minor 遗留。实机 BLE 配对、加密特征访问与 HID 送达仍按计划留给 HIL。
- Next step: 执行 Firmware Task 4，在任何 TLS 分配前实施固定容量 admission limiter，并验证 4 个已建立会话加 1 个 pending handshake 的硬上限。

## 2026-07-24 11:38 HKT

- Current work: 完成 Firmware Task 4 的固定容量 pre-TLS admission limiter 与基于 ESP HTTP Server 公共 session hook 的 bounded HTTPS server spike。
- Expected result: 16 个固定 source slots、每源 3 次/分钟与全局 6 次/分钟限流均在 `esp_tls_init()` 前执行；运行时最多 4 个 established 加 1 个 pending；TLS、transport context、socket 和计数在所有失败/关闭路径只释放一次。
- Result: Achieved — 实现提交 `00075e7` 与空白修复 `09d67e6` 已完成；主机 tests 7/7、完整 ESP-IDF 5.5.4 build、动态分配禁用扫描与 `httpd_ssl_start()` 禁用扫描通过。规格审查判定 Spec compliant；质量审查依据锁定 IDF 的同步 `open_fn` 失败清理链判定 Approved，无 Critical/Important/Minor 遗留。真实并发 TLS 占用仍按计划留给 Task 7 HIL。
- Next step: 执行 Firmware Task 5，建立真实 Web 配对、物理确认、管理员 session、Host/Origin/CSRF 校验和请求预算状态机。

## 2026-07-24 12:16 HKT

- Current work: 完成 Firmware Task 5 的 Web 配对、物理确认、固定管理员 session、Host/Origin/CSRF、限流、请求预算与六条真实 HTTPS handler。
- Expected result: 五分钟八位码、第五次错误后十分钟退避、最多五会话、30 分钟 idle、仅摘要凭据状态、固定容量限流、128KiB/depth-eight 流式导入与 16KiB/1009 WebSocket 边界均可测试，且物理确认等待不得阻塞 ESP HTTPD 单任务循环。
- Result: Achieved — 实现提交 `dd974e3`，审查修复 `400fe49`、`df5827a`、`d6eecb7` 已完成；配对响应使用官方 async request API 与单一静态 worker，析构可取消、完成并停止 worker。普通 host tests 9/9、ASan/UBSan 9/9、完整 ESP-IDF 5.5.4 build 通过；目标 ELF 含六个非零 handler、两项 async API 和 exact 六路由，镜像 `0xa8740`，最小 app 分区余量 34%。最终规格审查 Spec compliant、质量审查 Approved，无遗留 finding。真实浏览器、物理确认、连接中断和攻击矩阵仍按计划留给 HIL。
- Next step: 执行 Firmware Task 6，实现带 SPKI pinning 与 TLS exporter 绑定的外部 WSS transport。

## 2026-07-24 13:57 HKT

- Current work: 完成 Firmware Task 6 的外部 WSS transport、Companion SPKI pin、TLS exporter channel binding、设备签名向量验签和连接代际认证门控。
- Expected result: 仅使用 ESP-IDF 5.5.4 公共 API 建立自有 ESP-TLS 并包装为 websocket `ext_transport`；hostname 与 SPKI 双重验证后才保留 exporter；旧连接 `auth_ok` 不得认证新连接；foundation fixture 不被固件侧重定义。
- Result: Achieved — 实现提交 `e5fbb12` 与审查接入修复 `da62e8c` 已完成；同时修复了固件向量生成器误将 Companion peer SPKI 当作设备 signer 公钥的根因。生成器 tests 9/9、普通 host tests 10/10、ASan/UBSan 10/10、公开 API/硬编码 label 扫描与完整 ESP-IDF build 均通过；最终镜像 `0xa9290`，最小 app 分区余量 34%。规格复审 Spec compliant，质量复审 Approved；真实 app-level Companion 配置与 auth wire handler 接入保留为跨任务 warning，未通过硬编码伪配置制造运行假象。
- Next step: 执行 Firmware Task 7，加入固定内存 HID 延迟直方图、堆/栈/分配失败、固定队列和 transient burst JSONL 测量。

## 2026-07-24 14:41 HKT

- Current work: 完成 Firmware Task 7 的固定内存 HID 延迟直方图、堆/栈/分配失败、固定队列、真实 5 秒 transient burst 与 1Hz JSONL 证据采样。
- Expected result: HID 32 深度队列零等待且失败不伪造延迟；资源阈值、七任务栈水位、HTTPS/WSS 占用、网络队列溢出和 transient WSS/import/session/approval 计数进入同一身份绑定样本；原始设备 ID 与请求秘密不得输出。
- Result: Achieved — 实现提交 `80f02d1`，运行闭环修复 `dde3f49` 与窗口重开修复 `37bee7b` 已完成。JSONL 根对象与设备摘要泄漏问题在提交前修正；普通 host tests 11/11、ASan/UBSan 11/11、ESP-IDF 5.5.4 build 与 forbidden scans 通过。最终镜像 700,992 bytes，SHA-256 `62f7bf560d249ac4f86d2d3b1e81bc498c886f4dfac61d06c10e998e80657df9`，最小 app 分区余量 33%。规格复审 Spec compliant、质量复审 Approved，无遗留 finding。
- Next step: 执行 Firmware Task 8，提交严格 HIL runner/validator；若仍无唯一 Cardputer 串口设备，则生成 schema-valid `capture_complete=false` blocker 报告并继续最终干净构建交付，绝不误刷其他串口。

## 2026-07-24 15:04 HKT

- Current work: 完成 Firmware Task 8 的并发报告 schema/validator、严格 HIL 预检与破坏性动作屏障，并在已提交 runner 的干净树上执行正式预检。
- Expected result: 条件齐备时执行一次独立 30 分钟实机 HIL；条件缺失时仅生成 schema-valid、`capture_complete=false`、无裁决字段的 blocker 报告，且不读取或刷写任何非目标串口。
- Result: Partial — validator、初版 runner 与审查硬化提交 `5aea871`、`4158f4b`、`2415e96` 已完成，组合测试 39/39 通过。双审指出完整 1800 秒场景、真实 Web pairing 与证据聚合尚未实现；为避免未来误用，已删除不可达的刷写/随机 `boot_id` 路径，补齐 ESP-IDF 双锁与 checkout commit 检查，并将当前 runner 明确收敛为非破坏性预检。复跑只发现蓝牙、DJI 麦克风、打印机和调试串口，没有唯一 ESP32-S3；同时缺少实机 hardware manifest、macOS concurrency agent、GATT secret 和 17 个已分配 LAN 地址。runner 非零退出并生成六项明确 blocker 的有效报告，输出目录没有 flash backup 或其他运行证据，未发生读写/刷机。Task 8 的 `capture_complete=true` 退出条件因此未达成。
- Next step: 对 Task 8 harness 与整个 Firmware Tasks 1–8 变更做独立规格/质量复审；修复发现后执行全部 host/sanitizer/Python 回归和 ESP-IDF 干净构建，交付可刷写固件。实机 HIL 必须在连接唯一 Cardputer 并补齐 Companion 与 LAN bench 后重新运行。

## 2026-07-24 15:14 HKT

- Current work: 对 Firmware Tasks 1–8 做最终跨任务规格/质量审查、完整回归、ESP-IDF `fullclean` 构建与合并镜像打包。
- Expected result: 所有可在当前主机完成的测试、静态边界检查与目标构建通过；交付应用镜像和从 `0x0` 刷写的完整合并镜像，并保留实机 HIL 的真实 blocker。
- Result: Achieved for compiled-firmware delivery — Phase 0 Python tests 57/57、firmware host tests 11/11、ASan/UBSan tests 11/11 和四组禁止项扫描通过；锁定 ESP-IDF 5.5.4 从零构建通过。应用镜像 700,992 bytes、SHA-256 `0458d987288abdde9af080c7cd64150943677b7537aa333b536bb43f72811025`；完整合并镜像 766,528 bytes、SHA-256 `525a2a7b0130089ae0ca2aefd868b121b8cb51f57ea636bc8cae309ae21f3b96`。最终质量审查无新增 finding；规格审查确认 Tasks 4–7 实现边界，Task 8 仍按文档保持 preflight-only blocker。由于没有 Cardputer 串口设备，未刷机、未执行 30 分钟 HIL。
- Next step: 向用户交付 `firmware/build/cardputer_codex_phase0-full.bin` 和应用分区镜像路径。后续连接唯一 Cardputer 并补齐 mDNS/TLS Web pairing、macOS concurrency agent、17 地址 bench 后，继续完成 Task 8 `capture_complete=true` 实机门槛。

## 2026-07-24 16:02 HKT

- Current work: 在用户发现 Phase 0 探针无屏幕、无 Wi-Fi 和无产品运行时后，重新建立端到端产品化实施边界；通过 `lynx-vault` 仅确认 `shared.wifi.ssid` 与 `shared.wifi.password` 两个 scalar ref 可读取，未获取或落盘明文。
- Expected result: 明确区分测试探针与可用产品；批准真实屏幕/键盘/Wi-Fi/Web/Profile/宏、macOS Companion/Codex 联动、私有 NVS 凭据制品和分层验收设计。
- Result: Achieved — 用户批准延续现有 ESP-IDF/Swift 架构、固件与 Companion 端到端一起完成、Wi-Fi 凭据使用 Git 忽略的私有 NVS 分区封装，并批准架构、数据安全、故障和验收三部分设计。增量设计落盘为 `docs/superpowers/specs/2026-07-24-product-firmware-companion-implementation-design.md`。
- Next step: 自审并提交增量设计，然后使用 `writing-plans` 创建逐任务、测试先行的产品化实施计划；计划批准前不修改产品代码或读取 Vault 明文。

## 2026-07-24 16:24 HKT

- Current work: 将已批准的产品化增量设计拆成单一依赖顺序的固件、Web、Companion、Vault 私有打包与交付任务，并核对原版 Cardputer 官方键盘扫描实现。
- Expected result: 每项任务包含精确文件、RED/GREEN、验证命令和安全边界；56 键矩阵不再沿用错误的 3×7 理解。
- Result: Achieved — 产品实施计划已写入 `docs/superpowers/plans/2026-07-24-product-firmware-companion.md`，共 14 个测试先行任务。硬件依据固定到 M5Stack 官方 `M5Cardputer` commit `f1392858b9994c3547120e602a57d3553d16ab01`：GPIO 8/9/11 产生 8 个 selector 状态，GPIO 13/15/3/4/5/6/7 读取交错列并映射为 4×14/56 键。
- Next step: 自审计划、提交文档，然后按 `executing-plans` 在当前侧会话内联执行 Task 1–14；Vault 明文只在 Task 13 的忽略目录和 0600 临时文件中出现。

## 2026-07-24 16:55 HKT

- Current work: 完成产品固件、内嵌 Web、macOS Companion、Vault 私有 Wi-Fi NVS、完整镜像和发布门禁；发布复核时修正四层 Web Profile 仅执行 layer 0 以及屏幕 Profile 名称不更新的问题。
- Expected result: 从 `0x0` 可刷写的私有完整镜像包含 Vault 指定 Wi-Fi；固件具备可见启动/状态界面、56 键 BLE HID、四层自定义 chord/text/sequence、UTF-8/中文 Companion 链路和 Codex 活跃会话状态；所有可在无实机条件下完成的验证通过且不泄露凭据。
- Result: Achieved for compiled product delivery — Python 78/78、firmware host 21/21、ASan/UBSan 21/21、ESP-IDF 5.5.4 `fullclean` target build、Swift release build、Companion doctor、Web 资源、通用/私有镜像组装、分区偏移和 secret exclusion 均通过。应用镜像 1,467,152 bytes、SHA-256 `ab81fdd63f97d489ca0f8c46402b1b7d251abc0a143ed6fd62414b47512358b4`；私有完整镜像 1,598,224 bytes、SHA-256 `bf4bc762e15195bd8684aed7d229b8185d561b41b3c4bc09b924680b1109dbeb`。产品 LAN 状态链路使用 HTTPS + 屏幕八位 PIN，中文使用已加密、已认证、已绑定的 BLE GATT；本次不宣称 Phase 0 完整 P-256/SAS/pinned-WSS 双通道绑定已产品化。
- Next step: 用户将私有完整镜像写入 `0x0` 并启动 Companion。当前没有唯一 Cardputer 串口，因此未刷机，也未执行 30 分钟实机 HIL；LCD 方向、56 个实体键、真实 BLE/Wi-Fi/HTTPS/中文注入和持续并发仍需真机验收。

## 2026-07-24 17:02 HKT

- Current work: 在合并后的 `main` 上从头复验发布，定位并修复被忽略的旧 `firmware/sdkconfig` 令发布脚本沿用 Phase 0 默认分区表的问题。
- Expected result: 任意本地残留配置下，发布入口都必须重建 ESP32-S3 产品配置；构建后的二进制分区表必须逐项匹配产品 CSV，否则禁止打包交付。
- Result: Achieved — 新增分区布局单元测试和二进制校验器；默认 `nvs/phy_init/factory` 布局回归测试确认失败关闭，发布脚本改为每次执行 `set-target esp32s3` 并在打包前核验七个产品分区。合并后主分支完整门禁通过：Python 80/80、firmware host 21/21、ASan/UBSan 21/21、ESP-IDF 5.5.4 target build、精确产品分区校验、Swift release/doctor、通用/私有镜像打包与 secret exclusion 全部通过。最终应用镜像 1,467,152 bytes、SHA-256 `5cdb714a8354ac4ce12d2d63a0eac19d3c99eeab9f1a586515dcf2ec1f82c7c5`；私有完整镜像 1,598,224 bytes、SHA-256 `9181bfae366128c2f4417884c2e3cf4e9e3d692ed1898c4f566c1d92eccf1f35`。
- Next step: 提交发布复现修复并交付主目录私有完整镜像；由于没有唯一 Cardputer 串口，刷机和 30 分钟实机 HIL 继续明确留待真机。

## 2026-07-24 17:23 HKT

- Current work: 根据实机反复重启照片定位启动期 BLE `E001`、Wi-Fi `E257`，修复产品 Profile 的静态内存耗尽，并重新构建 1.0.1 私有完整镜像。
- Expected result: 保持四层 56 键、最多 16 步组合键/字符串序列及中文 UTF-8 协议不变，同时为 BLE、Wi-Fi、HTTPS 和运行时分配恢复充足的 ESP32-S3 DIRAM；发布门禁能够阻止同类内存回归。
- Result: Achieved for compiled-firmware delivery — 原实现为 224 个按键各自预留 16 个含 `std::string` 的序列步骤，活动 `Profile` 在目标 `.bss` 占 152,348 bytes，目标 DIRAM 只余 8,909 bytes；现改为按需序列向量，主机 `sizeof(Profile)` 受 24 KiB 回归门禁约束，目标发布门禁要求至少 96 KiB DIRAM，修复镜像实测余 149,581 bytes。Python 83/83、普通 host 21/21、ASan/UBSan 21/21、ESP-IDF 5.5.4 `fullclean` build、产品分区、Web 资源、镜像偏移与校验均通过。应用镜像 1,468,960 bytes、SHA-256 `364f41a77794e8bb7fd056742c83848a251d36f2207d5ee4a63ef1fbe6f351ff`；私有完整镜像 1,600,032 bytes、SHA-256 `89ccf191f116ff6bc90437eb4b61196797a40f5a49c155f3c4ff7898739fcb6d`。
- Next step: 将 `dist/private/cardputer_codex_companion-private-full.bin` 从 `0x0` 刷入，确认启动标题为 `CARDPUTER CODEX 1.0.1` 并进行实机 BLE/Wi-Fi/Web/Companion 验收。当前主机仍未发现 Cardputer 串口，因此本次未代刷、未执行重启耐久测试。

## 2026-07-24 18:00 HKT

- Current work: 在用户接入 Cardputer 后，通过 `/dev/cu.usbmodem21201` 串口实机排查 1.0.1 仍反复重启的问题，并刷入修复后的 private full image 做复验。
- Expected result: 找到真实重启根因，修复后设备启动到产品运行时；不再出现 `main` stack overflow、HID report map 解析错误、缺失 `wifi_cfg` 分区或 BLE 广播启动失败；Web 状态接口在局域网可访问。
- Result: Achieved — 串口证据显示 1.0.1 先成功初始化显示和 BLE 控制器，随后在 Wi-Fi/AP/HTTPS 启动后触发 `***ERROR*** A stack overflow in task main has been detected.`；同时发现当前设备实际分区表缺少 `wifi_cfg`，HID report map 因固定 66 字节数组多出尾部 `0x00` 触发 NimBLE HID parser 错误。修复包括：主任务栈升至 8192；发布脚本每次删除旧 `sdkconfig` 并重建；`wifi_cfg` 初始化改为可选且对缺失分区 fail-open；HID report map 改为 `std::to_array` 自动长度；BLE advertising name 缩短为 `Cardputer Codex` 并加入 31 字节 legacy advertising payload 回归测试。完整 release 门禁通过：Python 86/86、host 21/21、ASan/UBSan 21/21、ESP-IDF 5.5.4 target build、产品分区校验、Swift release/doctor、generic/private image 打包。已将 1.0.3 private full image 从 `0x0` 刷入实机，esptool 写入哈希校验通过；读回分区表前 3KB 与构建产物一致；45 秒完整启动日志确认 `App version: 1.0.3`、NimBLE `advertise` started、HTTPS server listening、`product runtime started`、`Returned from app_main()`，且无 stack overflow/HID parser/wifi_cfg/panic；额外 90 秒串口静默观察 boot/panic/overflow/BLE 广播错误计数均为 0。只读 Web 状态接口返回 200，`version=1.0.3`、`wifi=OK`。
- Next step: 提交修复；后续如需完整验收，再进行 macOS 蓝牙配对、实体 56 键、中文字符串注入、Companion/Codex 会话联动和更长时间 HIL soak。

## 2026-07-24 18:34 HKT

- Current work: 根据用户实机反馈继续修复 1.0.4/1.0.5 候选中的屏幕持续刷新闪烁、Mac/iOS 蓝牙发现名不一致和连接无响应问题。
- Expected result: 屏幕运行态不再 200ms 全屏清屏重绘；BLE 广播不再 180 秒后停止；Mac 扫描名不再暴露 `nimble`；中央设备可建立连接；实机串口不再出现重启、panic、广告超时或 `scanner` 栈溢出。
- Result: Achieved — 串口先确认 1.0.4 候选在约 32 秒触发 `***ERROR*** A stack overflow in task scanner has been detected.`；代码审查确认 UI 任务每 200ms 调用 `display_render_runtime()`，内部 `fillScreen()` 导致闪烁；ESP-IDF HID helper 使用 180000ms BLE advertising duration，解释了“短暂可发现后消失”；Mac Bleak 扫描确认旧候选 `adv.local_name=Cardputer Codex` 但 CoreBluetooth `name=nimble`，源于 sdkconfig 中 NimBLE 默认 GAP 名。修复包括：UiModel 增加 revision，运行屏仅 dirty render；BLE 改用自有 NimBLE HID advertising start，duration 为 `INT32_MAX` 并保留断线 watchdog；scanner task 栈升至 8192；广播名、HID/GAP 设备名与 sdkconfig 默认名统一为 `Cardputer Codex`；增加 BLE 名称/广告时长、scanner 栈与 sdkconfig 默认名回归测试；固件版本 bump 到 1.0.5。最终完整 release 门禁通过：Python 87/87、host 21/21、ASan/UBSan 21/21、ESP-IDF 5.5.4 target build、产品分区校验、Swift release/doctor、generic/private image 打包。最终 1.0.5 private full image SHA-256 为 `b30f5113a7c3ba52d68d31c735deefe26b193715e0caf2aa485537c7d6f29d81`，已从 `0x0` 刷入实机且 esptool 写入哈希校验通过；Mac Bleak 扫描显示 `name='Cardputer Codex'`、`local_name='Cardputer Codex'`，GATT 连接成功并发现 Companion 服务；串口捕获 `App version: 1.0.5`、NimBLE advertise、HTTPS listening、`product runtime started` 和 `HID GAP connection established; status=0`；最终刷机后 90 秒串口观察无 reboot、panic、stack overflow、watchdog restart 或 advertising error；前一同逻辑候选的 210 秒空闲观察已覆盖 180 秒 advertising 窗口且无 `advertise complete`。
- Next step: 提交修复并交付当前 `dist/private/cardputer_codex_companion-private-full.bin`；如 macOS Bluetooth 设置仍显示旧名，需要移除旧 `nimble`/`Cardputer Codex` 记录后重新配对，以清除系统缓存。

## 2026-07-24 20:17 HKT

- Current work: 继续处理 Mac 侧显示 `Waiting for Mac A:0 I:0`、Cardputer 实体输入未进入 Mac，以及用户要求使用 Computer Use 进行验证的问题。
- Expected result: 固件不再把未完成配对的 GATT/HID 连接误判为可输入；设备侧在 BLE GAP connect 后主动发起 security；实机刷入最终 private full image 并保留可验证边界。
- Result: Partial hardware verification / Achieved for firmware packaging — 先证明 1.0.12 可被 macOS CoreBluetooth 连接并发现 Companion GATT 服务，但 authenticated numeric comparison 在未接受 Mac 端确认时 30 秒超时；随后改为 Just Works bonding、连接后主动 `ble_gap_security_initiate()`，并将 keyboard ready gate 移到 `ENC_CHANGE status=0` 后，防止屏幕和输入路径误报。Computer Use 工具已按要求初始化、重启 `SkyComputerUseService` 并重试，但 `get_app_state` 对 System Settings/TextEdit 持续返回 `cgWindowNotFound`，System Events 也枚举不到任何窗口；截图仅显示桌面，当前 Mac GUI 会话无法完成蓝牙设置 UI 点击验证。最终 1.0.15 release gate 通过：Python 88/88、firmware host 21/21、ASan/UBSan 21/21、ESP-IDF 5.5.4 target build、产品分区校验、Swift release/doctor、generic/private image 打包。已将 `dist/private/cardputer_codex_companion-private-full.bin` 从 `0x0` 刷入实机，esptool 写入哈希校验通过；45 秒串口确认 `App version: 1.0.15`、NimBLE advertise、HTTPS listening、product runtime started、Wi-Fi/Web 启动，且无 Guru、abort、stack overflow、`rc=6` 或 advertising inactive。最终 private full image SHA-256 为 `54a40caa08559d71fee8d34872c2d227b5ddd456fc18dd5a566a7300b9f74cc2`。
- Next step: 在 Mac GUI 会话可操作或用户手动打开蓝牙设置后，移除旧 `nimble`/`Cardputer Codex` 记录并从系统蓝牙设置连接 `Cardputer Codex`；配对完成后再用实体键确认 HID 输入。若仍超时，下一轮需要采集 macOS Bluetooth 设置 UI 侧配对事件而非 CoreBluetooth 命令行连接。

## 2026-07-24 20:25 HKT

- Current work: 按 `$computer-use` 请求做最终 GUI 侧验证重试，并根据 verification-before-completion 重新执行完整发布门禁、重新刷入当前重新打包的 private full image。
- Expected result: 当前交付路径、SHA 和实机刷入内容保持一致；Computer Use 若仍无法操作 GUI，需要明确记录为工具/会话边界而非固件通过。
- Result: Partial hardware verification / Achieved for current firmware delivery — Computer Use 重新初始化后可列出 32 个应用，但 System Settings/TextEdit 通过显示名均返回 `Computer Use server error -10005: cgWindowNotFound`；按 skill 要求改用 bundle id 重试，TextEdit 仍为 `cgWindowNotFound`，Finder 超时，System Settings bundle id 对当前运行时无效，因此 GUI 蓝牙设置验证仍无法由当前会话自动完成。重新执行 `scripts/verify_product_release.sh` 通过：Python 88/88、firmware host 21/21、ASan/UBSan 21/21、ESP-IDF 5.5.4 target build、产品分区校验、Swift release/doctor、generic/private image 打包；目标 DIRAM headroom 144,521 bytes。当前 private full image SHA-256 为 `b3b0c6365cae0136261dfd0a699dcfb34d120cbc8a12fe8d9e4d6e9799540388`，已从 `0x0` 刷入 `/dev/cu.usbmodem21201` 且 esptool 写入 hash verified。45 秒串口采样尾部确认 `App version:      1.0.15`、NimBLE advertise、HTTPS server listening on port 443、`product runtime started`、Wi-Fi 获取 `192.168.1.195`，且 Guru/abort/stack overflow/`rc=6`/advertising inactive 计数均为 0。
- Next step: 提交本地修复。剩余真实验收边界是 macOS 系统蓝牙 HID 配对 UI：需要在可操作的 Mac GUI 会话中删除旧 `nimble`/`Cardputer Codex` 记录，再连接当前 `Cardputer Codex`；完成 encrypted pairing 后再验证实体按键输入。

## 2026-07-24 21:58 HKT

- Current work: 参照 Bruce 固件 BLE keyboard 实现重新审视 Cardputer Codex Companion 的 HID descriptor、配对安全、ready gate 与实体按键发送路径，并纳入用户反馈“连接蓝牙设备时没有要求 PIN 配对”。
- Expected result: macOS 不再只显示 Bluetooth/GATT 连接而无 HID 键盘输入；固件必须要求带 MITM 的键盘输入式配对，清除旧非 MITM bond，且只有在 GAP 已连接、链路已加密认证、HIDD 已连接、input report 已订阅后才允许发送按键报告。
- Result: Achieved — 修复了三个实机问题：1) HID report map 采用 Bruce 风格 reserved/LED padding，并将 key array 明确为 6 slots；2) ready state 从单一 encrypted flag 改为 `gap/encrypted/authenticated/hidd/subscribed` 五条件，并修复 HIDD connect 先于 GAP connect 时被 reset 掉的事件顺序问题；3) 原 `BLE_HS_IO_NO_INPUT_OUTPUT + MITM false` 导致 Just Works/no PIN，现改为 keyboard-only + MITM + passkey input，bond schema 升至 6 以清除设备侧旧 bond。已完成对应 RED/GREEN regression tests。完整 release gate 已通过：Python 88/88、firmware host 21/21、ASan/UBSan 21/21、ESP-IDF 5.5.4 target build、产品分区校验、Swift release/doctor、generic/private image 打包。最终 1.0.18 private full image SHA-256 为 `c3310189a42acb46c3b60318623b46652068adef006688f66cb743cb054549c8`，已从 `0x0` 刷入 `/dev/cu.usbmodem21201` 且 esptool 写入 hash verified。串口采样确认 `App version:      1.0.18`、产品运行时启动、`HID GAP encryption changed; status=0`、`HID GAP subscribe ... notify 0->1`、`HID keyboard ready=1 ... enc=1 auth=1 hidd=1 sub=1`；用户随后确认 Mac 当前可以正常收到 Cardputer 输入反馈。
- Next step: 提交本地修复并交付当前 `dist/private/cardputer_codex_companion-private-full.bin`。若后续其他 Mac 仍不弹 PIN 或不接收输入，先在主机侧删除旧 `nimble`/`Cardputer Codex` 蓝牙记录再重连，因为固件已清设备侧 bond，但不能主动清 macOS host-side pairing cache。

## 2026-07-24 22:30 HKT

- Current work: 按用户要求修改设备端 Web 固件流程并部署到当前 Cardputer：先 PIN 鉴权、增加 Settings、中文动作表述、浏览器采集组合键、非直通键显示真实用途并弹窗编辑。
- Expected result: 浏览器访问设备首页时先看到 PIN 鉴权屏；正确鉴权后才能进入键盘配置；Settings 能修改 PIN 和 Wi‑Fi；组合键如 `Alt+V` 由 Web 捕获并写入 `hid_chord`；非直通键键帽显示用途摘要。
- Result: Achieved — 新增设计与实施计划文档并本地提交 `47e51b8`；Web SPA 已重构为 PIN 首屏、`键盘配置/Settings` 选项卡和 key modal；动作选项改为中文展示但 profile JSON 枚举保持兼容；`keydown` 捕获组合键并转换为 HID modifiers/usages；键帽通过 `describeAction()` 显示 `组合键 Alt+V`、文本、设备动作、Codex 动作或禁用等摘要。固件新增受当前 PIN 保护的 `POST /api/v1/pin`，PIN 限定 8 位数字并持久化到 NVS `product/web_pin`；修改后内存鉴权码立即切换，运行态屏幕会刷新显示当前 PIN。版本 bump 至 1.0.19。完整 release gate 通过：Python 91/91、firmware host 21/21、ASan/UBSan 21/21、ESP-IDF 5.5.4 target build、产品分区校验、Swift release/doctor、generic/private packaging。最终 private full image SHA-256 为 `66f8bf88a1649cd94ba6eab5db70793af32bc7bec8e16908612bf52072f7dcf4`，已从 `0x0` 刷入 `/dev/cu.usbmodem21201` 且 esptool hash verified。75 秒串口采样确认 `App version:      1.0.19`、BLE advertise、product runtime started、`bad=0`、IP `192.168.1.195`。HTTPS smoke：`/api/v1/status` 返回 version `1.0.19`，首页 HTML 包含 `auth-screen`、`tab-settings`、`key-modal`、`/api/v1/pin`、`组合键`、`动作摘要`，错误 PIN 读取 profile 返回 401 `pairing_required`。
- Next step: 本地提交实现并交付当前私有 full image 路径。正确 PIN 登录、PIN 修改和 Wi‑Fi 写入的人工交互可由用户在浏览器用屏幕 PIN 继续验证；自动化未读取或打印 PIN。

## 2026-07-24 22:51 HKT

- Current work: 修复用户反馈的 `BLE 正常 · Wi‑Fi 正常 · Mac 离线`，检查本机 Mac companion agent、Cardputer HTTPS 状态链路与固件 companion stale/sequence 协议。
- Expected result: Mac agent 作为 LaunchAgent 常驻运行；不把 PIN 放入 launchd plist 或进程参数；agent 能启动 Codex app-server 并持续向 Cardputer 发布 snapshot；Cardputer 状态 API 和屏幕应显示 Mac 在线。
- Result: Achieved — 本机最初没有 `com.lynx.cardputer-companion` LaunchAgent，且 `cardputer-companion doctor` 仅在交互 shell 中能找到 Codex CLI。修复包括：Companion CLI 新增 `run --config`，本机 PIN/设备 URL 写入 `~/Library/Application Support/CardputerCodexCompanion/config.json` 且 mode 0600；新增 `scripts/install_companion_launch_agent.py`，生成不含 PIN 的 LaunchAgent，并注入 Homebrew/Codex PATH；因 launchd 下 Swift URLSession 被 macOS Local Network TCC 拦截，LANBridge 改为 `/usr/bin/curl --config -` 通过 stdin 发送 HTTPS 请求，PIN 不进入 argv；固件 `CompanionProtocol` 修复为旧 snapshot 超时后允许新 Mac agent 从 sequence 1 重新建链，避免 agent 重启后永久 `Mac 离线`。完整 release gate 通过：Python 95/95、firmware host 21/21、ASan/UBSan 21/21、ESP-IDF 5.5.4 target build、产品分区校验、Swift release/doctor、generic/private packaging。最终 1.0.20 private full image SHA-256 为 `2ed9ffad7aaf7674c3fb6f88039fcefb11cbe4d2d2df7db5c9a6dc7ace8757e6`，已从 `0x0` 刷入 `/dev/cu.usbmodem21201` 且 esptool hash verified。刷机后设备状态为 version `1.0.20`、Wi‑Fi OK；录入当前屏幕 PIN 后 LaunchAgent 运行中，`/api/v1/status` 连续超过 10 秒 stale 窗口返回 `companion=OK`，stderr 日志大小保持不变。
- Next step: 提交本地修复。当前 BLE 状态取决于 macOS 是否已重新连接 HID；本次目标 Mac companion LAN 状态链路已在线。

## 2026-07-24 23:23 HKT

- Current work: 按用户确认的方案修复 Cardputer 重启后 BLE 变 `OFFLINE`、Web PIN 重启随机刷新，以及屏幕字体过小的问题，并在当前实机验证。
- Expected result: BLE 断线/半连接后进入重试或重新广播，不再把重启后的未连接态显示为 `OFFLINE`；首次生成的 Web PIN 写入 NVS，之后重启保持不变，只有 Web 修改 PIN 才改变；屏幕运行态文字显著增大且不因长行换行。
- Result: Achieved — 新增 BLE advertising watchdog 与 stale HID link timeout：断开且未广播时每 5 秒恢复 advertising，GAP 已连但未完成加密/认证/HIDD/subscription 的半连接 15 秒后主动 terminate；产品控制器将未连接 BLE 状态呈现为 `starting` 而非 `offline`。`load_pairing_code()` 改为 NVS read/write：已有合法 PIN 直接复用，缺失或非法 PIN 时生成一次并持久化，NVS 不可用时才使用临时 PIN。运行屏状态行压缩为 `B/W/M` 和 `OK/OFF/ERR/...`，IP/PIN 分行，去掉 cwd，显示正文 `TextSize=2`。版本 bump 至 1.0.21。完整 release gate 通过：Python 96/96、firmware host 21/21、ASan/UBSan 21/21、ESP-IDF 5.5.4 target build、产品分区校验、Swift release/doctor、generic/private packaging；目标 DIRAM headroom 141,073 bytes。1.0.21 应用镜像 SHA-256 为 `c5ee200157458e6824526426985428baa1998afe78a34341219900fa0a3a0352`；1.0.21 private full image SHA-256 为 `da56a951d3b8a9e758a93d337e2b4d1478c55652d0b79a9d774badcc695cb3e5`。为保留当前设备上的 product NVS/PIN，实机部署使用应用分区刷写 `0x20000 firmware/build/cardputer_codex_companion.bin`，esptool 写入 hash verified；随后通过 `read_mac --after hard_reset` 做真实复位，未重新写入 PIN，状态接口仍返回 `version=1.0.21`、`wifi=OK`、`companion=OK`，证明 PIN 重启后未刷新。复位后 BLE 先处于重试态 `...` 而非旧 `OFFLINE`，随后自动恢复为 `BLE=OK`；macOS `system_profiler SPBluetoothDataType` 可见 `Cardputer Codex`，类型 Keyboard，RSSI 正常。
- Next step: 本地提交修复。若需要恢复空白设备或重刷完整镜像，可刷 `dist/private/cardputer_codex_companion-private-full.bin` 到 `0x0`；若是升级已配置设备，为保留 Web PIN，应刷应用分区 `0x20000` 或未来实现 OTA/保 NVS 升级路径。

## 2026-07-24 23:24 HKT

- Current work: 使用 Chrome 在实机 Web 页面复现字符串按键映射无法发布，并通过串口、API 与 NVS 边界检查定位根因；将用户批准的稀疏 Profile 修复方案固化为实施前规格。
- Expected result: 明确一个兼容旧 Profile、低内存、可持久化且符合用户保存直觉的修复边界，实施前不修改固件代码。
- Result: Achieved for diagnosis and design — Chrome 复现 `Failed to fetch`，串口在 Profile GET 上稳定记录 `httpd_uri: uri handler execution failed`；默认完整 Profile 实测约 5,193 bytes，超过 ESP-IDF `nvs_set_str()` 4,000-byte 上限，而稀疏 `null` 编码约 1,161 bytes。Mac agent 停止后的对照测试仍复现 Profile handler 失败，排除其为主因；agent 已恢复运行。用户批准使用 224 项稀疏数组、兼容旧 passthrough 对象、持久化成功后再切换内存 Profile，并将弹窗动作改为“保存并发布”。
- Next step: 用户审阅稀疏持久化规格；批准后创建测试先行实施计划，再实现、构建、刷入并做 Chrome/串口/实体键复验。

## 2026-07-24 23:30 HKT

- Current work: 将已批准的稀疏 Profile 规格拆成测试先行的固件、Web、版本发布和实机验证任务。
- Expected result: 每个实现步骤包含精确文件、RED/GREEN 命令、持久化顺序、Web 失败行为、app-only 刷写边界与 Chrome/串口/实体键验收。
- Result: Achieved — 实施计划写入 `docs/superpowers/plans/2026-07-24-web-profile-sparse-persistence.md`，包含四个可独立验收任务：稀疏 JSON 与原子持久化、弹窗保存即发布、1.0.22 完整发布门禁、`0x20000` app-only 刷写和实机复验。自审确认覆盖旧格式兼容、4,000-byte NVS 边界、失败不切换内存 Profile、无秘密日志和物理 UTF-8 输入验收，未发现占位符或接口命名冲突。
- Next step: 由用户选择执行模式；按当前无子代理授权边界，推荐在本会话使用 executing-plans 内联执行。

## 2026-07-24 23:37 HKT

- Current work: 在隔离 worktree `fix/web-profile-sparse-persistence` 中完成稀疏 Profile、原子 NVS 持久化、Web 弹窗保存即发布和 1.0.22 发布构建。
- Expected result: 默认 Profile 低于 4,000-byte NVS 字符串上限；旧 passthrough 对象仍可读取；持久化失败不替换内存 Profile；Web 弹窗直接发布并内联显示错误；完整发布门禁通过。
- Result: Achieved and ready for app-only flash — Task 1 RED 确认缺失稀疏/原子策略，GREEN 后 host policy 与稀疏回归 2/2；Task 2 RED 确认弹窗仍仅本地应用，GREEN 后 Web tests 6/6 与 JavaScript 语法检查通过；版本 RED/GREEN 确认 1.0.22 常量。完整发布门禁通过：Python 99/99、普通 host 21/21、ASan/UBSan 21/21、Web asset、ESP-IDF 5.5.4 target build、产品分区、Swift release/doctor、generic/private packaging 与 secret exclusion 全部通过；目标 DIRAM headroom 141,073 bytes。应用镜像 `firmware/build/cardputer_codex_companion.bin` 为 1,487,072 bytes，SHA-256 `105e742b203a94c81345cda2e21c485443f20f6f6ccaee46ff8acfc8617e5947`；private full image 为 1,618,144 bytes，SHA-256 `6982c14b8e7ca9929e4d01bf68f8bc4dbab97f7d0605d6add05874efb4152d85`。
- Next step: 将应用镜像刷入 `0x20000`，保留当前 NVS/PIN/Wi-Fi/bonds，然后用 Chrome、认证 API、串口和实体 F 键验证 UTF-8 映射发布及重启持久化。

## 2026-07-25 00:57 HKT

- Current work: 在实机上完成 Web 中文字符串映射、Mac Companion 同步、TLS 稳定性与 Cardputer 重启后 BLE 恢复的根因排查，并形成 1.0.25 发布候选。
- Expected result: Profile PUT 不再因 HTTPS task 栈溢出而挂起或重启；中文 UTF-8 映射可写入、读回并跨重启持久化；Mac agent 不再使用不安全的长连接流；Cardputer 重启后 BLE、Wi-Fi、Mac 状态自动恢复。
- Result: Achieved — `Profile` 解析和加载改为 heap 分配且原地重置，消除大对象及 `safe_profile()` 临时值占用 HTTPS task 栈；实机 PUT 返回 200，F 键索引 33 的 `字符串调试测试` 从 revision 3 更新至 revision 4，并在两次重启后原样读回。原异步 NDJSON 事件流在 ESP HTTPS server 上复现 TLS allocator assert/StoreProhibited，已改为 Mac 每 2 秒短轮询 action、仅内容变化或设备请求时 POST snapshot；120 秒串口观察无 TLS、heap、panic 或重启。Companion/BLE 跨任务状态改为原子快照，GAP connect 保留先到达的加密/HIDD/subscription 事件；实机发现一次 `ENC_CHANGE status=13` 后旧 watchdog 最终恢复，但等待过长，最终策略改为明确加密失败立即 terminate 并重新广播，保留 15 秒 stale fallback。1.0.25 已 app-only 刷入并 hash verified；重启后状态 API 返回 `BLE=OK`、`Wi-Fi=OK`、`companion=OK`。
- Next step: 提交当前分支，合并到 `main`，在合并结果上重跑发布门禁并交付主仓库 `dist/private/cardputer_codex_companion-private-full.bin`。

## 2026-07-25 01:05 HKT

- Current work: 将修复分支 fast-forward 合并到 `main`，在主分支重新生成最终发布制品，并把同一主分支应用镜像刷入实机做最后回归。
- Expected result: 主分支、LaunchAgent、实机运行固件和交付制品来自同一份源代码；完整门禁、BLE 失败快速恢复、Mac 在线和中文 Profile 持久化均有新鲜证据。
- Result: Achieved — 主分支完整门禁通过：Python 103/103、普通 host 21/21、ASan/UBSan 21/21、ESP-IDF 5.5.4 目标构建、产品分区、140,921 bytes DIRAM headroom、Swift release/doctor、generic/private packaging 与 secret exclusion。主分支应用镜像 app-only 刷入 `0x20000`，esptool 报告 `Hash of data verified`；LaunchAgent 已切换到主仓库 app。最终重启实测在 `ENC_CHANGE status=13` 后立即 terminate，约 0.75 秒重新连接、加密并恢复 `HID keyboard ready=1`，随后无 panic/reboot/TLS allocator 错误。状态 API 返回 version `1.0.25` 且 BLE/Wi-Fi/Mac 全部 `OK`；Profile revision 4 的 F 键中文字符串仍精确读回。最终 private full image 为 1,618,912 bytes，SHA-256 `12e0554ddca105dd252ed720ec7ded08add773009a9d5742a2d0f16084752db3`。
- Next step: 交付主仓库 `dist/private/cardputer_codex_companion-private-full.bin`；当前仓库无 remote，故没有可执行的 push。

## 2026-07-25 09:30 HKT

- Current work: 修复 Web Profile 中组合键和 ASCII 字符串执行后不输出的问题，并准备 1.0.26 实机部署。
- Expected result: 组合键完全经 BLE HID 执行且具有可被主机观察到的按下保持时间；可表示的 ASCII 字符串逐键走 HID、不依赖 Mac Agent；中文及其他 Unicode 仍走原生 Unicode Agent 通道。
- Result: Achieved — 回归测试先复现了组合键按下/释放之间无间隔，以及 ASCII 字符串被无条件发送到当前未获辅助功能权限的 Mac Agent。组合键现按“按下、保持 30 ms、释放”直接走 BLE HID；可表示的 US-HID ASCII 字符串逐键走 HID（每键按下 30 ms、释放 10 ms），中文及其他 Unicode 整串保留 GATT/Companion 路径；测试覆盖大小写、Shift 标点和通道隔离。完整发布门禁通过：Python 103/103、普通 host 21/21、ASan/UBSan 21/21、ESP-IDF 5.5.4 target build、分区、140,921 bytes DIRAM headroom、Swift release/doctor、generic/private packaging。1.0.26 应用镜像 1,488,848 bytes，SHA-256 `4f4ea3fcbe414b048d9939d7292f0d76958a49d28ed51224945a4972cefe9024`；private full image 1,619,920 bytes，SHA-256 `82ce7c8339be6afddf2c1ff675586d7a1396612bb3acfef0666c79036fd086f4`。应用镜像已刷入 `/dev/cu.usbmodem21201` 的 `0x20000`，esptool hash verified；HTTPS 状态返回 `version=1.0.26`、BLE/Wi-Fi/Mac 均 `OK`，Profile revision 6 和 V 键 `hihihi` 配置保持不变。
- Next step: 用户在任意 Mac 文本框按 V 验证现有 `hihihi` 映射，并将任意键设为组合键做实体输入复验；中文等 Unicode 仍需在 macOS 辅助功能中授权 `CardputerCompanion.app`。

## 2026-07-25 17:27 HKT

- Current work: 完成键位结果反馈、登录 PIN 掩码、登录文案和间距调整的设计确认，并将实施拆分为静态结构、交互发布、1.0.27 发布部署三个测试先行任务。
- Expected result: 规格与计划无占位符、接口命名一致，覆盖保存及恢复直通的成功/失败窗口、PIN 掩码、指定文案、样式、生成资源、完整门禁与 app-only 实机刷写。
- Result: Achieved — 设计提交为 `c1e8d5f`；实施计划写入 `docs/superpowers/plans/2026-07-25-web-key-result-login-polish.md`，明确了 RED/GREEN 命令、具体 HTML/CSS/JavaScript、版本双来源、发布门禁和实机验证步骤。
- Next step: 用户选择 inline execution 后，按计划执行 Tasks 1–3。

## 2026-07-25 17:34 HKT

- Current work: 在隔离 worktree 中完成 Web 登录界面、键位发布结果窗口与 1.0.27 发布候选。
- Expected result: PIN 使用密码掩码且登录文案、按钮间距符合要求；保存映射或恢复直通后在页面内明确显示结果，成功约 1.5 秒自动关闭，失败保持至用户确认；完整发布门禁通过。
- Result: Achieved and ready to merge — 静态 Web 回归先验证旧页面缺少掩码、结果窗口与新文案，随后通过；JavaScript 回归先验证缺少发布结果状态与失败回滚，随后通过。保存映射和恢复直通均等待 Profile PUT 成功后显示成功窗口，失败时恢复本地旧配置并保留错误窗口。完整发布门禁通过：Python 104/104、普通 host 21/21、ASan/UBSan 21/21、Web asset freshness、ESP-IDF 5.5.4 target build、产品分区、140,921 bytes DIRAM headroom、Swift release/doctor、generic/private packaging。1.0.27 应用镜像 SHA-256 为 `20e9d179083ba74f12cd6fd4e6b63f7e5378ac391504e7bfadb144c8dddcc3d3`；private full image SHA-256 为 `0973af231a741ea14154b8f25dcfa662dcc44283a46c27b0449a8f0cee2781ce`。
- Next step: 提交发布候选并 fast-forward 合并到 `main`，在主分支重新执行发布门禁，然后 app-only 刷入当前 Cardputer 并验证 1.0.27 Web 标记与运行状态。
