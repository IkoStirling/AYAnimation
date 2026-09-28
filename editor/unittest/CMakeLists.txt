add_executable(AYAnimationEditorCore_UnitTests
    Test_AnimationAuthoring.cpp
    Test_AnimationTimeTransform.cpp
    Test_AnimationPreviewSession.cpp
    Test_SkeletonEditorCore.cpp
    Test_SkeletonBakePublication.cpp
)

target_link_libraries(AYAnimationEditorCore_UnitTests PRIVATE
    AYAnimationEditorCore
    AYTest
)

ay_add_test_tiers(AYAnimationEditorCore_UnitTests
    PREFIX animation-editor DEFAULT_TIER integration
    FAST_SUITES AnimationAuthoringTests AnimationClipboardTests
        AnimationTimeTransformTests AnimationTimeQuaternionTests
    STRESS_SUITES AnimationAuthoringStressTests
    TIMEOUT 120 RUN_SERIAL)
