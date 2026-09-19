add_executable(AYAnimationEditorCore_UnitTests
    Test_SkeletonEditorCore.cpp
)

target_link_libraries(AYAnimationEditorCore_UnitTests PRIVATE
    AYAnimationEditorCore
    AYTest
)

ay_add_test(AYAnimationEditorCore_UnitTests)
