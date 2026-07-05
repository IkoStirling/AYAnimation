# AYAnimation Design

> 工业级动画系统，目标对标 Unreal Engine / Unity Animation System

## 1. 概述

AYAnimation 是 AY Engine 的**动画子系统**，负责：
- 动画播放与混合
- 骨骼变换计算
- IK求解（FABRIK + CCD + 物理约束）
- 蒙皮计算（CPU + GPU）
- 骨骼重定向
- Motion Matching 状态机

### 1.1 设计目标

- **完整状态机**：L4 MotionMatching 风格，支持 BlendTree + 嵌套状态机
- **双模蒙皮**：CPU 蒙皮（灵活）+ GPU 蒙皮（高效），按需切换
- **自适应压缩**：动画数据根据使用场景自动压缩
- **离线重定向**：打包时建立骨骼映射，运行时直接使用
- **工业级 IK**：FABRIK + CCD + 物理约束

### 1.2 在引擎中的位置

```
┌─────────────────────────────────────────────────────────────────┐
│                        Game Engine                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────┐    ┌──────────────────────────────────────┐  │
│  │  AYResource │───▶│           AYAnimation               │  │
│  │  (资源管理)  │    │                                      │  │
│  └─────────────┘    │  ┌────────────────────────────────┐   │  │
│                      │  │ AnimationPlayer           │   │  │
│                      │  │   (播放控制 + 时间轴管理)      │   │  │
│                      │  └──────────────┬─────────────────┘   │  │
│                      │ │                     │  │
│                      │  ┌──────────────▼─────────────────┐   │  │
│                      │  │    AnimationStateMachine     │   │  │
│                      │  │   (L4 状态机 + BlendTree)     │   │  │
│                      │  └──────────────┬─────────────────┘   │  │
│                      │                 │                     │  │
│                      │  ┌──────────────▼─────────────────┐   │  │
│                      │ │    SkeletonTransformCalculator │   │  │
│                      │  │   (骨骼矩阵 + 蒙皮计算)        │   │  │
│                      │ └──────────────┬─────────────────┘   │  │
│                      │                 │                     │  │
│                      │  ┌──────────────▼─────────────────┐   │  │
│                      │  │         IKSolver               │   │  │
│                      │  │  (FABRIK + CCD + 物理约束)     │   │  │
│                      │  └────────────────────────────────┘   │  │
│                      └──────────────────────────────────────┘  │
│                                    │                            │
│                         ┌──────────▼──────────┐                │
│                         │     AYRenderer      │                │
│                         │   (使用骨骼变换)    │                │
│                         └─────────────────────┘                │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 核心概念

### 2.1 蒙皮（Skinning）

**蒙皮** = 将顶点绑定到骨骼，按权重跟随骨骼变形。

```
顶点数据结构：
struct Vertex {
    FVector3 position;
    FVector3 normal;
    FVector2 uv;
    
    // === 蒙皮数据 ===
    uint8_t boneIndices[4];   // 绑定的骨骼索引 (最多4根)
    float boneWeights[4]; // 权重 (总和=1)
};
```

**蒙皮计算公式**：
```
最终顶点 = Σ (boneWeight[i] × boneMatrix[i]) × 原始顶点

其中 boneMatrix[i] = boneWorldMatrix[i] × inverseBindMatrix[i]
```

### 2.2 两种蒙皮模式

| 模式 | 说明 | 适用场景 |
|------|------|----------|
| **CPU 蒙皮** | CPU 计算顶点变形，上传 GPU | 程序化动画、调试、需要读取变形数据 |
| **GPU 蒙皮** | 顶点着色器计算 | 大量骨骼、高性能需求 |

```
CPU 蒙皮流程：
骨骼动画数据 → 骨骼矩阵计算 → 顶点变形 → 上传 GPU

