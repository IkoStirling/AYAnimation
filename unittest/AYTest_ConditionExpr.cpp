// AYTest_ConditionExpr.cpp — P3.x (2026-08-07) L2 Condition DSL unit tests
//                            + P3.x刀 N+1.B (2026-08-07) Time-in-State Query.
//
// 30+6 cases pinning INV-32..35 + INV-36..39 contracts from design.md §4.16..4.17:
//   INV-32 — empty conditionExpr means no condition (always true)
//   INV-33 — parse failure ⇒ cachedAst=null + conditionParseError non-empty
//            + stderr one-liner; transition evaluates false (no crash, no assert)
//   INV-34 — setConditionExpr auto-flags conditionDirty; old cachedAst stays
//            alive until next evaluate (RAII swap)
//   INV-35 — Cache semantics: conditionDirty=true ⇒ re-parse; dirty=false ⇒
//            use cachedAst; cachedAst=null + dirty=false ⇒ permanently false
//   INV-36 — getCurrentStateElapsedTime() returns 0.0f when _initialized=false
//   INV-37 — fireTransition (instant-cut AND cross-fade START) reset
//            _currentStateEnterTime to 0.0f
//   INV-38 — _currentStateEnterTime accumulates dt in update() top-of-frame
//            (even during cross-fade window, matches UE semantics)
//   INV-39 — reserved ident "CurrentStateTime" in CondIdentifierExpr routes
//            to ctx.currentStateTime (SHADOWS any user param of same name)
//
// Layout:
//   §8.1.1 Parser unit (8 cases)
//   §8.1.2 Evaluator unit (12 cases)
//   §8.1.3 Cache / invalidate unit (6 cases)
//   §8.1.4 Backward-compat (L1 zero regression) (4 cases)
//   §8.1.5 Time-in-State Query (6 cases, NEW P3.x刀 N+1.B)
//
// Standalone — no AYEntity, no AnimationPlayer. Direct API calls.

#include <AYAnimation.h>
#include <AYTest.h>

#include <ayanimation/ConditionExpr.h>
#include <ayanimation/ConditionParser.h>
#include <ayanimation/StateMachine.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace ayt::anim;

namespace
{

// Make a 2-state SM (Idle ↔ Run) with a single trigger-driven transition
// that the caller can override with `conditionExpr`.
StateMachine makeIdleRunSM()
{
    StateMachine sm;
    State idle; idle.name = "Idle"; idle.clipPath = "idle.ayanm";
    sm.addState(idle);
    State run;  run.name  = "Run";  run.clipPath  = "run.ayanm";
    sm.addState(run);

    Transition t;
    t.trigger   = "Go";
    t.fromState = "Idle";
    t.toState   = "Run";
    sm.addTransition(t);

    sm.setInitialState("Idle");
    return sm;
}

ConditionEvalCtx makeCtx(std::initializer_list<std::pair<std::string, float>> params)
{
    // P0 polish (2026-08-07) — adapt to flat-vector containers.
    // The signature is unchanged (still takes string→float pairs), but
    // internals switch from unordered_map<string,float> to
    // vector<ParamEntry> with FNV-1a hash via ParamNameRegistry.
    static std::vector<ParamEntry> paramVec;
    paramVec.clear();
    for (auto& p : params) {
        paramVec.push_back({
            ayt::anim::detail::ParamNameRegistry::instance().intern(p.first),
            p.second,
        });
    }
    static std::vector<uint32_t> trigVec;
    trigVec.clear();
    ConditionEvalCtx ctx;
    ctx.params = &paramVec;
    ctx.triggers = &trigVec;
    return ctx;
}

} // namespace

TEST_SUITE(ConditionExprTests)

// =====================================================================
// §8.1.5 Time-in-State Query (6 cases, NEW P3.x刀 N+1.B)
// =====================================================================
//
// P3.x刀 N+1.B — reserved identifier "CurrentStateTime" routes to the
// live state-machine time-in-state value. Mirrors UE's
// FAnimNode_StateMachine::GetCurrentStateElapsedTime semantics:
//   * 0.0f before any update tick
//   * accumulates dt in update() top-of-frame (even during cross-fade)
//   * reset to 0 on transition (instant-cut AND cross-fade START)

