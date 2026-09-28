add_executable(AYAnimationEditorCore_UnitTests
    Test_AnimationPreviewSession.cpp
    Test_SkeletonEditorCore.cpp
)

target_link_libraries(AYAnimationEditorCore_UnitTests PRIVATE
    AYAnimationEditorCore
    AYTest
)

ay_add_test(AYAnimationEditorCore_UnitTests
    LABELS "animation-editor;animation-editor-full;animation-editor-integration" TIMEOUT 120 RUN_SERIAL)