GPU 蒙皮流程：
骨骼矩阵数据 → 上传 GPU → 顶点着色器计算 → 直接输出
```

### 2.3 烘焙（Baking）

**常态**（选项 A）：保留绑定数据，运行时计算。

**烘焙**（选项 B）：预计算动画帧的顶点位置，运行时直接读取。

```
适用烘焙的场景：
- 完全固定的角色动画
- LOD 优化（低模烘焙）
- 不需要骨骼重定向的动画
```

---

## 3. 核心数据结构

### 3.1 Skeleton（骨骼）

```cpp
class Skeleton {
public:
    struct Bone {
        std::string name; // 骨骼名称
        int parentIndex;                // 父骨骼索引，-1=根
        FVector3 localPosition;        // Rest Pose 位置
        FQuaternion localRotation;     // Rest Pose 旋转
        FVector3 localScale;           // Rest Pose 缩放
        Float4x4 inverseBindMatrix;    // 逆绑定矩阵
    };
    
    struct IKChain {
        std::string name;
        int effectorBone;              // 末端骨骼
        int rootBone;                   // 根骨骼
        int poleVectorBone;             // 极向量控制骨骼
    };

    // 基础接口
    size_t getBoneCount() const;
    const Bone* getBones() const;
    int findBone(const char* name) const;
    
    // IK
    size_t getIKChainCount() const;
    const IKChain* getIKChains() const;
    
    // 骨骼变换
    void evaluate(const Animation* anim, float time);
    const std::vector<Float4x4>& getBoneMatrices() const;
    
private:
    std::vector<Bone> m_bones;
    std::vector<IKChain> m_ikChains;
    std::vector<Float4x4> m_boneMatrices;  // 计算结果
};
```

### 3.2 AnimationData（动画数据）

```cpp
// .ayanm 加载后的内存结构
struct AnimationData {
    std::string name; // "Walk", "Idle", "Run"
    float duration;                 // 总时长（秒）
    float ticksPerSecond;           // 时间精度
    
    //动画压缩信息
    struct CompressionInfo {
        bool isCompressed;
        float tolerance; // 压缩误差容限
    };
    CompressionInfo compression;
    
    struct Keyframe {
        float time;
        FVector3 translation;
        FQuaternion rotation;
        FVector3 scale;
    };
    
    struct BoneTrack {
        int boneIndex;              // 关联到 Skeleton 的骨骼索引
        
        // 自适应压缩：关键帧列表
        std::vector<Keyframe> translationKeys;
        std::vector<Keyframe> rotationKeys;
        std::vector<Keyframe> scaleKeys;
        
        // 压缩统计
        int originalKeyframeCount;
        int compressedKeyframeCount;
    };
    
    std::vector<BoneTrack> tracks;
};
```

### 3.3 BoneTransforms（骨骼变换结果）

```cpp
struct BoneTransform {
    FVector3 position;
    FQuaternion rotation;
    FVector3 scale;
};

struct BoneTransforms {
    std::vector<BoneTransform> transforms;  // 每根骨骼的变换
    
    // 蒙皮矩阵（boneMatrix = worldMatrix × inverseBindMatrix）
    std::vector<Float4x4> skinMatrices;
};
```

---

## 4. AnimationPlayer（动画播放器）

### 4.1 接口设计

```cpp
class AnimationPlayer {
public:
    AnimationPlayer();
    ~AnimationPlayer();
    
    // === 骨骼绑定 ===
    void setSkeleton(Skeleton* skeleton);
    Skeleton* getSkeleton() const { return m_skeleton; }
    
    // === 播放控制 ===
    void play(const char* animName, float blendTime = 0.0f);
    void stop();
    void pause();
    void resume();
    
    // === 时间控制 ===
    void setTime(float time);
    float getTime() const { return m_currentTime; }
    float getDuration() const { return m_currentAnimation ? m_currentAnimation->duration : 0.0f; }
    void setPlayRate(float rate) { m_playRate = rate; }
    
    // === 混合 ===
    void crossFadeTo(const char* animName, float duration);
    void blend(const char* animName, float weight);
    
    // === 骨骼变换结果 ===
    const std::vector<Float4x4>& getBoneMatrices() const {
        return m_skeleton->getBoneMatrices();
    }
    
