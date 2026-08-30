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

AYAnimation 使用一套男女老少共用的语义骨架：以 [VRM 1.0 Humanoid](https://github.com/vrm-c/vrm-specification/blob/master/specification/VRMC_vrm-1.0/humanoid.md) 的 55 个角色为主体，增加可选的 `SceneRoot` 与 `MotionRoot` 两个引擎根。性别、年龄和体型差异放在 bind pose、骨长、关节朝向与网格中，不复制角色层级。

[glTF 2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html) 和 `ISkeleton` 负责保存真实节点与蒙皮数据；[H-Anim](https://www.web3d.org/documents/specifications/19774/V1.0/HAnim/concepts.html) 仅作为正式人体标准参考。MMD、Mixamo 被视为来源适配器，原始辅助骨和层级应保留。

当前没有内置 MMD/Mixamo 名称别名或自动猜测。`HumanoidBoneMap` 默认 57 项全部未映射，等待代表性真实资产验证后再补来源 adapter。`validateHumanoidSkeleton()` 校验 15 个必需角色、索引唯一性和语义祖先链，并允许 twist、IK、grant 等未映射骨位于标准角色之间。

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