TEST_CASE(TIS_InitialState_ElapsedTimeZero) {
    // INV-36 — setInitialState without any update tick returns 0.0f.
    StateMachine sm;
    State s; s.name = "Idle"; s.clipPath = "idle.ayanm";
    sm.addState(s);
    sm.setInitialState("Idle");
    CHECK(sm.getCurrentStateElapsedTime() == 0.0f);
}

TEST_CASE(TIS_AfterUpdate_ElapsedTimeAccumulates) {
    // INV-38 — update(dt) accumulates dt into the elapsed clock.
    StateMachine sm;
    State s; s.name = "Idle"; s.clipPath = "idle.ayanm";
    sm.addState(s);
    sm.setInitialState("Idle");
    sm.update(0.5f);
    // Floating-point accumulation — exact value should hold for short windows.
    CHECK(sm.getCurrentStateElapsedTime() == 0.5f);
    sm.update(0.25f);
    CHECK(sm.getCurrentStateElapsedTime() == 0.75f);
}

TEST_CASE(TIS_AfterTransition_ResetsToZero) {
    // INV-37 — fireTransition instant-cut resets the clock to 0.
    auto sm = makeIdleRunSM();
    sm.update(0.3f);
    CHECK(sm.getCurrentStateElapsedTime() == 0.3f);
    sm.setTrigger("Go");
    sm.update(0.0f);   // transition fires (no condition + trigger matches)
    CHECK(sm.getCurrentStateName() == "Run");
    CHECK(sm.getCurrentStateElapsedTime() == 0.0f);
    // Next update starts accumulating from 0 again.
    sm.update(0.1f);
    CHECK(sm.getCurrentStateElapsedTime() == 0.1f);
}

TEST_CASE(TIS_CrossFade_ElapsedTimeSinceFireNotComplete) {
    // INV-37 / INV-38 — cross-fade START resets to 0; the clock advances
    // during the blend window (NOT frozen until blend complete).
    StateMachine sm;
    State sA; sA.name = "Idle"; sA.clipPath = "i.ayanm"; sm.addState(sA);
    State sB; sB.name = "Run";  sB.clipPath  = "r.ayanm"; sm.addState(sB);
    Transition t;
    t.trigger   = "Go";
    t.fromState = "Idle";
    t.toState   = "Run";
    t.duration  = 0.5f;       // cross-fade
    sm.addTransition(t);
    sm.setInitialState("Idle");

    sm.update(0.3f);
    CHECK(sm.getCurrentStateElapsedTime() == 0.3f);

    sm.setTrigger("Go");
    sm.update(0.0f);          // transition START (cross-fade begins)
    CHECK(sm.getCurrentStateName() == "Idle");     // mid-transition; currentState still Idle
    CHECK(sm.getCurrentStateElapsedTime() == 0.0f);

    sm.update(0.2f);          // mid-blend
    CHECK(sm.getCurrentStateElapsedTime() == 0.2f);

    sm.update(0.5f);          // cross-fade complete
    CHECK(sm.getCurrentStateName() == "Run");
    // After cross-fade complete, accumulator was still running (0.2 + 0.5 = 0.7).
    CHECK(sm.getCurrentStateElapsedTime() == 0.7f);
}

TEST_CASE(TIS_Condition_CurrentStateTime_GT_Fires) {
    // INV-39 — reserved ident "CurrentStateTime" routes to ctx.currentStateTime.
    // After accumulating past the threshold, the L2 condition evaluates true
    // and the transition fires.
    StateMachine sm;
    State sA; sA.name = "Idle"; sA.clipPath = "i.ayanm"; sm.addState(sA);
    State sB; sB.name = "Run";  sB.clipPath  = "r.ayanm"; sm.addState(sB);
    Transition t;
    t.trigger   = "Go";
    t.fromState = "Idle";
    t.toState   = "Run";
    t.setConditionExpr("CurrentStateTime > 0.5");
    sm.addTransition(t);
    sm.setInitialState("Idle");

    sm.update(0.3f);          // elapsed < 0.5
    sm.setTrigger("Go");
    sm.update(0.0f);
    CHECK(sm.getCurrentStateName() == "Idle");      // condition false → no fire

    sm.update(0.3f);          // elapsed = 0.6 > 0.5
    sm.setTrigger("Go");      // re-arm (auto-erased on first fire attempt)
    sm.update(0.0f);
    CHECK(sm.getCurrentStateName() == "Run");       // fires now
}