    // === CPU 蒙皮结果 ===
    void evaluateSkinning(Mesh* mesh);
    const Mesh* getSkinnedMesh() const { return m_skinnedMesh.get(); }
    
    // === IK 控制 ===
    void setIKTarget(const char* chainName, const FVector3& position);
    void setIKEnabled(const char* chainName, bool enabled);
    void updateIK();
    
    // ===评估（每帧调用）===
    void evaluate(float deltaTime);

private:
    Skeleton* m_skeleton = nullptr;
    const AnimationData* m_currentAnimation = nullptr;
    
    float m_currentTime = 0.0f;
    float m_playRate = 1.0f;
    
    // 混合层
    struct BlendLayer {
        const AnimationData* anim;
        float weight;
        float time;
    };
    std::vector<BlendLayer> m_blendLayers;
    
    // IK
    std::unordered_map<std::string, FVector3> m_ikTargets;
    std::unordered_map<std::string, bool> m_ikEnabled;
    std::unique_ptr<IKSolver> m_ikSolver;
    
    // CPU 蒙皮结果
    std::unique_ptr<Mesh> m_skinnedMesh;
};
```

---

## 5. AnimationStateMachine（状态机）

### 5.1 L4 状态机架构

```
AnimationStateMachine
    │
    ├── RootStateMachine
    │   ├── States[]
    │   │   ├── Locomotion (子状态机)
    │   │   │   ├── Idle
    │   │   │   ├── Walk
    │   │   │   ├── Run
    │   │   │   └── BlendTree
    │   │   ├── Combat
    │   │   │   ├── Idle
    │   │   │   ├── Attack_Light
    │   │   │   └── Attack_Heavy
    │   │   └── Jump
    │   │
    │   ├── Transitions[]
    │   │   ├── Locomotion → Combat (onCombatStart)
    │   │   └── Combat → Locomotion (onCombatEnd)
    │   │
    │   └── BlendRules
    │
    └── BlendTrees
        ├── FullBodyBlend
        └── UpperBodyBlend
```

### 5.2 状态机接口

```cpp
class AnimationStateMachine {
public:
    // === 状态定义 ===
    struct State {
        std::string name;
        enum class Type { Simple, SubMachine, BlendTree };
        Type type;
        
        // 简单状态
        const AnimationData* animation = nullptr;
        
        // 子状态机引用
        AnimationStateMachine* subMachine = nullptr;
        
        // BlendTree 引用
        void* blendTree = nullptr;
    };
    
    struct Transition {
        std::string fromState;
        std::string toState;
        
        // 转换条件
        struct Condition {
            std::string parameter; // 触发参数
            enum class Op { Less, Greater, Equals, NotEquals };
            Op op;
            float threshold;
        };
        std::vector<Condition> conditions;
        
        float duration; // 过渡时长
        enum class BlendMode { Instant, CrossFade, Blend };
        BlendMode blendMode;
    };
    
    struct BlendTree {
        std::string name;
        std::vector<BlendNode> nodes;
    };
    
    struct BlendNode {
        enum class Type { Animation, Blend, Operator };
        Type type;
        
        std::string name;
        const AnimationData* animation = nullptr;
        
        // 混合输入
        std::vector<BlendNode*> inputs;
        
        // 混合参数
        float weight = 1.0f;
        std::vector<float> thresholds; // 用于 Blend1D/2D
    };

    // === 状态机控制 ===
    void setState(const std::string& stateName);
    void setParameter(const std::string& name, float value);
    void setBool(const std::string& name, bool value);
    
    // === 更新 ===
    void evaluate(float deltaTime, AnimationPlayer* player);
    
    // === 查询 ===
    const std::string& getCurrentStateName() const;
    float getParameter(const std::string& name) const;
};
```

### 5.3 BlendTree 节点类型

```cpp
// BlendTree 节点
struct BlendNode_Animation {
    const AnimationData* anim;
    float playRate = 1.0f;
};

