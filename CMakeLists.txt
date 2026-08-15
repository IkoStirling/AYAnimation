cmake_minimum_required(VERSION 3.12)
project(AYAnimation
    VERSION 0.1.0
    DESCRIPTION "AYAnimation — animation subsystem (BlendSpace 1D/2D + Shared Skeleton Tick Cache + AdditiveLayer)"
)

set(CMAKE_CXX_STANDARD 23)
set(MODULE_NAME AYAnimation)

# P0 (2026-07-26): Skeleton.cpp / Animation.cpp deleted — AnimationPlayer
# now consumes ayt::resource::ISkeleton + ayt::resource::IAnimation directly.
set(SRC_FILES
    src/AnimationPlayer.cpp
    src/KeySampler.cpp
    # P1.7 (2026-07-27) — asset-level shared (ISkeleton*, name) → boneIdx
    # cache consumed by AnimationPlayer::resolveBoneIdxOnce.
    src/AssetBoneCache.cpp
    # P2.1 (2026-07-27) — Blend 1D / 2D linear-simplex BlendSpace
    # independent of AnimationPlayer; owns N sample AnimationPlayers.
    src/BlendSpace.cpp
    # P3.1 (2026-08-06) — L1 简单状态机 engine. Standalone class;
    # AnimationSystem bridges to AnimationPlayer.
    src/StateMachine.cpp
    # P3.x (2026-08-07) — L2 Condition DSL mini lexer + precedence-climbing
    # parser. Independent of AYShader / AYScript / AYGraph (borrow precedence
    # pattern only, no link). Consumed by Transition::evaluateCondition.
    src/ConditionParser.cpp
    # P2 polish (2026-08-07) — Flat opcode bytecode evaluator (program-
    # counter switch over CondOpByte opcodes). Consumes the bytecode cache
    # compiled from the AST by ConditionParser.cpp.
    src/CondBytecode.cpp
    # P4-1 (2026-08-10) — Two-Bone IK analytic solver. Pure math core
    # (world-space positions/rotations in, world rotations out); consumed
    # by the AnimationPlayer::evaluate Phase 2.5 IK pass.
    src/TwoBoneSolver.cpp
    # P4-2 (2026-08-11) — FABRIK + CCD iterative IK solvers (multi-joint
    # chains, root→tip path auto-derivation). Pure math cores sharing
    # IterativeIKResult + kMaxIKChainBones; dispatched per chain by
    # IKChainSpec::type in the Phase 2.5 unified pass.
    src/FabrikSolver.cpp
    src/CcdSolver.cpp
)

add_library(${MODULE_NAME} STATIC ${SRC_FILES})

target_include_directories(${MODULE_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/AYAnimation>
)

# P0 (2026-07-26): link AYResource so AnimationPlayer can read
# IAnimation::getTrackTimes/Values, ISkeleton::getLocalPositions/Rotations/Scales,
# and getInverseBindMatrices() directly.
target_link_libraries(${MODULE_NAME} PUBLIC AYMath AYResource)

enable_testing()
add_subdirectory(unittest)

# P0 polish (2026-08-07) — micro-benchmark for params/triggers lookup
# performance. Default OFF; build with -DAY_BUILD_BENCHMARKS=ON to
# verify pre/post-refactor speedup numbers. NOT in PR-gate.
option(AY_BUILD_BENCHMARKS "Build AYAnimation micro-benchmarks" OFF)
if(AY_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()

message(STATUS "AYAnimation v0.1.0 (P2.1 BlendSpace ship + P1.7 final pieces) — see ${CMAKE_CURRENT_SOURCE_DIR}/design.md")
