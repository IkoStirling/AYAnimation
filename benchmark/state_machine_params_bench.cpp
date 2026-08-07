// state_machine_params_bench.cpp — P0 Polish (2026-08-07) +
//                                     P1 Polish (2026-08-07) Micro-Benchmark.
//
// Bench is API-stable: it exercises the public setParam/getParam/setTrigger
// surface of StateMachine. Pre-refactor (unordered_map<string,float>) and
// post-refactor (vector<ParamEntry> + ParamNameRegistry) ship the same
// public API, so the bench source compiles unchanged on both sides.
//
// P1 polish adds Scenarios E (findEligibleTransition hot path) and F
// (Condition DSL evaluate hot path). Both scenarios measure the savings
// from eliminating per-frame ParamNameRegistry::intern() calls; pre-P1
// polish these were 100% of the hot-path cost (setParam/getParam were
// already optimized in P0 polish).
//
// Output: nanoseconds per iteration + total time. Numbers are compared
// pre vs post refactor to validate the P0 polish claim (≥2x speedup).
//
// Build (local only, NOT in PR-gate):
//   cmake -DAY_BUILD_BENCHMARKS=ON -S D:\Projects -B D:\Projects\out\build\x64-Debug
//   cmake --build D:\Projects\out\build\x64-Debug --target AYAnimation_Benchmarks -j 8
//   ./bin/Debug/AYAnimation/benchmark/AYAnimation_Benchmarks

#include <ayanimation/ConditionExpr.h>
#include <ayanimation/StateMachine.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using ayt::anim::StateMachine;
using ayt::anim::State;
using ayt::anim::Transition;
using ayt::anim::ConditionEvalCtx;
using ayt::anim::ParamEntry;
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

// Scenario E — P1 polish: findEligibleTransition hot path.
// 5 transitions, each with a trigger + L1 condition. Per update, the
// StateMachine walks all 5 transitions and runs intern() for each
// non-empty trigger + each non-empty L1 condition. P1 polish replaces
// those per-frame intern() calls with cached hashes from addTransition.
void scenarioE_findEligibleTransition() {
    std::printf("=== Scenario E: findEligibleTransition 5 transitions × 100K iters ===\n");

    // Build SM with 6 states, 5 transitions. Each transition has a unique
    // trigger + unique L1 condition.
    StateMachine sm;
    State sA; sA.name = "Idle";     sA.clipPath = "i.ayanm"; sm.addState(sA);
    State sB; sB.name = "Run";      sB.clipPath = "r.ayanm"; sm.addState(sB);
    State sC; sC.name = "Jump";     sC.clipPath = "j.ayanm"; sm.addState(sC);
    State sD; sD.name = "Fall";     sD.clipPath = "f.ayanm"; sm.addState(sD);
    State sE; sE.name = "Land";     sE.clipPath = "l.ayanm"; sm.addState(sE);
    State sF; sF.name = "Crouch";   sF.clipPath = "c.ayanm"; sm.addState(sF);
    sm.setInitialState("Idle");

    struct Pair {
        std::string from;
        std::string to;
        std::string trigger;
        std::string condParam;
    };
    const Pair pairs[] = {
        {"Idle",   "Run",    "Run",    "Speed"},
        {"Run",    "Jump",   "Jump",   "IsGrounded"},
        {"Jump",   "Fall",   "Fall",   "VerticalSpeed"},
        {"Fall",   "Land",   "Land",   "TimeInAir"},
        {"Land",   "Crouch", "Crouch", "Stamina"},
    };
    for (const auto& p : pairs) {
        Transition t;
        t.trigger   = p.trigger;
        t.fromState = p.from;
        t.toState   = p.to;
        t.hasCondition = true;
        t.condition.paramName = p.condParam;
        t.condition.op        = ayt::anim::StateConditionOp::Greater;
        t.condition.compareValue = 0.0f;
        sm.addTransition(t);
    }

    // Prime params (so the condition lookup succeeds).
    sm.setParam("Speed", 5.0f);
    sm.setParam("IsGrounded", 1.0f);
    sm.setParam("VerticalSpeed", 0.0f);
    sm.setParam("TimeInAir", 0.0f);
    sm.setParam("Stamina", 100.0f);

    // The actual hot path: sm.update(dt) calls findEligibleTransition
    // (5× per frame), which evaluates triggerHash + conditionParamNameHash
    // + condition. With the trigger not set, none fire, so the path is
    // a clean scan of 5 transitions.
    const std::size_t N = 100000;
    auto t0 = clock_type::now();
    for (std::size_t i = 0; i < N; ++i) {
        sm.update(0.0f);   // 5 transitions walked, none fire (no trigger set)
    }
    auto t1 = clock_type::now();
    const double scanNs = std::chrono::duration<double, std::nano>(t1 - t0).count();
    std::printf("  scan 5 transitions (no trigger): %.2f ns/iter (%.2f ms total)\n",
                scanNs / static_cast<double>(N), scanNs / 1.0e6);

    // Now with a trigger set, findEligibleTransition must run the full
    // condition eval on each matching transition.
    sm.setTrigger("Run");
    auto t2 = clock_type::now();
    for (std::size_t i = 0; i < N; ++i) {
        sm.update(0.0f);   // Idle→Run fires; trigger auto-erases
        sm.setTrigger("Run");  // re-arm
    }
    auto t3 = clock_type::now();
    const double fireNs = std::chrono::duration<double, std::nano>(t3 - t2).count();
    std::printf("  scan + fire 1 transition (trigger set): %.2f ns/iter (%.2f ms total)\n",
                fireNs / static_cast<double>(N), fireNs / 1.0e6);
}

