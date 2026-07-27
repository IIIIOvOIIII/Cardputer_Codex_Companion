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
