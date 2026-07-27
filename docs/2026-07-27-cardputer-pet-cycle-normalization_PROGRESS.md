# Cardputer 宠物周期泛化修复进度

## 2026-07-27 20:34 HKT

- Current work: 完成 B+W+M+ 下 Rocky 动画后半周期重复前两帧的根因定位，并将
  用户确认的严格像素周期规则固化为设计规格。
- Expected result: 不假设特定素材布局；只有完整 RGB565 像素序列严格证明存在
  较短周期且至少两个尾帧重复前缀时，才按较短周期重新展开为八帧，否则保持现有
  保守回退。
- Result: Achieved。已证明固件索引正常遍历 0...7；根因是 Mac 转码器将 Rocky
  的 `A,B,C,D,A,B,--,--` 错误补为 `A,B,C,D,A,B,A,B`。设计保持 CCPT v1、
  固件解码、LCD 提交、状态映射和 MIC 路径不变，并要求版本统一提升到 1.1.6。
- Next step: 提交设计与进度文档，等待用户完成书面规格门禁；获批后编写 TDD
  实施计划。

## 2026-07-27 20:42 HKT

- Current work: 将用户批准的严格周期设计拆分为可独立审查的 TDD 转码修复、
  1.1.6 版本一致性和完整构建/安装/app-only 刷写/实机门禁任务。
- Expected result: 每个任务包含准确文件、接口、RED/GREEN 命令、提交边界和
  最终物理验收；执行者不需要推测像素比较、回退逻辑、版本面或部署顺序。
- Result: Achieved。计划要求至少两个尾帧严格复现前缀、完整 RGB565 数组相等、
  最小已证明周期和非周期回退；保持 CCPT v1 与固件渲染不变，并禁止串口作为
  post-flash 被动监控。
- Next step: 自检并提交实施计划；由用户选择 subagent-driven 或当前会话内联执行。

## 2026-07-27 21:20 HKT

- Current work: 完成严格周期重建、1.1.6 构建与 app-only 烧录，并执行实体动画
  门禁。
- Expected result: Rocky 在 B+W+M- 与 B+W+M+ 下均遍历完整动作周期。
- Result: Partial。B+W+M- 已恢复完整周期；B+W+M+ 仍只显示两次静态画面。
  代码检查证明 `display_policy.cpp` 会在 MIC starting/live16/live24/stopping
  状态禁止动画帧索引递增，所以素材周期修复无法覆盖 MIC 活跃态。
- Next step: 先以主机测试要求 MIC 活跃态在帧到期时返回
  `animated_frame`，观察 RED；再移除运行时冻结策略、提升版本至 1.1.7，
  重新执行完整门禁与实体 M+ 验收。

## 2026-07-27 21:45 HKT

- Current work: 对 1.1.7 的第二次实体失败执行端到端帧链路与 Codex 状态取证。
- Expected result: 区分素材帧折叠、CCPT 编解码错误、运行时帧冻结和在线状态映射。
- Result: Achieved。Rocky CCPT 摘要与设备完全一致；WORKING/WAITING 均为严格
  证明的四帧周期，IDLE 则是素材原生的两帧周期。Codex app-server 连续 20 次
  返回 `notLoaded`，旧映射将其归为 IDLE，因此 Companion 在线后合法但错误地
  选择了两帧素材；M- 则固定使用 WAITING。
- Next step: 将 `notLoaded` 精确映射为 WAITING，保留未知状态回退 IDLE 和
  ACTIVE→WORKING；统一提升至 1.1.8，完成门禁、Mac Agent 升级、app-only
  烧录与第三次实体验收。

## 2026-07-27 23:16 HKT

- Current work: 完成 1.1.8 全量发布、Mac App/HAL/AudioBridge 安装、app-only
  实机烧录、HTTPS 稳定性采样、宠物同步校验和实体动画门禁。
- Expected result: Companion 在线且 Codex 会话尚未装载时选择 WAITING 的完整
  四帧周期；MIC 活跃时动画继续运行；不改变 Rocky 资源、CCPT v1、NVS 配置或
  用户确认的严格像素周期规则。
- Result: Achieved。`notLoaded` 现在精确映射为 WAITING，ACTIVE 仍映射
  WORKING，其他未知状态仍保守回退 IDLE。完整门禁通过 Python 206/206、
  音频专项 29/29、普通与 sanitizer host 各 37/37、ESP-IDF clean build
  （app `0x187770`、分区剩余 49%、DIRAM headroom 144,545 bytes）及
  Swift/HAL/签名/private packaging。Mac App 与 HAL 均安装为 1.1.8；
  `0x20000` app-only 写入和独立 `verify_flash` digest matched；最终 HTTPS
  连续 20/20 为 1.1.8、BLE/Wi-Fi/Companion OK、MIC READY。Rocky digest
  保持 `53ad97058ec2507c28698e9dc7f23593a0945a8eeaf7dd3a02747283c603433d`，
  同步事务未激活且结果为 `cached`。用户确认 B+W+M+ 与 MIC 16K 两种状态均
  显示完整动画，不再只重复前两帧。
- Next step: 提交 1.1.8 验证记录与 SHA-256 清单，完成分支集成和最终构件交付。

## 2026-07-27 23:27 HKT

- Current work: 完成 1.1.8 发布记录、本地 fast-forward 集成、合并结果验证、
  精确构件同步和功能 worktree 清理。
- Expected result: `main` 包含全部修复和验证记录；主目录交付件与实机最终刷入件
  一致；功能分支不再残留。
- Result: Achieved。`main` fast-forward 至 `5c686e1`。合并态 Python
  206/206、普通与 sanitizer host 各 37/37、Swift/ProductAudio/
  ProductGATT/ProductConfiguration、音频 ring 和安装器 20/20 全部通过。
  首次合并测试发现主目录独立测试用 HAL 构件仍为旧 1.1.5，按发布脚本重建后
  从头复验通过；交付 App、固件和已安装 HAL 未受影响。五项 1.1.8 构件在主目录
  再次通过 SHA-256 清单和 codesign 校验，功能 worktree 与分支已删除。仓库没有
  远端，因此没有 push。
- Next step: 无；交付 1.1.8 固件、Mac App/安装器、校验清单和验证记录。
