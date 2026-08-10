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
//   §8.1.6 Arithmetic + - * / (11 cases, NEW P5 polish 2026-08-10)
//   §P5 polish — bytecode arith parity + opcode encoding (3 cases)
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

// =====================================================================
// §P1 polish — Hot-path hash cache for CondIdentifierExpr (INV-50..51)
// =====================================================================
//
// After P1 polish, CondIdentifierExpr::nameHash is pre-computed at ctor
// time via ParamNameRegistry::intern(). The per-eval intern() call inside
// evaluateAsFloat is eliminated; the cached nameHash is walked directly
// against ctx.params. Production state machines with 3+ identifiers in
// a transition condition (e.g. "Speed > 5.0 && IsGrounded && !IsDead")
// benefit the most — only 3 intern() calls total (one per ctor) instead
// of 3 × N evals × 60 fps.

TEST_CASE(P1_CondIdent_NameHash_NonEmpty) {
    // INV-50 — CondIdentifierExpr::nameHash is non-zero for non-empty
    // names. FNV-1a baseline guarantees no collision with 0.
    CondIdentifierExpr ident(std::string("Speed"));
    CHECK(ident.name == "Speed");
    CHECK(ident.nameHash != 0);                   // INV-50
    // The cached hash matches what the registry would return.
    const uint32_t directHash =
        ayt::anim::detail::ParamNameRegistry::instance().intern("Speed");
    CHECK(ident.nameHash == directHash);
}

TEST_CASE(P1_CondIdent_NameHash_EmptyName_HashZero) {
    // INV-50 sentinel — empty name yields 0 hash (no intern call,
    // no entry inserted into the registry).
    const std::size_t registrySizeBefore =
        StateMachine::getParamNameRegistrySize();
    CondIdentifierExpr identEmpty(std::string(""));
    CondIdentifierExpr identFoo(std::string("Foo"));
    const std::size_t registrySizeAfter =
        StateMachine::getParamNameRegistrySize();

    CHECK(identEmpty.name == "");
    CHECK(identEmpty.nameHash == 0);              // INV-50 sentinel
    CHECK(identFoo.name == "Foo");
    CHECK(identFoo.nameHash != 0);

    // Only "Foo" was interned; empty name skipped (no insert).
    CHECK(registrySizeAfter - registrySizeBefore == 1);
}

TEST_CASE(P1_CondIdent_Evaluate_NoIntern) {
    // The hot path: 1000 evaluations of the same identifier expression
    // should NOT trigger additional ParamNameRegistry::intern() calls.
    // Registry size is captured before/after to assert zero growth
    // beyond the single ctor-time intern.
    CondIdentifierExpr ident(std::string("Speed"));
    const std::size_t registrySizeAfterCtor =
        StateMachine::getParamNameRegistrySize();

    // Build a 1-entry params vector (hash matches ident.nameHash).
    std::vector<ParamEntry> paramVec;
    paramVec.push_back({ ident.nameHash, 5.0f });

    ConditionEvalCtx ctx;
    ctx.params = &paramVec;
    ctx.triggers = nullptr;
    ctx.currentState = "Idle";
    ctx.currentStateTime = 0.0f;

    // 1000 evaluations — registry size must stay constant.
    for (int i = 0; i < 1000; ++i) {
        const float v = ident.evaluateAsFloat(ctx);
        CHECK(v == 5.0f);
    }

    const std::size_t registrySizeAfterEvals =
        StateMachine::getParamNameRegistrySize();
    CHECK(registrySizeAfterEvals == registrySizeAfterCtor);   // zero per-eval intern

    // Reserved ident priority preserved (INV-51).
    CondIdentifierExpr reserved(std::string("CurrentStateTime"));
    ctx.currentStateTime = 1.25f;
    CHECK(reserved.evaluateAsFloat(ctx) == 1.25f);   // uses ctx, not registry
}

// =====================================================================
// P2 polish (2026-08-07) — Bytecode parallel cache unit tests (INV-52..58)
// =====================================================================