struct BlendNode_Blend1D {
    std::vector<BlendNode*> inputs;
    std::vector<float> thresholds;  // [0.0, 0.5, 1.0]
    float currentThreshold; // 由外部参数驱动
};

struct BlendNode_Blend2D {
    std::vector<BlendNode*> inputs;
    FVector2 currentBlend;           // 2D 混合坐标
};

struct BlendNode_Additive {
    BlendNode* base;
    BlendNode* additive;
    float weight;
};

struct BlendNode_LayerMix {
    std::vector<BlendNode*> layers;
    std::vector<float> layerWeights;
    std::vector<FVector3> mask; // 骨骼遮罩
};
```

---

## 6. IKSolver（IK求解器）

### 6.1 支持的 IK 类型

| 类型 | 说明 | 适用场景 |
|------|------|----------|
| **TwoBoneSolver** | 2骨骼 IK（膝关节、肘关节） | 手臂、腿部 |
| **FABRIKSolver** | FABRIK 链式 IK | 手指、触须、多关节 |
| **CCDSolver** | CCD 循环坐标下降 | 通用链式 IK |

### 6.2 接口设计

```cpp
class IKSolver {
public:
    virtual ~IKSolver() = default;
    
    // 求解
    virtual void solve(
        std::vector<FVector3>& bonePositions,
        std::vector<FQuaternion>& boneRotations,
        const FVector3& target,
        const FVector3& poleVector = FVector3::zero()
    ) = 0;
    
    // 链式 IK工厂
    static std::unique_ptr<IKSolver> create(
        const FVector3* bonePositions,
        const float* boneLengths,
        int boneCount,
        Type type
    );
    
    enum class Type { TwoBone, FABRIK, CCD };
};

class TwoBoneSolver : public IKSolver {
public:
    void solve(
        std::vector<FVector3>& bonePositions,
        std::vector<FQuaternion>& boneRotations,
        const FVector3& target,
        const FVector3& poleVector = FVector3::zero()
    ) override;
    
    void setBoneLengths(float thigh, float calf) {
        m_thighLength = thigh;
        m_calfLength = calf;
    }

private:
    float m_thighLength;
    float m_calfLength;
};

class FABRIKSolver : public IKSolver {
public:
    void solve(
        std::vector<FVector3>& bonePositions,
        std::vector<FQuaternion>& boneRotations,
        const FVector3& target,
        const FVector3& poleVector = FVector3::zero()
    ) override;
    
    void setTolerance(float t) { m_tolerance = t; }
    void setMaxIterations(int n) { m_maxIterations = n; }

private:
    float m_tolerance = 0.01f;
    int m_maxIterations = 10;
};
```

### 6.3 物理约束

```cpp
struct IKConstraint {
    enum class Type { Distance, Angle, Rotation };
    Type type;
    
    // 约束参数
    float minDistance;
    float maxDistance;
    float minAngle;
    float maxAngle;
    FQuaternion minRotation;
    FQuaternion maxRotation;
};

class ConstrainedIKSolver : public IKSolver {
public:
    void addConstraint(const IKConstraint& constraint);
    
    void solve(
        std::vector<FVector3>& bonePositions,
        std::vector<FQuaternion>& boneRotations,
        const FVector3& target,
        const FVector3& poleVector = FVector3::zero()
    ) override;

private:
    std::vector<IKConstraint> m_constraints;
};
```

---

## 7. 骨骼重定向

### 7.1 离线重定向流程

```
打包阶段（Converter）：
    │
    ├── 加载源骨骼 (.ayskel)
    ├── 加载目标骨骼 (引擎标准骨骼)
    │
    ├── BoneMappingTable 自动/手动建立
    │   ├── "左足IK" → "Foot_L"
    │   └── "右足IK" → "Foot_R"
    │
    └── 写入 MappingCache
            │
            ▼
运行时：
    │
    ├── 加载 MappingCache
    │
    ├── AnimationPlayer 应用动画时
    │   └── BoneMappingTable.remap(boneIndex)
    │
    └── 输出到目标骨骼
