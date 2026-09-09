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

当前已实现显式映射和 15 个必需角色的语义祖先校验，默认映射为空；标准资产生成、MMD/Mixamo 内置映射、自动重定向与根运动提取尚未交付。实现不变量与测试历史见 [design.md](design.md) §7。

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