TEST_CASE(TIS_Condition_CurrentStateTime_LT_DoesNotFire) {
    // INV-39 — same reserved ident, opposite polarity. Verifies the
    // comparison operator is honored and the value is the live elapsed
    // time (not a stale 0.0f literal).
    StateMachine sm;
    State sA; sA.name = "Idle"; sA.clipPath = "i.ayanm"; sm.addState(sA);
    State sB; sB.name = "Run";  sB.clipPath  = "r.ayanm"; sm.addState(sB);
    Transition t;
    t.trigger   = "Go";
    t.fromState = "Idle";
    t.toState   = "Run";
    t.setConditionExpr("CurrentStateTime < 0.5");
    sm.addTransition(t);
    sm.setInitialState("Idle");

    sm.update(0.6f);          // elapsed > 0.5
    sm.setTrigger("Go");
    sm.update(0.0f);
    CHECK(sm.getCurrentStateName() == "Idle");      // condition false → no fire

    // Reset to a fresh initial state (resets _currentStateEnterTime to 0),
    // then advance LESS than 0.5s so the condition becomes true.
    sm.setInitialState("Idle");
    sm.update(0.2f);          // elapsed = 0.2 < 0.5
    sm.setTrigger("Go");
    sm.update(0.0f);
    CHECK(sm.getCurrentStateName() == "Run");
}

// =====================================================================
// §8.1.1 Parser unit (8 cases)
// =====================================================================

TEST_CASE(L2_Parser_EmptyString_SucceedsWithNull) {
    std::string err = "stale";
    auto ast = ConditionParser::parse("", err);
    CHECK(ast == nullptr);
    CHECK(err == "");        // empty source is not an error
}

