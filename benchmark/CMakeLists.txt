# benchmark/CMakeLists.txt — P0 Polish micro-benchmark (2026-08-07)
#
# Standalone binary, NOT a unit test. Built only when
#   -DAY_BUILD_BENCHMARKS=ON
# (default OFF). Used to validate pre/post polish speedup claims.

set(MODULE_NAME AYAnimation_Benchmarks)

add_executable(${MODULE_NAME} state_machine_params_bench.cpp)

target_link_libraries(${MODULE_NAME} PRIVATE AYAnimation)
target_include_directories(${MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# No test registration. Run manually:
#   ./bin/Debug/AYAnimation/benchmark/AYAnimation_Benchmarks