```

### 7.2 BoneMappingTable

```cpp
class BoneMappingTable {
public:
    FGuid sourceSkeletonGuid;
    FGuid targetSkeletonGuid;
    
    struct Mapping {
        int sourceBoneIndex;
        int targetBoneIndex;
        float positionWeight;
        float rotationWeight;
    };
    
    std::vector<Mapping> mappings;
    
    // 快速查找
    int remap(int sourceBoneIndex) const;
    int remap(const std::string& sourceBoneName) const;
    
    // 批量重定向动画
    void retargetAnimation(
        const AnimationData* source,
        AnimationData* target,
        const Skeleton* targetSkeleton
    ) const;
};
```

---

## 8. 自适应压缩

### 8.1 压缩策略

```cpp
struct CompressionSettings {
    enum class Mode {
        None,          // 不压缩
        Fixed,         // 固定间隔
        Adaptive,      // 自适应（误差驱动）
        KeyframeReduction // 关键帧削减
    };
    Mode mode = Mode::Adaptive;
    
    float positionTolerance = 0.001f;  // 位置误差容限
    float rotationTolerance = 0.001f;   // 旋转误差容限（弧度）
    float scaleTolerance = 0.001f;     // 缩放误差容限
    
    int maxKeyframesPerTrack = 0;     // 0=无限制
};

class AnimationCompressor {
public:
    // 自适应压缩
    static std::unique_ptr<AnimationData> compress(
        const AnimationData* source,
        const CompressionSettings& settings
    );
    
    // 误差分析
    static float computeError(
        const AnimationData* original,
        const AnimationData* compressed
    );
    
    // 增量压缩（用于热重载）
    static std::unique_ptr<AnimationData> compressIncremental(
        const AnimationData* source,
        const CompressionSettings& settings,
        float maxMemoryBudget
    );
};
```

### 8.2 压缩算法

```cpp
// 自适应关键帧削减
class AdaptiveKeyframeReducer {
public:
    struct KeyframeReductionResult {
        std::vector<Keyframe> reducedKeys;
        int originalCount;
        int reducedCount;
        float maxError;
    };
    
    KeyframeReductionResult reduce(
        std::vector<Keyframe>& keys,
        float tolerance,
        float timeRange
    );
    
private:
    // 使用 Douglas-Peucker 算法变体
    KeyframeReductionResult douglasPeucker(
        std::vector<Keyframe>& keys,
        float tolerance
    );
};
```

---

## 9. CPU/GPU 蒙皮

### 9.1 CPU 蒙皮

```cpp
class CPUSkinning {
public:
    static void computeSkinning(
        const Skeleton* skeleton,
        const std::vector<Float4x4>& boneMatrices,
        Mesh* mesh,
        SkinningMode mode = SkinningMode::Linear
    );
    
    enum class SkinningMode {
        Linear,         // 线性混合
        DualQuaternion, // 双四元数（防止体积损失）
        Normalize // 归一化权重
    };
};
```

### 9.2 GPU 蒙皮

```cpp
// GPU 蒙皮顶点着色器（bgfx shader）
// 顶点着色器输入结构
struct VSInput_Skinned {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 boneIndices : BLENDINDICES;  // 骨骼索引
    float4 boneWeights : BLENDWEIGHT;   // 骨骼权重
};

// GPU 蒙皮计算
float4 skinnedPosition = 
    boneWeights[0] * mul(boneMatrices[boneIndices[0]], float4(position, 1.0)) +
    boneWeights[1] * mul(boneMatrices[boneIndices[1]], float4(position, 1.0)) +
    boneWeights[2] * mul(boneMatrices[boneIndices[2]], float4(position, 1.0)) +
    boneWeights[3] * mul(boneMatrices[boneIndices[3]], float4(position, 1.0));