// P2_Bytecode_Parity_3IdentExpr — compile `(Speed>5) && IsGrounded && !IsDead`
// to bytecode; evaluate(ctx) must produce IDENTICAL boolean to the AST path
// across 5 case scenarios. INV-53 + INV-54 contract.
TEST_CASE(P2_Bytecode_Parity_3IdentExpr) {
    // Build AST once.
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

    auto code = ayt::anim::compileToBytecode(full.get());
    CHECK(code != nullptr);
    CHECK(!code->program.empty());
    CHECK(!code->literals.empty());

    // Build params vector: Speed, IsGrounded, IsDead all present.
    std::vector<ParamEntry> paramVec;
    paramVec.push_back({ ayt::anim::detail::ParamNameRegistry::instance().intern("Speed"), 7.0f });
    paramVec.push_back({ ayt::anim::detail::ParamNameRegistry::instance().intern("IsGrounded"), 1.0f });
    paramVec.push_back({ ayt::anim::detail::ParamNameRegistry::instance().intern("IsDead"), 0.0f });
    ConditionEvalCtx ctx;
    ctx.params = &paramVec;
    ctx.triggers = nullptr;
    ctx.currentState = "Idle";
    ctx.currentStateTime = 0.0f;

    // Case 1 — all-true (true path).
    const bool astTrue = full->evaluate(ctx);
    const bool bcTrue  = code->evaluate(ctx);
    CHECK(astTrue == true);
    CHECK(bcTrue == true);
    CHECK(astTrue == bcTrue);

    // Case 2 — one-false (Speed=3 → false path).
    paramVec[0].value = 3.0f;
    CHECK(full->evaluate(ctx) == false);
    CHECK(code->evaluate(ctx) == false);
    paramVec[0].value = 7.0f;                  // restore

    // Case 3 — short-circuit (IsDead=1 → false; only NOT branch matters).
    paramVec[2].value = 1.0f;
    CHECK(full->evaluate(ctx) == false);
    CHECK(code->evaluate(ctx) == false);
    paramVec[2].value = 0.0f;                  // restore

    // Case 4 — unknown param (use a name not in paramVec) → fail-soft false
    //          (INV-23 / bytecode fallback to 0.0f).
    auto unknown = std::make_unique<ayt::anim::CondIdentifierExpr>(
        std::string("P2_Parity_Unknown_Param_Name"));
    CHECK(code != nullptr);                    // code is the compiled 3-ident expr
    // For bytecode, we need a separate compile that uses the unknown ident.
    auto unknownCode = ayt::anim::compileToBytecode(unknown.get());
    CHECK(unknownCode != nullptr);
    CHECK(unknownCode->evaluate(ctx) == false);  // INV-23 — fail-soft 0.0f → false

    // Case 5 — reserved ident "CurrentStateTime" encoded as OP_LOAD_RESERVED.
    auto reserved = std::make_unique<ayt::anim::CondIdentifierExpr>(
        std::string("CurrentStateTime"));
    auto reservedCode = ayt::anim::compileToBytecode(reserved.get());
    CHECK(reservedCode != nullptr);
    ctx.currentStateTime = 1.25f;
    CHECK(reserved->evaluateAsFloat(ctx) == 1.25f);
    // Bytecode path returns currentStateTime directly (push 1.25f onto stack;
    // final coerce to bool = 1.25 != 0.0 = true).
    CHECK(reservedCode->evaluate(ctx) == true);   // INV-55 — encoded as opcode
    ctx.currentStateTime = 0.0f;
    CHECK(reservedCode->evaluate(ctx) == false);  // 0.0f → false
}

// P2_Bytecode_LazyBuild_FirstEvalCompiles — `cachedBytecode == null` initially;
// after evaluateBytecode(ctx), cachedBytecode != null + program.size() > 0.
// INV-52 contract: parallel cache, lazy init.
TEST_CASE(P2_Bytecode_LazyBuild_FirstEvalCompiles) {
    using ayt::anim::StateMachine;
    using ayt::anim::State;
    using ayt::anim::Transition;

    StateMachine sm;
    State sA; sA.name = "Idle"; sA.clipPath = "i.ayanm"; sm.addState(sA);
    State sB; sB.name = "Run";  sB.clipPath = "r.ayanm"; sm.addState(sB);
    sm.setInitialState("Idle");

    Transition t;
    t.fromState = "Idle";
    t.toState   = "Run";
    t.conditionExpr = "Speed > 5.0";
    sm.addTransition(t);

    // Initial state — cachedBytecode is null (not yet evaluated).
    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());
    CHECK(transitions[0].cachedBytecode == nullptr);   // INV-52 — null before first eval

    // Drive evaluateBytecode via findEligibleTransition by calling sm.update().
    sm.setParam("Speed", 7.0f);
    sm.update(0.0f);

    // After update(), the hot path evaluateBytecode fired.
    CHECK(transitions[0].cachedBytecode != nullptr);  // INV-52 — built lazily
    CHECK(!transitions[0].cachedBytecode->program.empty());
    CHECK(!transitions[0].cachedBytecode->literals.empty());

    // The compiled bytecode's first load uses Speed hash.
    // OP_LOAD_PARAM = 7 (byte). The next 4 bytes are the hash.
    CHECK(transitions[0].cachedBytecode->program[0] ==
          static_cast<uint8_t>(ayt::anim::CondOpByte::OP_LOAD_PARAM));
}

