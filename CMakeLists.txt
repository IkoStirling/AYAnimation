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
)

add_library(${MODULE_NAME} STATIC ${SRC_FILES})

target_include_directories(${MODULE_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/ayanimation>
)

# P0 (2026-07-26): link AYResource so AnimationPlayer can read
# IAnimation::getTrackTimes/Values, ISkeleton::getLocalPositions/Rotations/Scales,
# and getInverseBindMatrices() directly.
target_link_libraries(${MODULE_NAME} PUBLIC AYMath AYResource)

enable_testing()
add_subdirectory(unittest)

message(STATUS "AYAnimation v0.1.0 (P2.1 BlendSpace ship + P1.7 final pieces) — see ${CMAKE_CURRENT_SOURCE_DIR}/design.md")