TEST_CASE(L2_Parser_SingleIdent_Ok) {
    std::string err;
    auto ast = ConditionParser::parse("Speed", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    auto ctx = makeCtx({{"Speed", 5.0f}});
    CHECK(ast->evaluate(ctx) == true);
}

TEST_CASE(L2_Parser_SingleLiteral_Ok) {
    std::string err;
    auto ast = ConditionParser::parse("3.14", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    auto ctx = makeCtx({});
    CHECK(ast->evaluate(ctx) == true);

    auto astTrue = ConditionParser::parse("true", err);
    CHECK(err.empty());
    CHECK(astTrue != nullptr);
    CHECK(astTrue->evaluate(ctx) == true);

    auto astFalse = ConditionParser::parse("false", err);
    CHECK(err.empty());
    CHECK(astFalse != nullptr);
    CHECK(astFalse->evaluate(ctx) == false);
}

TEST_CASE(L2_Parser_Compare_Ok) {
    std::string err;
    auto ast = ConditionParser::parse("Speed > 5.0", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    auto ctx = makeCtx({{"Speed", 7.0f}});
    CHECK(ast->evaluate(ctx) == true);
}

TEST_CASE(L2_Parser_AndOrPrecedence) {
    std::string err;
    // "A && B || C"  parses as  (A && B) || C  (AND binds tighter)
    auto ast = ConditionParser::parse("A && B || C", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    {
        auto ctx = makeCtx({{"A", 1.0f}, {"B", 1.0f}, {"C", 0.0f}});
        CHECK(ast->evaluate(ctx) == true);   // (A && B) || C
    }
    {
        auto ctx = makeCtx({{"A", 1.0f}, {"B", 0.0f}, {"C", 1.0f}});
        CHECK(ast->evaluate(ctx) == true);   // (A && B = false) || C
    }
    {
        auto ctx = makeCtx({{"A", 1.0f}, {"B", 0.0f}, {"C", 0.0f}});
        CHECK(ast->evaluate(ctx) == false);
    }
}

TEST_CASE(L2_Parser_Parens_Override) {
    std::string err;
    auto ast = ConditionParser::parse("(A || B) && C", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    {
        auto ctx = makeCtx({{"A", 1.0f}, {"B", 0.0f}, {"C", 1.0f}});
        CHECK(ast->evaluate(ctx) == true);
    }
    {
        auto ctx = makeCtx({{"A", 1.0f}, {"B", 0.0f}, {"C", 0.0f}});
        CHECK(ast->evaluate(ctx) == false);
    }
}

TEST_CASE(L2_Parser_UnaryNot_Ok) {
    std::string err;
    auto ast = ConditionParser::parse("!IsDead", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    {
        auto ctx = makeCtx({{"IsDead", 0.0f}});
        CHECK(ast->evaluate(ctx) == true);
    }
    {
        auto ctx = makeCtx({{"IsDead", 1.0f}});
        CHECK(ast->evaluate(ctx) == false);
    }
}

TEST_CASE(L2_Parser_SyntaxError_FailsWithLineCol) {
    std::string err;
    auto ast = ConditionParser::parse("Speed >", err);
    CHECK(ast == nullptr);
    CHECK(!err.empty());
    CHECK(err.find("line") != std::string::npos);
}

// =====================================================================
// §8.1.2 Evaluator unit (12 cases)
// =====================================================================

TEST_CASE(L2_Eval_Compare_GT_FiresWhenTrue) {
    std::string err;
    auto ast = ConditionParser::parse("Speed > 5.0", err);
    CHECK(ast != nullptr);
    auto ctx = makeCtx({{"Speed", 7.0f}});
    CHECK(ast->evaluate(ctx) == true);
}

TEST_CASE(L2_Eval_Compare_GT_FailsWhenFalse) {
    std::string err;
    auto ast = ConditionParser::parse("Speed > 5.0", err);
    CHECK(ast != nullptr);
    auto ctx = makeCtx({{"Speed", 3.0f}});
    CHECK(ast->evaluate(ctx) == false);
}

TEST_CASE(L2_Eval_AllFourCompareOps) {
    {
        std::string err;
        auto ast = ConditionParser::parse("Speed > 5.0", err);
        CHECK(ast != nullptr);
        CHECK(ast->evaluate(makeCtx({{"Speed", 6.0f}})) == true);
        CHECK(ast->evaluate(makeCtx({{"Speed", 5.0f}})) == false);
    }
    {
        std::string err;
        auto ast = ConditionParser::parse("Speed < 5.0", err);
        CHECK(ast != nullptr);
        CHECK(ast->evaluate(makeCtx({{"Speed", 4.0f}})) == true);
        CHECK(ast->evaluate(makeCtx({{"Speed", 6.0f}})) == false);
    }
    {
        std::string err;
        auto ast = ConditionParser::parse("Speed == 5.0", err);
        CHECK(ast != nullptr);
        CHECK(ast->evaluate(makeCtx({{"Speed", 5.0f}})) == true);
        CHECK(ast->evaluate(makeCtx({{"Speed", 5.0000001f}})) == true);
        CHECK(ast->evaluate(makeCtx({{"Speed", 6.0f}})) == false);
    }
    {
        std::string err;
        auto ast = ConditionParser::parse("Speed != 5.0", err);
        CHECK(ast != nullptr);
        CHECK(ast->evaluate(makeCtx({{"Speed", 4.0f}})) == true);
        CHECK(ast->evaluate(makeCtx({{"Speed", 5.0f}})) == false);
    }
}

TEST_CASE(L2_Eval_And_ShortCircuit) {
    std::string err;
    auto ast = ConditionParser::parse("A && B", err);
    CHECK(ast != nullptr);
    // When A is false, the AST still returns false; we can't observe the
    // short-circuit directly via public API but we CAN observe that B is
    // irrelevant — substituting any value for B still yields false when A
    // is false.
    auto ctxFalse = makeCtx({{"A", 0.0f}, {"B", 1.0f}});
    CHECK(ast->evaluate(ctxFalse) == false);
    auto ctxTrue = makeCtx({{"A", 1.0f}, {"B", 1.0f}});
    CHECK(ast->evaluate(ctxTrue) == true);
}

TEST_CASE(L2_Eval_Or_ShortCircuit) {
    std::string err;
    auto ast = ConditionParser::parse("A || B", err);
    CHECK(ast != nullptr);
    auto ctxTrue = makeCtx({{"A", 1.0f}, {"B", 0.0f}});
    CHECK(ast->evaluate(ctxTrue) == true);
    auto ctxBoth = makeCtx({{"A", 0.0f}, {"B", 0.0f}});
    CHECK(ast->evaluate(ctxBoth) == false);
}

TEST_CASE(L2_Eval_Not_FlipsBool) {
    std::string err;
    auto ast = ConditionParser::parse("!IsDead", err);
    CHECK(ast != nullptr);
    CHECK(ast->evaluate(makeCtx({{"IsDead", 1.0f}})) == false);
    CHECK(ast->evaluate(makeCtx({{"IsDead", 0.0f}})) == true);
}

TEST_CASE(L2_Eval_NestedParens) {
    std::string err;
    auto ast = ConditionParser::parse("(A || B) && C", err);
    CHECK(ast != nullptr);
    CHECK(ast->evaluate(makeCtx({{"A", 1.0f}, {"B", 0.0f}, {"C", 1.0f}})) == true);
    CHECK(ast->evaluate(makeCtx({{"A", 1.0f}, {"B", 0.0f}, {"C", 0.0f}})) == false);
    CHECK(ast->evaluate(makeCtx({{"A", 0.0f}, {"B", 1.0f}, {"C", 1.0f}})) == true);
}

TEST_CASE(L2_Eval_UnknownParam_FailsSoft) {
    std::string err;
    auto ast = ConditionParser::parse("NonExist > 5", err);
    CHECK(ast != nullptr);
    // INV-23 — unknown identifier returns 0.0f; 0 > 5 is false.
    CHECK(ast->evaluate(makeCtx({})) == false);
}

TEST_CASE(L2_Eval_LiteralComparison) {
    std::string err;
    auto ast = ConditionParser::parse("5 > 3", err);
    CHECK(ast != nullptr);
    CHECK(ast->evaluate(makeCtx({})) == true);

    auto astEq = ConditionParser::parse("5 == 5", err);
    CHECK(astEq != nullptr);
    CHECK(astEq->evaluate(makeCtx({})) == true);
}

TEST_CASE(L2_Eval_BoolCoercion) {
    std::string err;
    auto ast = ConditionParser::parse("IsGrounded == 1.0", err);
    CHECK(ast != nullptr);
    CHECK(ast->evaluate(makeCtx({{"IsGrounded", 1.0f}})) == true);
    CHECK(ast->evaluate(makeCtx({{"IsGrounded", 0.0f}})) == false);
}

TEST_CASE(L2_Eval_DeepNesting_5Levels) {
    std::string err;
    // (((((A && B) || C) && D) || E))
    auto ast = ConditionParser::parse("((((A && B) || C) && D) || E)", err);
    CHECK(ast != nullptr);
    // Path 1: A&&B is true, skip C, (true || C) = true, then && D, then || E
    CHECK(ast->evaluate(makeCtx({
        {"A", 1.0f}, {"B", 1.0f}, {"C", 0.0f}, {"D", 1.0f}, {"E", 0.0f}
    })) == true);
    // Path 2: A&&B is false, fallback to C, then && D, then || E
    CHECK(ast->evaluate(makeCtx({
        {"A", 0.0f}, {"B", 1.0f}, {"C", 1.0f}, {"D", 0.0f}, {"E", 1.0f}
    })) == true);
    // Path 3: A&&B false, C false, D false, E false
    CHECK(ast->evaluate(makeCtx({
        {"A", 0.0f}, {"B", 0.0f}, {"C", 0.0f}, {"D", 0.0f}, {"E", 0.0f}
    })) == false);
}

TEST_CASE(L2_Eval_FloatLiteral_Negative) {
    std::string err;
    auto ast = ConditionParser::parse("-3.14 < 0", err);
    CHECK(ast != nullptr);
    CHECK(ast->evaluate(makeCtx({})) == true);
}

// =====================================================================
// §8.1.3 Cache / invalidate unit (6 cases)
// =====================================================================

TEST_CASE(L2_Cache_FirstEval_Parses) {
    auto sm = makeIdleRunSM();
    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());
    transitions[0].setConditionExpr("A && B");
    CHECK(transitions[0].conditionDirty == true);
    CHECK(transitions[0].cachedAst == nullptr);

    sm.setParam("A", 1.0f);
    sm.setParam("B", 1.0f);
    sm.setTrigger("Go");
    sm.update(0.0f);   // triggers findEligibleTransition → first evaluate
    CHECK(transitions[0].conditionDirty == false);
    CHECK(transitions[0].cachedAst != nullptr);
    CHECK(transitions[0].conditionParseError.empty());
}

TEST_CASE(L2_Cache_SecondEval_NoReparse) {
    auto sm = makeIdleRunSM();
    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());
    transitions[0].setConditionExpr("A > 0");

    sm.setParam("A", 1.0f);
    sm.setTrigger("Go");
    sm.update(0.0f);
    const auto* firstAst = transitions[0].cachedAst.get();
    CHECK(firstAst != nullptr);

    sm.update(0.0f);
    const auto* secondAst = transitions[0].cachedAst.get();
    CHECK(secondAst == firstAst);   // same AST — no reparse
}

TEST_CASE(L2_Cache_SetConditionExpr_Invalidates) {
    auto sm = makeIdleRunSM();
    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());
    transitions[0].setConditionExpr("A > 0");
    sm.setParam("A", 1.0f);
    sm.setTrigger("Go");
    sm.update(0.0f);
    CHECK(transitions[0].cachedAst != nullptr);
    // After fire, the transition advanced Idle → Run.
    CHECK(sm.getCurrentStateName() == "Run");

    // Re-arm and switch the expression: setConditionExpr must flag dirty
    // AND the next evaluate must observe the new expression (not the
    // cached "A > 0" — that would otherwise evaluate true here too).
    sm.setInitialState("Idle");
    transitions[0].setConditionExpr("B > 0");
    CHECK(transitions[0].conditionDirty == true);
    sm.setParam("B", 1.0f);
    sm.setTrigger("Go");
    sm.update(0.0f);
    // The new expression fires (B > 0 with B=1).
    CHECK(sm.getCurrentStateName() == "Run");
    CHECK(transitions[0].cachedAst != nullptr);
}

TEST_CASE(L2_Cache_ExplicitInvalidate) {
    auto sm = makeIdleRunSM();
    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());
    transitions[0].setConditionExpr("A > 0");
    sm.setParam("A", 1.0f);
    sm.setTrigger("Go");
    sm.update(0.0f);
    CHECK(transitions[0].cachedAst != nullptr);

    transitions[0].invalidateConditionCache();
    CHECK(transitions[0].conditionDirty == true);

    // Re-arm the SM at Idle so the transition still matches on next
    // update (Idle→Run). Otherwise the post-fire currentState="Run"
    // would skip the transition (fromState mismatch).
    sm.setInitialState("Idle");
    sm.setTrigger("Go");
    sm.update(0.0f);
    CHECK(transitions[0].cachedAst != nullptr);   // rebuilt on next evaluate
    CHECK(transitions[0].conditionDirty == false);
}

TEST_CASE(L2_Cache_ParseFail_CachedNull_ReturnsFalse) {
    auto sm = makeIdleRunSM();
    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());
    transitions[0].setConditionExpr("Speed >");   // bad parse
    sm.setParam("Speed", 7.0f);
    sm.setTrigger("Go");
    sm.update(0.0f);
    CHECK(transitions[0].cachedAst == nullptr);
    CHECK(!transitions[0].conditionParseError.empty());
    // INV-33 — transition does not fire even though Speed > (truncated) would
    // have evaluated true.
    CHECK(sm.getCurrentStateName() == "Idle");
}

TEST_CASE(L2_Cache_ParseFail_DoesNotCrash) {
    auto sm = makeIdleRunSM();
    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());
    transitions[0].setConditionExpr("@#$ garbage");
    sm.setTrigger("Go");
    for (int i = 0; i < 100; ++i) {
        sm.update(0.016f);
    }
    // No crash, still Idle, transition never fired.
    CHECK(sm.getCurrentStateName() == "Idle");
    CHECK(transitions[0].cachedAst == nullptr);
}