// P2_Bytecode_ReservedIdent_CompiledAsOpcode — compileToBytecode of
// CondIdentifierExpr("CurrentStateTime") emits OP_LOAD_RESERVED, NOT
// OP_LOAD_PARAM. INV-55 contract.
TEST_CASE(P2_Bytecode_ReservedIdent_CompiledAsOpcode) {
    auto reserved = std::make_unique<ayt::anim::CondIdentifierExpr>(
        std::string("CurrentStateTime"));
    auto code = ayt::anim::compileToBytecode(reserved.get());
    CHECK(code != nullptr);
    CHECK(code->program.size() == 2);          // OP_LOAD_RESERVED + rid byte
    CHECK(code->program[0] ==
          static_cast<uint8_t>(ayt::anim::CondOpByte::OP_LOAD_RESERVED));
    CHECK(code->program[1] ==
          static_cast<uint8_t>(ayt::anim::CondReservedId::R_CURRENT_STATE_TIME));
    // Sanity: NOT OP_LOAD_PARAM.
    CHECK(code->program[0] !=
          static_cast<uint8_t>(ayt::anim::CondOpByte::OP_LOAD_PARAM));
}

// P2_Bytecode_ParseFail_NullBytecode_ReturnsFalse — invalid DSL produces
// evaluateBytecode == false + cachedBytecode == null + conditionParseError
// non-empty (INV-33 + INV-52).
TEST_CASE(P2_Bytecode_ParseFail_NullBytecode_ReturnsFalse) {
    using ayt::anim::StateMachine;
    using ayt::anim::State;
    using ayt::anim::Transition;

    StateMachine sm;
    State sA; sA.name = "A"; sA.clipPath = "a.ayanm"; sm.addState(sA);
    State sB; sB.name = "B"; sB.clipPath = "b.ayanm"; sm.addState(sB);
    sm.setInitialState("A");

    Transition t;
    t.fromState = "A";
    t.toState   = "B";
    t.conditionExpr = "&&&";                   // invalid DSL
    sm.addTransition(t);

    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());

    // Drive evaluateBytecode via findEligibleTransition (private; use sm.update).
    sm.update(0.0f);

    // INV-33 — parse failed; cachedAst stays null; INV-52 — cachedBytecode
    // never built because AST is null.
    CHECK(transitions[0].cachedAst == nullptr);
    CHECK(transitions[0].cachedBytecode == nullptr);
    CHECK(!transitions[0].conditionParseError.empty());

    // Stayed in A because evaluateBytecode returned false (no fire).
    CHECK(sm.getCurrentStateName() == "A");
}

// =====================================================================
// §8.1.6 Arithmetic + - * / (P5 polish 2026-08-10, INV-64..69)
// =====================================================================
//
// DSL 四则运算. Grammar addition (low → high):
//   ... Compare(4) < Add/Sub(5) < Mul/Div(6) < unary ! / - < Primary
//   "A + B > C" parses as "(A + B) > C" (INV-69).
//   "A-3" == "A - 3" (INV-65 lexer disambiguation).
//   "-A * B" == "(-A) * B" (INV-66 unary minus binds tighter).
//   "5 / 0" → 0.0f (INV-67 fail-soft, never inf/nan).

