cmake_minimum_required(VERSION 3.12)
project(AYAnimation)

set(CMAKE_CXX_STANDARD 23)
set(MODULE_NAME AYAnimation)

# P0 (2026-07-26): Skeleton.cpp / Animation.cpp deleted — AnimationPlayer
# now consumes ayt::resource::ISkeleton + ayt::resource::IAnimation directly.
set(SRC_FILES
    src/AnimationPlayer.cpp
    src/KeySampler.cpp
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