// =====================================================================
// §8.1.4 Backward-compat (L1 zero regression) (4 cases)
// =====================================================================

TEST_CASE(L2_BackCompat_L1Condition_StillWorks) {
    // L1 single-predicate path — conditionExpr empty + hasCondition=true.
    auto sm = makeIdleRunSM();
    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());
    transitions[0].hasCondition = true;
    transitions[0].condition.paramName    = "Speed";
    transitions[0].condition.op           = StateConditionOp::Greater;
    transitions[0].condition.compareValue  = 5.0f;
    transitions[0].conditionExpr.clear();   // L2 path inactive

    sm.setParam("Speed", 7.0f);
    sm.setTrigger("Go");
    sm.update(0.0f);
    CHECK(sm.getCurrentStateName() == "Run");
}

TEST_CASE(L2_BackCompat_L1UnknownParam_FailsSoft) {
    auto sm = makeIdleRunSM();
    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());
    transitions[0].hasCondition = true;
    transitions[0].condition.paramName    = "NonExistentParam";
    transitions[0].condition.op           = StateConditionOp::Greater;
    transitions[0].condition.compareValue  = 5.0f;
    transitions[0].conditionExpr.clear();

    sm.setTrigger("Go");
    sm.update(0.0f);
    CHECK(sm.getCurrentStateName() == "Idle");   // INV-23 fail-soft
}

