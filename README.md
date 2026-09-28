# AYAnimation

AYAnimation 是角色和骨骼动画运行时，提供动画播放、混合空间、状态机、骨骼遮罩、Notify、Additive Layer 与 IK 求解。

## 主要能力

- 1D/2D BlendSpace 与状态机
- Skeleton Mask、共享骨架 Tick Cache
- Animation Notify 与状态切换事件
- Two-Bone、FABRIK、CCD IK
- 条件表达式与字节码求值
- AYHumanoid 语义骨架、显式骨索引映射与层级校验

## AYHumanoid 骨架约定

统一条件见 [AYHumanoid 标准骨架与参考模型规范](../../AYDocs/AYHUMANOID-STANDARD.md)，制作见 [low poly 实施计划](../../AYDocs/AYHUMANOID-IMPLEMENTATION-PLAN.md)。通用语义允许可选骨缺失；计划中的仓库参考资产使用完整 57 个角色，并冻结具体绑定数据。

当前已实现显式映射和 15 个必需角色的语义祖先校验，默认映射为空；标准参考资产生成、MMD/Mixamo 内置映射、自动猜测映射与根运动提取尚未交付。实现不变量与测试历史见 [design.md](design.md) §7。

已提供 UI-free `AYAnimationEditorCore`，AYEditor 通过薄适配接入：

- 源骨架保持只读；手工/模板映射、参考姿势与骨轴修正保存在可绑定的 `.ayrig` RigProfile。
  旧 `.aysmap` 仅作迁移输入，不再作为新建格式。
- source→target 局部姿势/离线 Clip 求解、源目标同步预览、清理烘焙与引用闭包校验已接入。
  发布使用 staging/receipt 与源指纹校验；未知 TRS、未映射动画骨、Additive 重定向明确拒绝。
- 跨骨架网格几何重绑定尚未实现，带网格的 BakeToTarget 安全阻止；Cubic Quaternion
  重定向尚未支持切线空间变换，不能当作 Linear 降级发布。
- 动画预览支持模型/骨架及组合模式；轨道、关键帧分量、Linear/Step/Hermite 和切线编辑、
  Notify、Clip 属性、撤销与保存已接入。Quaternion 使用运行时正式采样语义。

资源管线详见 [骨骼动画资源管线设计](../../AYDocs/SKELETAL-ANIMATION-RESOURCE-PIPELINE.md)，
本轮加固与批量作者工具实施见 [连续实施记录](../../AYDocs/animation-authoring-hardening.md)。

## 公开接口

```cpp
#include <AYAnimation.h>
#include <AYAnimation/AnimationPlayer.h>
#include <AYAnimation/BlendSpace.h>
#include <AYAnimation/StateMachine.h>
#include <AYAnimation/HumanoidSkeleton.h>
```

入口头文件位于模块根目录，公开头位于 `include/AYAnimation/`。

## 依赖

- AYMath
- AYResource
- AYTest（仅测试）

完整设计与当前交付状态见 [design.md](design.md)。

作者核心 `AYAnimationEditorCore` 独立于 AYEditor/UI。AnimationAuthoring 支持跨轨/
Notify 原子移动删除；AnimationClipboard 使用内存 payload（相对秒数、值及切线），
不新增扩展名。跨 Clip 粘贴要求唯一兼容目标轨道，不覆盖已有关键帧、不自动映射骨骼。
AYEditor 提供 Copy/Cut/Paste at Playhead/Duplicate After 与 Ctrl+C/X/V/D，
剪切成功后才替换会话剪贴板；烘焙输出和旧格式仅允许预览/复制。

运行时 fast/stress 与作者核心 integration 使用独立清单，见[统一测试契约](../../AYDocs/testing.md)。

`KeySampler` 的 Hermite 段数学复用 `AYMath/CurveMath.h`；轨道插值策略、
Quaternion 最短弧与归一化仍由 AYAnimation 负责，不依赖作者控件。编辑器应通过
自己的资源适配器调用正式 KeySampler，避免曲线图与实际播放使用不同采样语义。