TEST_CASE(P5_Arith_Add_Compare) {
    // INV-69 — arithmetic binds tighter than comparison.
    std::string err;
    auto ast = ConditionParser::parse("Speed + 1 > 5", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    CHECK(ast->evaluate(makeCtx({{"Speed", 7.0f}})) == true);
    CHECK(ast->evaluate(makeCtx({{"Speed", 3.0f}})) == false);
}

TEST_CASE(P5_Arith_MulDiv_Precedence) {
    // INV-69 — Mul/Div binds tighter than Add/Sub.
    std::string err;
    auto ast = ConditionParser::parse("A + B * 2 == 10", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    // B*2 first: 4 + 3*2 = 10 → true. If it parsed as (A+B)*2 = 14 → false.
    CHECK(ast->evaluate(makeCtx({{"A", 4.0f}, {"B", 3.0f}})) == true);
    CHECK(ast->evaluate(makeCtx({{"A", 5.0f}, {"B", 3.0f}})) == false);

    auto ast2 = ConditionParser::parse("A * 2 + B == 10", err);
    CHECK(ast2 != nullptr);
    CHECK(ast2->evaluate(makeCtx({{"A", 3.0f}, {"B", 4.0f}})) == true);
}

TEST_CASE(P5_Arith_Parens_Override) {
    // Parens still override arithmetic precedence.
    std::string err;
    auto ast = ConditionParser::parse("(A + B) * 2 == 10", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    CHECK(ast->evaluate(makeCtx({{"A", 2.0f}, {"B", 3.0f}})) == true);
    // (4+2)*2 = 12 ≠ 10 → false (if parens were ignored: 4+2*2 = 8 ≠ 10, also
    // false — use B=2 so the parens actually matter for the false arm).
    CHECK(ast->evaluate(makeCtx({{"A", 4.0f}, {"B", 2.0f}})) == false);
}

TEST_CASE(P5_Arith_NoSpace_Minus) {
    // INV-65 — '-' after an expression-ending token is binary subtraction,
    // even with no whitespace: "A-3" == "A - 3" (not [A][-3] parse error).
    std::string err;
    auto ast = ConditionParser::parse("A-3 > 0", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    CHECK(ast->evaluate(makeCtx({{"A", 5.0f}})) == true);
    CHECK(ast->evaluate(makeCtx({{"A", 2.0f}})) == false);

    // "1-2 == -1": left '-' is binary, right "-1" is a negative literal.
    auto ast2 = ConditionParser::parse("1-2 == -1", err);
    CHECK(ast2 != nullptr);
    CHECK(ast2->evaluate(makeCtx({})) == true);
}

TEST_CASE(P5_Arith_Div_Ok) {
    std::string err;
    auto ast = ConditionParser::parse("Speed / 2 > 3", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    CHECK(ast->evaluate(makeCtx({{"Speed", 8.0f}})) == true);
    CHECK(ast->evaluate(makeCtx({{"Speed", 6.0f}})) == false);
}

TEST_CASE(P5_Arith_DivByZero_FailsSoft) {
    // INV-67 — division by zero yields 0.0f (fail-soft; never inf/nan
    // which would poison the downstream fabs-epsilon comparisons).
    std::string err;
    auto ast = ConditionParser::parse("5 / 0 == 0", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    CHECK(ast->evaluate(makeCtx({})) == true);

    // Zero via a runtime expression, not just a literal.
    auto ast2 = ConditionParser::parse("5 / (A - A) == 0", err);
    CHECK(ast2 != nullptr);
    CHECK(ast2->evaluate(makeCtx({{"A", 7.0f}})) == true);

    // 0.0f > 0 is false — the coercion stays sane.
    auto ast3 = ConditionParser::parse("5 / 0 > 0", err);
    CHECK(ast3 != nullptr);
    CHECK(ast3->evaluate(makeCtx({})) == false);
}

TEST_CASE(P5_Arith_UnaryNeg) {
    // INV-66 — unary minus on identifiers / expressions.
    std::string err;
    auto ast = ConditionParser::parse("-Speed > 0", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    CHECK(ast->evaluate(makeCtx({{"Speed", -3.0f}})) == true);
    CHECK(ast->evaluate(makeCtx({{"Speed", 3.0f}})) == false);

    // Discriminator: Speed=-3 → (-Speed)-1 = 3-1 = 2 == 2 → true.
    // If unary minus bound LOOSER than binary '-' it would parse as
    // -(Speed-1) = -(-4) = 4 ≠ 2 → false. Proves unary binds tighter.
    auto ast2 = ConditionParser::parse("-Speed - 1 == 2", err);
    CHECK(ast2 != nullptr);
    CHECK(ast2->evaluate(makeCtx({{"Speed", -3.0f}})) == true);

    // Unary minus tighter than Mul: (-A)*B = (-3)*2 = -6.
    auto ast3 = ConditionParser::parse("-A * B == -6", err);
    CHECK(ast3 != nullptr);
    CHECK(ast3->evaluate(makeCtx({{"A", 3.0f}, {"B", 2.0f}})) == true);
}

TEST_CASE(P5_Arith_Chain_LeftAssoc) {
    // Same-precedence chains are left-associative.
    std::string err;
    auto ast = ConditionParser::parse("A + B + C == 6", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    CHECK(ast->evaluate(makeCtx({{"A", 1.0f}, {"B", 2.0f}, {"C", 3.0f}})) == true);

    // Left-assoc: (6-3)-1 = 2. Right-assoc would give 6-(3-1) = 4 ≠ 2.
    auto ast2 = ConditionParser::parse("A - B - C == 2", err);
    CHECK(ast2 != nullptr);
    CHECK(ast2->evaluate(makeCtx({{"A", 6.0f}, {"B", 3.0f}, {"C", 1.0f}})) == true);
}

TEST_CASE(P5_Arith_BoolCoercion) {
    // INV-64 — an arithmetic expression used directly as the condition
    // coerces to bool via (value != 0.0f), same as bare identifiers.
    std::string err;
    auto ast = ConditionParser::parse("Speed * 2", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    CHECK(ast->evaluate(makeCtx({{"Speed", 3.0f}})) == true);
    CHECK(ast->evaluate(makeCtx({{"Speed", 0.0f}})) == false);
}

TEST_CASE(P5_Arith_ParseErrors) {
    // Truncated / misplaced arithmetic operators fail cleanly (INV-33).
    const char* bad[] = {
        "A +", "A *", "Speed > 5 +", "A ** B", "A && + B", "* A", "+ 5",
    };
    for (const char* e : bad) {
        std::string err;
        auto ast = ConditionParser::parse(e, err);
        CHECK(ast == nullptr);
        CHECK(!err.empty());
    }
}

TEST_CASE(P5_Arith_Transition_Integration) {
    // End-to-end: an arithmetic L2 condition drives a real SM transition
    // through the bytecode hot path (findEligibleTransition).
    auto sm = makeIdleRunSM();
    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());
    transitions[0].setConditionExpr("Speed * 2 > 10");
    sm.setParam("Speed", 6.0f);
    sm.setTrigger("Go");
    sm.update(0.0f);
    CHECK(sm.getCurrentStateName() == "Run");     // 12 > 10 fires

    // Re-arm with a low Speed — condition false, no fire.
    sm.setInitialState("Idle");
    transitions[0].invalidateConditionCache();    // re-parse path too
    sm.setParam("Speed", 4.0f);
    sm.setTrigger("Go");
    sm.update(0.0f);
    CHECK(sm.getCurrentStateName() == "Idle");    // 8 > 10 stays
}

// =====================================================================
// §P5 polish — Bytecode arith parity + opcode encoding (INV-64..68)
// =====================================================================

TEST_CASE(P5_Arith_Bytecode_Parity) {
    // INV-68 — OP_ADD/SUB/MUL/DIV/NEG ≡ AST CondOp arith: parse → compile →
    // evaluate both; bytecode result must match the AST exactly for the
    // same ctx (and both match the expected outcome).
    struct Case { const char* expr; float a; float b; float c; float cst; bool expected; };
    const Case cases[] = {
        { "Speed + 1 > 5",     7.0f, 0.0f, 0.0f, 0.0f,  true  },
        { "Speed + 1 > 5",     3.0f, 0.0f, 0.0f, 0.0f,  false },
        { "(A + B) * 2 == 10", 2.0f, 3.0f, 0.0f, 0.0f,  true  },
        { "(A + B) * 2 == 10", 4.0f, 2.0f, 0.0f, 0.0f,  false },   // (4+2)*2 = 12
        { "A + B * 2 == 10",   4.0f, 3.0f, 0.0f, 0.0f,  true  },
        { "Speed / 2 > 3",     8.0f, 0.0f, 0.0f, 0.0f,  true  },
        { "Speed / 2 > 3",     6.0f, 0.0f, 0.0f, 0.0f,  false },
        { "-Speed > 0",       -3.0f, 0.0f, 0.0f, 0.0f,  true  },
        { "-Speed > 0",        3.0f, 0.0f, 0.0f, 0.0f,  false },
        { "A-3 == 2",          5.0f, 0.0f, 0.0f, 0.0f,  true  },   // INV-65 no-space
        { "5 / (A - A) == 0",  7.0f, 0.0f, 0.0f, 0.0f,  true  },   // INV-67
        { "CurrentStateTime * 2 > 1", 0.0f, 0.0f, 0.0f, 0.75f, true },  // INV-39 + arith
        { "-3.14 < 0",         0.0f, 0.0f, 0.0f, 0.0f,  true  },   // negative literal
    };
    int checked = 0;
    for (const auto& cs : cases) {
        std::string err;
        auto ast = ConditionParser::parse(cs.expr, err);
        CHECK(ast != nullptr);
        if (ast == nullptr) continue;
        auto code = ayt::anim::compileToBytecode(ast.get());
        CHECK(code != nullptr);
        if (code == nullptr) continue;

        auto ctx = makeCtx({{"Speed", cs.a}, {"A", cs.a}, {"B", cs.b}, {"C", cs.c}});
        ctx.currentStateTime = cs.cst;
        const bool astV = ast->evaluate(ctx);
        const bool bcV  = code->evaluate(ctx);
        CHECK(astV == cs.expected);
        CHECK(bcV == cs.expected);
        CHECK(astV == bcV);                 // INV-68 parity — the key assertion
        ++checked;
    }
    CHECK(checked == static_cast<int>(sizeof(cases) / sizeof(cases[0])));
}

TEST_CASE(P5_Arith_Bytecode_Opcodes) {
    // Encoding check (INV-68): the trailing opcode of the compiled program
    // is the last emitted operator (post-order walk).
    auto lastOp = [](const char* expr) -> uint8_t {
        std::string err;
        auto ast = ConditionParser::parse(expr, err);
        CHECK(ast != nullptr);
        auto code = ayt::anim::compileToBytecode(ast.get());
        CHECK(code != nullptr);
        return (code && !code->program.empty()) ? code->program.back() : 0xFF;
    };
    CHECK(lastOp("A + B") == static_cast<uint8_t>(CondOpByte::OP_ADD));
    CHECK(lastOp("A - B") == static_cast<uint8_t>(CondOpByte::OP_SUB));
    CHECK(lastOp("A * B") == static_cast<uint8_t>(CondOpByte::OP_MUL));
    CHECK(lastOp("A / B") == static_cast<uint8_t>(CondOpByte::OP_DIV));
    CHECK(lastOp("-A")    == static_cast<uint8_t>(CondOpByte::OP_NEG));
    CHECK(lastOp("A - 3") == static_cast<uint8_t>(CondOpByte::OP_SUB));   // INV-65
    // "-3.14 < 0" — negative literal is ONE LOAD_LITERAL, NOT unary neg
    // (INV-65 shape preservation: pre-P5 AST shape kept).
    CHECK(lastOp("-3.14 < 0") != static_cast<uint8_t>(CondOpByte::OP_NEG));
}

TEST_CASE(P5_Arith_Bytecode_ShortCircuit_SkipArith) {
    // INV-58 still holds with arithmetic in the skipped right subtree:
    // "A && (B + 1 > 5)" — when A is false the right subtree (incl. arith)
    // is jumped over, and bytecode matches AST.
    std::string err;
    auto ast = ConditionParser::parse("A && (B + 1 > 5)", err);
    CHECK(err.empty());
    CHECK(ast != nullptr);
    auto code = ayt::anim::compileToBytecode(ast.get());
    CHECK(code != nullptr);

    // A false → short-circuit; B value irrelevant.
    CHECK(ast->evaluate(makeCtx({{"A", 0.0f}, {"B", 0.0f}})) == false);
    CHECK(code->evaluate(makeCtx({{"A", 0.0f}, {"B", 0.0f}})) == false);
    // A true → right subtree evaluates: B+1=6 > 5 → true.
    CHECK(ast->evaluate(makeCtx({{"A", 1.0f}, {"B", 5.0f}})) == true);
    CHECK(code->evaluate(makeCtx({{"A", 1.0f}, {"B", 5.0f}})) == true);
}

TEST_SUITE_END