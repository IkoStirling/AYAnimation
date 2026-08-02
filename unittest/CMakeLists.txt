set(MODULE_NAME AYAnimation_UnitTests)

set(SRC_FILES
    main.cpp
    AYTest_KeySampler.cpp
    AYTest_AnimationPlayer.cpp
    # P2.1 (2026-07-27) â€?BlendSpace1D / BlendSpace2D unit tests
    # (own-engine math + library-mode helper, no World/Entity).
    AYTest_BlendSpace.cpp
)

add_executable(${MODULE_NAME} ${SRC_FILES})

# Phase 1.5: AYEventSystem dependency for eventbus_animnotify_event_pod_carries_entity_id.
# AnimationPlayer itself does NOT link AYEventSystem â€?only this test does.
target_link_libraries(${MODULE_NAME} PRIVATE AYAnimation AYTest AYEventSystem)
target_include_directories(${MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

ay_configure_test_tmpdir(${MODULE_NAME})
add_test(NAME ${MODULE_NAME} COMMAND ${MODULE_NAME})