```

### 9.3 蒙皮模式切换

```cpp
class SkinningFactory {
public:
    static SkinningMode selectMode(
        const Skeleton* skeleton,
        const AnimationData* anim,
        HardwareCapability hw
    ) {
        //移动端优先 GPU蒙皮（如果支持）
        if (hw.isMobile() && hw.supportsVertexShader()) {
            return SkinningMode::GPU;
        }
        
        // 需要读取变形数据时用 CPU
        if (anim->requiresDeformedPositions) {
            return SkinningMode::CPU;
        }
        
        // 默认 GPU
        return SkinningMode::GPU;
    }
};
```

---

## 10. 目录结构

```
AYAnimation/
├── design.md
├── CMakeLists.txt
├── include/
│   └── AYAnimation/
│       ├── AYAnimation.h              # 主入口
│       │
│       ├── Core/
│       │   ├── AnimationPlayer.h       # 动画播放器
│       │   ├── AnimationData.h        # 动画数据
│       │   ├── Skeleton.h             # 骨骼
│       │   └── BoneTransforms.h       # 骨骼变换结果
│       │
│       ├── StateMachine/
│       │   ├── AnimationStateMachine.h
│       │   ├── State.h
│       │   ├── Transition.h
│       │   └── BlendTree.h
│       │
│       ├── IK/
│       │   ├── IKSolver.h             # IK 基类
│       │   ├── TwoBoneSolver.h        # 2骨骼 IK
│       │   ├── FABRIKSolver.h          # FABRIK IK
│       │   ├── CCDSolver.h            # CCD IK
│       │   └── IKConstraint.h         # IK 约束
│       │
│       ├── Skinning/
│       │   ├── CPUSkinning.h          # CPU 蒙皮
│       │   ├── GPUSkinning.h          # GPU 蒙皮
│       │   └── SkinningFactory.h # 蒙皮模式选择
│       │
│       ├── Compression/
│       │   ├── AnimationCompressor.h # 压缩器
│       │   ├── AdaptiveKeyframeReducer.h
│       │   └── CompressionSettings.h
│       │
│       ├── Retargeting/
│       │   ├── BoneMappingTable.h # 骨骼映射表
│       │   └── AnimationRetargeter.h  # 动画重定向
│       │
│       └── Debug/
│           ├── AnimationDebugDraw.h  # 调试可视化
│           └── AnimationProfiler.h   # 性能分析
│
└── src/
    ├── AYAnimation.cpp
    ├── AnimationPlayer.cpp
    ├── AnimationData.cpp
    ├── Skeleton.cpp
    ├── AnimationStateMachine.cpp
    ├── State.cpp
    ├── Transition.cpp
    ├── BlendTree.cpp
    ├── IKSolver.cpp
    ├── TwoBoneSolver.cpp
    ├── FABRIKSolver.cpp
    ├── CCDSolver.cpp
    ├── CPUSkinning.cpp
    ├── GPUSkinning.cpp
    ├── AnimationCompressor.cpp
    ├── AdaptiveKeyframeReducer.cpp
    ├── BoneMappingTable.cpp
    └── AnimationRetargeter.cpp
```

---

## 11. 实现优先级

### Phase 1: 核心播放
- [ ] AnimationData 数据结构
- [ ] Skeleton 骨骼系统
- [ ] AnimationPlayer 基础播放
- [ ] 基础插值（Linear, Quaternion Slerp）
- [ ] CPU 蒙皮

### Phase 2: 混合系统
- [ ] CrossFade 过渡
- [ ] BlendTree (1D, 2D)
- [ ] AdditiveAnimation
- [ ] GPU 蒙皮

### Phase 3: 状态机
- [ ] L1简单状态机
- [ ] L2 条件转换
- [ ] L3 子状态机
- [ ] L4 MotionMatching 风格状态机

### Phase 4: IK + 高级
- [ ] TwoBoneSolver
- [ ] FABRIKSolver + CCDSolver
- [ ] IK 约束
- [ ] 骨骼重定向

### Phase 5: 优化
- [ ] 自适应压缩
- [ ] AnimationProfiler
- [ ] DebugVisualization
- [ ] LOD 动画系统

---

## 12. 参考

- Unreal Engine Animation System
- Unity Animancer
- O3DE Animation System
- Gameloft Animation System