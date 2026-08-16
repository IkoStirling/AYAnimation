# AYAnimation

AYAnimation 是角色和骨骼动画运行时，提供动画播放、混合空间、状态机、骨骼遮罩、Notify、Additive Layer 与 IK 求解。

## 主要能力

- 1D/2D BlendSpace 与状态机
- Skeleton Mask、共享骨架 Tick Cache
- Animation Notify 与状态切换事件
- Two-Bone、FABRIK、CCD IK
- 条件表达式与字节码求值

## 公开接口

```cpp
#include <AYAnimation.h>
#include <AYAnimation/AnimationPlayer.h>
#include <AYAnimation/BlendSpace.h>
#include <AYAnimation/StateMachine.h>
```

入口头文件位于模块根目录，公开头位于 `include/AYAnimation/`。

## 依赖

- AYMath
- AYResource
- AYTest（仅测试）

完整设计与当前交付状态见 [design.md](design.md)。
