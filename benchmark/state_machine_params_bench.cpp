// state_machine_params_bench.cpp — P0 Polish Micro-Benchmark (2026-08-07)
//
// Bench is API-stable: it exercises the public setParam/getParam/setTrigger
// surface of StateMachine. Pre-refactor (unordered_map<string,float>) and
// post-refactor (vector<ParamEntry> + ParamNameRegistry) ship the same
// public API, so the bench source compiles unchanged on both sides.
//
// We capture two snapshots per scenario:
//   1. setParam 100K iterations (write path)
//   2. getParam 100K iterations (read path — the hot path)
//
// Output: nanoseconds per iteration + total time. Numbers are compared
// pre vs post refactor to validate the P0 polish claim (≥2x speedup).
//
// Build (local only, NOT in PR-gate):
//   cmake -DAY_BUILD_BENCHMARKS=ON -S D:\Projects -B D:\Projects\out\build\x64-Debug
//   cmake --build D:\Projects\out\build\x64-Debug --target AYAnimation_Benchmarks -j 8
//   ./bin/Debug/AYAnimation/benchmark/AYAnimation_Benchmarks

#include <ayanimation/StateMachine.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using ayt::anim::StateMachine;
using ayt::anim::State;
using clock_type = std::chrono::steady_clock;

namespace {

// Build an attention-limited parameter set: 8 unique names — typical
// game character (Speed, IsGrounded, VerticalSpeed, IsAttacking, Direction,
// AnimationSpeed, BlendAlpha, IsCrouching).
std::vector<std::string> makeParamNames() {
    return {
        "Speed", "IsGrounded", "VerticalSpeed", "IsAttacking",
        "Direction", "AnimationSpeed", "BlendAlpha", "IsCrouching",
    };
}

void scenarioA_8params() {
    std::printf("=== Scenario A: 8 params, 100K iterations each ===\n");
    StateMachine sm;
    State s; s.name = "Idle"; s.clipPath = "idle.ayanm";
    sm.addState(s);
    sm.setInitialState("Idle");

    const auto names = makeParamNames();
    const std::size_t N = 100000;

    // Warm-up: prime the registry so any first-use cost doesn't pollute.
    for (const auto& n : names) sm.setParam(n, 0.0f);

    // setParam path (write).
    auto t0 = clock_type::now();
    for (std::size_t i = 0; i < N; ++i) {
        for (const auto& n : names) {
            sm.setParam(n, static_cast<float>(i & 0xFF));
        }
    }
    auto t1 = clock_type::now();
    const double setNs = std::chrono::duration<double, std::nano>(t1 - t0).count();
    std::printf("  setParam:    %.2f ns/iter (%.2f ms total, %zu params × %zu iter)\n",
                setNs / static_cast<double>(N * names.size()),
                setNs / 1.0e6,
                names.size(), N);

    // getParam path (read — the hot path).
    auto t2 = clock_type::now();
    volatile float sink = 0.0f;
    for (std::size_t i = 0; i < N; ++i) {
        for (const auto& n : names) {
            sink += sm.getParam(n);
        }
    }
    auto t3 = clock_type::now();
    const double getNs = std::chrono::duration<double, std::nano>(t3 - t2).count();
    std::printf("  getParam:    %.2f ns/iter (%.2f ms total, %zu params × %zu iter)\n",
                getNs / static_cast<double>(N * names.size()),
                getNs / 1.0e6,
                names.size(), N);
    std::printf("  sink         = %.6f (volatile; do not optimize)\n", sink);
}

void scenarioB_1param() {
    std::printf("=== Scenario B: 1 param, 100K iterations ===\n");
    StateMachine sm;
    State s; s.name = "Idle"; s.clipPath = "idle.ayanm";
    sm.addState(s);
    sm.setInitialState("Idle");

    const std::string n = "Speed";
    const std::size_t N = 100000;

    sm.setParam(n, 0.0f);

    auto t0 = clock_type::now();
    for (std::size_t i = 0; i < N; ++i) sm.setParam(n, static_cast<float>(i));
    auto t1 = clock_type::now();
    const double setNs = std::chrono::duration<double, std::nano>(t1 - t0).count();
    std::printf("  setParam:    %.2f ns/iter (%.2f ms total)\n",
                setNs / static_cast<double>(N), setNs / 1.0e6);

    auto t2 = clock_type::now();
    volatile float sink = 0.0f;
    for (std::size_t i = 0; i < N; ++i) sink += sm.getParam(n);
    auto t3 = clock_type::now();
    const double getNs = std::chrono::duration<double, std::nano>(t3 - t2).count();
    std::printf("  getParam:    %.2f ns/iter (%.2f ms total)\n",
                getNs / static_cast<double>(N), getNs / 1.0e6);
    std::printf("  sink         = %.6f\n", sink);
}

void scenarioC_32params() {
    std::printf("=== Scenario C: 32 params, 100K iterations ===\n");
    StateMachine sm;
    State s; s.name = "Idle"; s.clipPath = "idle.ayanm";
    sm.addState(s);
    sm.setInitialState("Idle");

    // 32 unique names — data-heavy NPC AI.
    std::vector<std::string> names;
    for (int i = 0; i < 32; ++i) names.push_back("NPCParam_" + std::to_string(i));
    const std::size_t N = 100000;

    for (const auto& n : names) sm.setParam(n, 0.0f);

    auto t0 = clock_type::now();
    for (std::size_t i = 0; i < N; ++i) {
        for (const auto& n : names) sm.setParam(n, static_cast<float>(i & 0xFF));
    }
    auto t1 = clock_type::now();
    const double setNs = std::chrono::duration<double, std::nano>(t1 - t0).count();
    std::printf("  setParam:    %.2f ns/iter (%.2f ms total, %zu params × %zu iter)\n",
                setNs / static_cast<double>(N * names.size()),
                setNs / 1.0e6, names.size(), N);

    auto t2 = clock_type::now();
    volatile float sink = 0.0f;
    for (std::size_t i = 0; i < N; ++i) {
        for (const auto& n : names) sink += sm.getParam(n);
    }
    auto t3 = clock_type::now();
    const double getNs = std::chrono::duration<double, std::nano>(t3 - t2).count();
    std::printf("  getParam:    %.2f ns/iter (%.2f ms total, %zu params × %zu iter)\n",
                getNs / static_cast<double>(N * names.size()),
                getNs / 1.0e6, names.size(), N);
    std::printf("  sink         = %.6f\n", sink);
}

void scenarioD_triggers() {
    std::printf("=== Scenario D: triggers set/has/erase, 100K iterations ===\n");
    StateMachine sm;
    State s; s.name = "Idle"; s.clipPath = "idle.ayanm";
    sm.addState(s);
    sm.setInitialState("Idle");

    const std::string trig = "Go";
    const std::size_t N = 100000;

    // setTrigger path (write).
    auto t0 = clock_type::now();
    for (std::size_t i = 0; i < N; ++i) sm.setTrigger(trig);
    auto t1 = clock_type::now();
    const double setNs = std::chrono::duration<double, std::nano>(t1 - t0).count();
    std::printf("  setTrigger:  %.2f ns/iter (%.2f ms total)\n",
                setNs / static_cast<double>(N), setNs / 1.0e6);

    // The setTrigger / findEligibleTransition cycle is the real hot path.
    // We can't directly call hasTriggerHash (private), so we approximate
    // by alternating setTrigger + update, which triggers _triggers.count
    // inside findEligibleTransition.
    auto t2 = clock_type::now();
    for (std::size_t i = 0; i < N; ++i) {
        sm.setTrigger(trig);
        sm.update(0.0f);  // triggers count() iteration
        sm.setTrigger(trig);  // re-arm
    }
    auto t3 = clock_type::now();
    const double cycleNs = std::chrono::duration<double, std::nano>(t3 - t2).count();
    std::printf("  trigger+update cycle: %.2f ns/iter (%.2f ms total)\n",
                cycleNs / static_cast<double>(N), cycleNs / 1.0e6);
}

} // namespace

int main() {
    std::printf("=== P0 Polish Micro-Benchmark ===\n");
    std::printf("=== StateMachine param/trigger lookup performance ===\n");
    std::printf("=== date: 2026-08-07 ===\n\n");

    scenarioA_8params();
    scenarioB_1param();
    scenarioC_32params();
    scenarioD_triggers();

    std::printf("\n=== done ===\n");
    return 0;
}