// Scenario F — P1 polish: Condition DSL evaluator hot path.
// Build a small AST manually (no parser cost) with 3 identifiers.
// evaluate() walks the AST; each CondIdentifierExpr::evaluateAsFloat
// uses cached nameHash (was: per-eval intern() before P1 polish).
void scenarioF_dslEvaluate() {
    std::printf("=== Scenario F: Condition DSL evaluate (3 identifiers) × 100K iters ===\n");

    // Build AST: (Speed > 5.0) && IsGrounded && !IsDead
    //   Speed > 5.0   → CondBinaryExpr(GT, CondIdentifierExpr("Speed"),
    //                                     CondLiteralExpr(5.0f))
    //   && IsGrounded → CondBinaryExpr(And, ..., CondIdentifierExpr("IsGrounded"))
    //   && !IsDead    → CondBinaryExpr(And, ..., CondUnaryExpr(Not,
    //                                     CondIdentifierExpr("IsDead")))
    auto speedGt5 = std::make_unique<ayt::anim::CondBinaryExpr>(
        std::make_unique<ayt::anim::CondIdentifierExpr>(std::string("Speed")),
        ayt::anim::CondOp::GT,
        std::make_unique<ayt::anim::CondLiteralExpr>(5.0f));
    auto speedGt5AndGrounded = std::make_unique<ayt::anim::CondBinaryExpr>(
        std::move(speedGt5),
        ayt::anim::CondOp::And,
        std::make_unique<ayt::anim::CondIdentifierExpr>(std::string("IsGrounded")));
    auto notDead = std::make_unique<ayt::anim::CondUnaryExpr>(
        ayt::anim::CondOp::Not,
        std::make_unique<ayt::anim::CondIdentifierExpr>(std::string("IsDead")));
    auto full = std::make_unique<ayt::anim::CondBinaryExpr>(
        std::move(speedGt5AndGrounded),
        ayt::anim::CondOp::And,
        std::move(notDead));

    // Build ConditionEvalCtx: Speed=7, IsGrounded=1, IsDead=0 (true).
    std::vector<ParamEntry> paramVec;
    paramVec.push_back({ ayt::anim::detail::ParamNameRegistry::instance().intern("Speed"), 7.0f });
    paramVec.push_back({ ayt::anim::detail::ParamNameRegistry::instance().intern("IsGrounded"), 1.0f });
    paramVec.push_back({ ayt::anim::detail::ParamNameRegistry::instance().intern("IsDead"), 0.0f });
    ConditionEvalCtx ctx;
    ctx.params = &paramVec;
    ctx.triggers = nullptr;
    ctx.currentState = "Idle";
    ctx.currentStateTime = 0.0f;

    const std::size_t N = 100000;
    volatile bool sink = false;
    auto t0 = clock_type::now();
    for (std::size_t i = 0; i < N; ++i) {
        sink = full->evaluate(ctx);
    }
    auto t1 = clock_type::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    std::printf("  evaluate 3-ident expr (true path): %.2f ns/iter (%.2f ms total)\n",
                ns / static_cast<double>(N), ns / 1.0e6);
    std::printf("  sink = %d (volatile; do not optimize)\n", static_cast<int>(sink));

    // Verify short-circuit: IsDead=1.0 → false (no further eval).
    paramVec[2].value = 1.0f;
    sink = false;
    auto t2 = clock_type::now();
    for (std::size_t i = 0; i < N; ++i) {
        sink = full->evaluate(ctx);
    }
    auto t3 = clock_type::now();
    const double nsShort = std::chrono::duration<double, std::nano>(t3 - t2).count();
    std::printf("  evaluate 3-ident expr (short-circuit on IsDead): %.2f ns/iter (%.2f ms total)\n",
                nsShort / static_cast<double>(N), nsShort / 1.0e6);
    std::printf("  sink = %d\n", static_cast<int>(sink));
}

} // namespace

int main() {
    std::printf("=== P0 Polish + P1 Polish Micro-Benchmark ===\n");
    std::printf("=== StateMachine param/trigger lookup performance ===\n");
    std::printf("=== date: 2026-08-07 ===\n\n");

    scenarioA_8params();
    scenarioB_1param();
    scenarioC_32params();
    scenarioD_triggers();
    scenarioE_findEligibleTransition();
    scenarioF_dslEvaluate();

    std::printf("\n=== done ===\n");
    return 0;
}
