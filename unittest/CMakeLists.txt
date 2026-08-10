set(MODULE_NAME AYAnimation_UnitTests)

set(SRC_FILES
    main.cpp
    AYTest_KeySampler.cpp
    AYTest_AnimationPlayer.cpp
    # P2.1 (2026-07-27) — BlendSpace1D / BlendSpace2D unit tests
    # (own-engine math + library-mode helper, no World/Entity).
    AYTest_BlendSpace.cpp
    # P2.2 (2026-08-03) — SkeletonMask unit tests (resource-level bone
    # mask; INV-12..17 + 16 cases; in-memory fixture SkeletonMask).
    AYTest_SkeletonMask.cpp
    # P3.1 (2026-08-06) — L1 简单状态机 unit tests. 15 cases pinning
    # INV-18..24; standalone (no AYEntity / no AnimationPlayer).
    AYTest_StateMachine.cpp
    # P3.2 (2026-08-06) — L3 子状态机 unit tests. 12 cases pinning
    # INV-27..31; standalone (no AYEntity / no AnimationPlayer).
    AYTest_SubStateMachine.cpp
    # P3.x (2026-08-07) — L2 Condition DSL unit tests. 30 cases pinning
    # INV-32..35 (parser + evaluator + cache + back-compat); standalone.
    AYTest_ConditionExpr.cpp
    # P3.x刀 N+1.C (2026-08-07) — Per-state AnimNotify routing unit tests.
    # 4 cases pinning INV-40..42 (fromStateName on AnimNotifyRecord +
    # AnimNotifyEvent, default-empty back-compat, merged queue dedup);
    # standalone (no AYEntity).
    AYTest_AnimNotifyRouting.cpp
    # P4 polish (2026-08-10) — batch-tick stress + bind/clear cycles +
    # AdditiveSlot memory release (INV-61/62) + AssetBoneCache
    # heterogeneous lookup (INV-63). standalone (no AYEntity).
    AYTest_P4Stress.cpp
    # P4-1 (2026-08-10) — TwoBone IK. S1-S10 solver math (standalone,
    # INV-74) + P1-P12 AnimationPlayer Phase 2.5 integration
    # (INV-71/72/73, 3-bone fixture).
    AYTest_TwoBoneIK.cpp
)

add_executable(${MODULE_NAME} ${SRC_FILES})

# Phase 1.5: AYEventSystem dependency for eventbus_animnotify_event_pod_carries_entity_id.
# AnimationPlayer itself does NOT link AYEventSystem �?only this test does.
target_link_libraries(${MODULE_NAME} PRIVATE AYAnimation AYTest AYEventSystem)
target_include_directories(${MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

ay_configure_test_tmpdir(${MODULE_NAME})
add_test(NAME ${MODULE_NAME} COMMAND ${MODULE_NAME})