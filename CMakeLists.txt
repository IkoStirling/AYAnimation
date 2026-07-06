cmake_minimum_required(VERSION 3.12)
project(AYAnimation)

set(CMAKE_CXX_STANDARD 23)
set(MODULE_NAME AYAnimation)

# AN-01: Skeleton / Animation / AnimationPlayer / KeySampler — all in src/.
set(SRC_FILES
    src/Skeleton.cpp
    src/Animation.cpp
    src/AnimationPlayer.cpp
    src/KeySampler.cpp
)

add_library(${MODULE_NAME} STATIC ${SRC_FILES})

target_include_directories(${MODULE_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/ayanimation>
)

# AN-01 only needs Vec/Quat/Lerp/Slerp primitives. AYResource loader wiring is
# deferred to the next PR to keep this static lib green from day one.
target_link_libraries(${MODULE_NAME} PUBLIC AYMath)

enable_testing()
add_subdirectory(unittest)