TEST_CASE(L2_BackCompat_NoCondition_AlwaysFires) {
    // INV-32 — empty conditionExpr + hasCondition=false ⇒ true unconditionally.
    auto sm = makeIdleRunSM();
    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());
    transitions[0].hasCondition = false;
    transitions[0].conditionExpr.clear();

    sm.setTrigger("Go");
    sm.update(0.0f);
    CHECK(sm.getCurrentStateName() == "Run");
}

TEST_CASE(L2_BackCompat_SubMachinePath_Unaffected) {
    // P3.2 L3 sub-machine entry should NOT be affected by L2 — a sub-machine
    // entry has no conditionExpr, just isSubMachine flag. Verify the existing
    // P3.2 lazy-init + child activation path still works.
    StateMachine sm;
    State sA; sA.name = "Top"; sA.clipPath = "top.ayanm"; sm.addState(sA);
    State sB; sB.name = "Bot"; sB.clipPath = "bot.ayanm"; sm.addState(sB);

    auto child = std::make_unique<StateMachine>();
    State sIdle; sIdle.name = "Idle"; sIdle.clipPath = "i.ayanm";
    State sRun;  sRun.name  = "Run";  sRun.clipPath  = "r.ayanm";
    child->addState(sIdle);
    child->addState(sRun);
    Transition childT;
    childT.trigger   = "Go";
    childT.fromState = "Idle";
    childT.toState   = "Run";
    child->addTransition(childT);
    child->setInitialState("Idle");
    const int cidx = sm.addSubMachine(std::move(child));

    auto& states = const_cast<std::vector<State>&>(sm.getStates());
    states[0].isSubMachine    = true;
    states[0].subMachineIndex = cidx;
    sm.setInitialState("Top");

    sm.setTrigger("Go");
    sm.update(0.0f);
    // child fires: Idle → Run
    CHECK(sm.getActiveLeafStateName() == "Run");
}

TEST_SUITE_END