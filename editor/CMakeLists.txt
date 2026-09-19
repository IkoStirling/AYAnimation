find_package(nlohmann_json CONFIG REQUIRED)

add_library(AYAnimationEditorCore STATIC
    src/SkeletonBakeJob.cpp
    src/SkeletonEditorCore.cpp
)

target_include_directories(AYAnimationEditorCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(AYAnimationEditorCore PUBLIC
    AYAnimation
    AYResource
    AYIO
    nlohmann_json::nlohmann_json
)

add_subdirectory(unittest)
