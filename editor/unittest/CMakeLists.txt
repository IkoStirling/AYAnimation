add_executable(AYAnimationEditorCore_UnitTests
    Test_AnimationPreviewSession.cpp
    Test_SkeletonEditorCore.cpp
    Test_SkeletonBakePublication.cpp
)

target_link_libraries(AYAnimationEditorCore_UnitTests PRIVATE
    AYAnimationEditorCore
    AYTest
)

ay_add_test(AYAnimationEditorCore_UnitTests CASE_REPORT
    LABELS "animation-editor;animation-editor-full;animation-editor-integration" TIMEOUT 120 RUN_SERIAL)
