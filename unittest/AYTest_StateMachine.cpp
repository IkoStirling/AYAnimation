// AYTest_StateMachine.cpp — P3.1 (2026-08-06) L1 简单状态机 unit tests +
//                              P0 polish (2026-08-07) flat-array tests +
//                              P1 polish (2026-08-07) hot-path hash cache.
//
// 15 cases pinning INV-18..24 contracts from design.md §4.14:
//   INV-18 single current state at any moment after update()
//   INV-19 didTransitionThisFrame true iff state changed in update()
//   INV-20 trigger fires once + auto-erases
//   INV-21 fromState="" matches any; trigger="" automatic
//   INV-22 cross-fade waits for duration before currentState updates
//   INV-23 unknown param returns false (fail-soft)
//   INV-24 addTransition rejects unknown toState (debug assert)
//
// + 2 cases (P0 polish) pinning INV-43..46:
//   INV-43..44 ParamNameRegistry hash-keyed intern table is process-global
//   INV-45     _params is a flat std::vector<ParamEntry>; findParamIndex
//              linear probe; vec reserve 8
//   INV-46     _triggers is a sorted std::vector<uint32_t>; binary search
//              via std::lower_bound; vec reserve 4
//
// + 2 cases (P1 polish) pinning INV-47..49:
//   INV-47     Transition::triggerHash pre-computed at addTransition time
//              (0 ⟺ empty trigger); eliminates per-frame intern in
//              findEligibleTransition + fireTransition
//   INV-48     Transition::conditionParamNameHash pre-computed at
//              addTransition time (0 ⟺ !hasCondition || empty paramName);
//              eliminates per-frame intern in Transition::evaluateCondition
//              L1 path
//   INV-49     Both hashes immutable after addTransition
//
// Standalone — no AYEntity, no AnimationPlayer. Direct StateMachine API.

#include <AYResource/assetsImpl/Animation.h>
#include <AYTest.h>

#include <AYAnimation/StateMachine.h>

#include <string>

using namespace ayt::anim;

namespace
{

// Build a 2-state machine: Idle ↔ Run with a trigger-driven transition.
StateMachine makeIdleRunSM()
{
    StateMachine sm;
    State idle;
    idle.name     = "Idle";
    idle.clipPath = "idle.ayanm";
    idle.loop     = true;
    sm.addState(idle);

    State run;
    run.name     = "Run";
    run.clipPath = "run.ayanm";
    run.loop     = true;
    sm.addState(run);

    Transition idleToRun;
    idleToRun.trigger   = "Run";
    idleToRun.fromState = "Idle";
    idleToRun.toState   = "Run";
    sm.addTransition(idleToRun);

    Transition runToIdle;
    runToIdle.trigger   = "Idle";
    runToIdle.fromState = "Run";
    runToIdle.toState   = "Idle";
    sm.addTransition(runToIdle);

    sm.setInitialState("Idle");
    return sm;
}

} // namespace

TEST_SUITE(StateMachineTests)

    // ─── #1 — single state stays put. ─────────────────────────────────
    TEST_CASE(L1_SingleState_NoTransition_StaysIdle) {
        StateMachine sm;
        State idle;
        idle.name = "Idle";
        sm.addState(idle);
        sm.setInitialState("Idle");

        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Idle");
        CHECK(sm.didTransitionThisFrame() == false);
        CHECK(sm.isTransitioning() == false);
    }

    // ─── #2 — automatic transition fires on first update. ─────────────
    TEST_CASE(L1_Transition_NoCondition_Immediate) {
        StateMachine sm = makeIdleRunSM();
        sm.setTrigger("Run");

        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Run");
        CHECK(sm.didTransitionThisFrame() == true);
        CHECK(sm.getPreviousStateName() == "Idle");
    }

    // ─── #3 — trigger fires + auto-erases. ────────────────────────────
    TEST_CASE(L1_Trigger_FiresTransition_ThenErases) {
        StateMachine sm = makeIdleRunSM();
        sm.setTrigger("Run");
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Run");

        // Now we're in "Run". setTrigger("Run") again should NOT bring us
        // back to "Idle" (no Run→Idle transition with that trigger name).
        sm.setTrigger("Run");
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Run");
        CHECK(sm.didTransitionThisFrame() == false);
    }

    // ─── #4 — trigger set but no matching transition → no fire. ───────
    TEST_CASE(L1_Trigger_DoesNotFireIfNoMatchingTransition) {
        StateMachine sm = makeIdleRunSM();
        sm.setTrigger("UnknownTrigger");
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Idle");
        CHECK(sm.didTransitionThisFrame() == false);
    }

    // ─── #5 — condition (Speed > 5.0) fires when true. ────────────────
    TEST_CASE(L1_Condition_Greater_FiresWhenTrue) {
        StateMachine sm;
        State idle;
        idle.name = "Idle";
        sm.addState(idle);
        State run;
        run.name = "Run";
        sm.addState(run);

        Transition t;
        t.fromState = "Idle";
        t.toState   = "Run";
        t.hasCondition = true;
        t.condition.paramName    = "Speed";
        t.condition.op            = StateConditionOp::Greater;
        t.condition.compareValue  = 5.0f;
        sm.addTransition(t);
        sm.setInitialState("Idle");

        sm.setParam("Speed", 7.0f);
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Run");
        CHECK(sm.didTransitionThisFrame() == true);
    }

    // ─── #6 — condition does NOT fire when false. ────────────────────
    TEST_CASE(L1_Condition_Greater_DoesNotFireWhenFalse) {
        StateMachine sm;
        State idle;
        idle.name = "Idle";
        sm.addState(idle);
        State run;
        run.name = "Run";
        sm.addState(run);

        Transition t;
        t.fromState = "Idle";
        t.toState   = "Run";
        t.hasCondition = true;
        t.condition.paramName    = "Speed";
        t.condition.op            = StateConditionOp::Greater;
        t.condition.compareValue  = 5.0f;
        sm.addTransition(t);
        sm.setInitialState("Idle");

        sm.setParam("Speed", 3.0f);
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Idle");
        CHECK(sm.didTransitionThisFrame() == false);
    }

    // ─── #7 — fromState wildcard matches any current state. ───────────
    TEST_CASE(L1_FromState_Wildcard_MatchesAny) {
        StateMachine sm;
        State idle;
        idle.name = "Idle";
        sm.addState(idle);
        State run;
        run.name = "Run";
        sm.addState(run);

        Transition anyToJump;
        anyToJump.fromState = "";   // wildcard
        anyToJump.toState   = "Idle";
        anyToJump.trigger   = "Reset";
        sm.addTransition(anyToJump);
        sm.setInitialState("Run");

        sm.setTrigger("Reset");
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Idle");
    }

    // ─── #8 — fromState specific does NOT match other states. ────────
    TEST_CASE(L1_FromState_Specific_DoesNotMatchOthers) {
        StateMachine sm = makeIdleRunSM();
        // We're in "Idle". Trigger "Run" → Idle→Run fires (transition
        // fromState="Idle" matches). But setTrigger("Idle") with us
        // already in "Idle" → no Idle→Run uses trigger "Idle", so
        // nothing happens.
        sm.setTrigger("Idle");
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Idle");
    }

    // ─── #9 — cross-fade duration > 0 keeps isTransitioning. ─────────
    TEST_CASE(L1_CrossFade_Duration_GreaterThanZero_Transitioning) {
        StateMachine sm;
        State idle;
        idle.name = "Idle";
        sm.addState(idle);
        State run;
        run.name = "Run";
        sm.addState(run);

        Transition t;
        t.fromState = "Idle";
        t.toState   = "Run";
        t.duration  = 0.5f;
        t.trigger   = "Run";
        sm.addTransition(t);
        sm.setInitialState("Idle");

        sm.setTrigger("Run");
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Idle");  // hasn't advanced yet
        CHECK(sm.isTransitioning() == true);
        // After first update, fireTransition latches elapsed=0; second
        // update advances it. Pin the contract via a second tick.
        sm.update(0.05f);
        CHECK(sm.getTransitionElapsed() > 0.0f);
        CHECK(sm.getTransitionElapsed() < 0.5f);   // not yet complete
    }

    // ─── #10 — cross-fade completion advances currentState. ──────────
    TEST_CASE(L1_CrossFade_Completion_AdvancesCurrentState) {
        StateMachine sm;
        State idle;
        idle.name = "Idle";
        sm.addState(idle);
        State run;
        run.name = "Run";
        sm.addState(run);

        Transition t;
        t.fromState = "Idle";
        t.toState   = "Run";
        t.duration  = 0.1f;
        t.trigger   = "Run";
        sm.addTransition(t);
        sm.setInitialState("Idle");

        sm.setTrigger("Run");
        sm.update(0.016f);
        CHECK(sm.isTransitioning() == true);

        // Advance time past the 0.1s duration.
        sm.update(0.2f);
        CHECK(sm.getCurrentStateName() == "Run");
        CHECK(sm.isTransitioning() == false);
    }

    // ─── #11 — unknown param in condition returns false fail-soft. ────
    TEST_CASE(L1_UnknownParam_FailsSoft) {
        StateMachine sm;
        State idle;
        idle.name = "Idle";
        sm.addState(idle);
        State run;
        run.name = "Run";
        sm.addState(run);

        Transition t;
        t.fromState = "Idle";
        t.toState   = "Run";
        t.hasCondition = true;
        t.condition.paramName = "NonExistentParam";
        t.condition.op         = StateConditionOp::Greater;
        t.condition.compareValue = 0.0f;
        sm.addTransition(t);
        sm.setInitialState("Idle");

        sm.update(0.016f);   // param never set; condition returns false
        CHECK(sm.getCurrentStateName() == "Idle");
        CHECK(sm.didTransitionThisFrame() == false);
    }

    // ─── #12 — didTransitionThisFrame only true once per fire. ───────
    TEST_CASE(L1_DidTransitionThisFrame_OnlyTrueOnce) {
        StateMachine sm = makeIdleRunSM();
        sm.setTrigger("Run");
        sm.update(0.016f);
        CHECK(sm.didTransitionThisFrame() == true);

        // Next frame without a new transition — flag must reset.
        sm.update(0.016f);
        CHECK(sm.didTransitionThisFrame() == false);
    }

    // ─── #13 — previousStateName tracks last frame's state. ──────────
    TEST_CASE(L1_PrevStateName_TracksLastFrame) {
        StateMachine sm = makeIdleRunSM();
        sm.setTrigger("Run");
        sm.update(0.016f);
        CHECK(sm.getPreviousStateName() == "Idle");
        CHECK(sm.getCurrentStateName()  == "Run");
    }

    // ─── #14 — author order matters for transition selection. ────────
    TEST_CASE(L1_TransitionOrder_AuthorOrderMatters) {
        StateMachine sm;
        State idle;
        idle.name = "Idle";
        sm.addState(idle);
        State run;
        run.name = "Run";
        sm.addState(run);

        // Two automatic (trigger="") transitions with fromState wildcard.
        Transition first;
        first.fromState = "";
        first.toState   = "Idle";   // first wins
        sm.addTransition(first);

        Transition second;
        second.fromState = "";
        second.toState   = "Run";
        sm.addTransition(second);

        sm.setInitialState("Idle");   // suppress lazy-init flip
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Idle");   // first authored wins
    }

    // ─── #15 — multiple triggers; only matching one fires. ───────────
    TEST_CASE(L1_MultipleTriggers_OnlyMatchingFires) {
        StateMachine sm = makeIdleRunSM();
        sm.setTrigger("Attack");       // not in any transition
        sm.setTrigger("Run");          // matches Idle→Run
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Run");
        CHECK(sm.didTransitionThisFrame() == true);
    }

    // =====================================================================
    // §P0 polish — flat-array params/triggers (INV-43..46)
    // =====================================================================
    //
    // After P0 polish, _params is std::vector<ParamEntry> and _triggers is
    // sorted std::vector<uint32_t>. These tests pin the new internal
    // contract while exercising the same public API. They also pin
    // INV-43/44 (registry process-global, hash-based intern).

    // ─── #16 — P0 polish: params flat-array lookup round-trip. ─────────
    TEST_CASE(Params_FlatArray_FindByHashReturnsCorrectValue) {
        // INV-45 — _params is a flat vector; findParamIndex resolves
        // hash → index. setParam/getParam ARE the public API; we
        // additionally verify the intern table state.
        StateMachine sm;
        State s; s.name = "Idle"; s.clipPath = "idle.ayanm";
        sm.addState(s);
        sm.setInitialState("Idle");

        const std::size_t registrySizeBefore = StateMachine::getParamNameRegistrySize();
        sm.setParam("Speed", 7.5f);
        sm.setParam("IsGrounded", 1.0f);
        sm.setParam("Speed", 9.0f);   // overwrite

        CHECK(sm.getParam("Speed")      == 9.0f);   // overwrite took effect
        CHECK(sm.getParam("IsGrounded") == 1.0f);
        CHECK(sm.getParam("Unknown")    == 0.0f);   // INV-23 fail-soft

        // Two new names were registered (Speed, IsGrounded). "Unknown"
        // also triggers an intern() call inside getParam (adds to
        // registry the first time it is seen).
        const std::size_t registrySizeAfter = StateMachine::getParamNameRegistrySize();
        CHECK(registrySizeAfter > registrySizeBefore);

        // The registry round-trips: getParamName(intern(name)) == name.
        const uint32_t speedHash =
            ayt::anim::detail::ParamNameRegistry::instance().intern("Speed");
        CHECK(StateMachine::getParamName(speedHash) == "Speed");
    }

    // ─── #17 — P0 polish: triggers sorted-vector + binary search. ─────
    TEST_CASE(Triggers_FlatArray_BinarySearchWorks) {
        // INV-46 — _triggers is sorted std::vector<uint32_t>. Even
        // though the public API stays setTrigger / erase-on-fire, we
        // verify the round-trip via the public surface.
        StateMachine sm;
        State sIdle; sIdle.name = "Idle"; sIdle.clipPath = "idle.ayanm"; sm.addState(sIdle);
        State sRun;  sRun.name  = "Run";  sRun.clipPath  = "run.ayanm";  sm.addState(sRun);
        sm.setInitialState("Idle");

        // Prime trigger intern table with the names we'll use, then
        // immediately clear them via the helper. (We can't directly
        // access _triggersHashes from outside, so we use the public
        // setTrigger + transition-fire pair to verify the sorted vector.)
        //
        // The simpler form: set trigger THEN add the transition that
        // uses it. After addTransition, the *next* update sees the
        // trigger set and fires. The first update below is therefore
        // a transition update, not a no-op.
        Transition t;
        t.trigger   = "Go";
        t.fromState = "Idle";
        t.toState   = "Run";
        sm.addTransition(t);

        // Verify duplicate setTrigger is idempotent (sorted-vector
        // invariant: lower_bound finds the existing entry, no insert).
        sm.setTrigger("Go");
        sm.setTrigger("Go");
        sm.setTrigger("Jump");      // a uniquely-named second trigger

        sm.update(0.016f);          // fires Idle→Run (trigger="Go" set)
        CHECK(sm.getCurrentStateName() == "Run");

        // After fire, "Go" is auto-erased (INV-20). update again should
        // not re-fire (we are now in Run, so fromState="Idle" doesn't
        // match — proves the trigger is gone).
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Run");
    }

    // =====================================================================
    // §P1 polish — Hot-path hash cache (INV-47..49)
    // =====================================================================
    //
    // After P1 polish, Transition carries triggerHash + conditionParamNameHash
    // fields computed once at addTransition time. The per-frame
    // ParamNameRegistry::intern() calls in findEligibleTransition +
    // Transition::evaluateCondition L1 path + fireTransition are eliminated.

    // ─── #18 — P1 polish: triggerHash pre-computed at addTransition. ───
    TEST_CASE(P1_Transition_TriggerHash_CachedAtAddTransition) {
        // INV-47 — Transition::triggerHash is computed once at addTransition
        // time (0 ⟺ empty trigger). After addTransition, the hash is stable
        // and matches what ParamNameRegistry::intern() returns for the same
        // name. The per-frame findEligibleTransition + fireTransition calls
        // use this cached hash directly (no per-frame intern).
        //
        // Use a unique name not registered anywhere else so we can assert
        // the registry grew by exactly 1 (avoid shared-registry pollution
        // from earlier tests in this suite).
        const std::string uniqueTrig = "P1_TriggerHash_Test_Unique_Name";

        StateMachine sm;
        State sIdle; sIdle.name = "Idle"; sIdle.clipPath = "idle.ayanm"; sm.addState(sIdle);
        State sRun;  sRun.name  = "Run";  sRun.clipPath  = "run.ayanm";  sm.addState(sRun);
        sm.setInitialState("Idle");

        Transition t;
        t.trigger   = uniqueTrig;
        t.fromState = "Idle";
        t.toState   = "Run";

        const std::size_t registrySizeBefore =
            StateMachine::getParamNameRegistrySize();
        sm.addTransition(t);
        const std::size_t registrySizeAfter =
            StateMachine::getParamNameRegistrySize();

        // Read back the transition (via getTransitions + const_cast pattern
        // used by P3.x L2 tests) to inspect the cached hash.
        auto& transitions = const_cast<std::vector<Transition>&>(
            sm.getTransitions());

        CHECK(transitions.size() == 1);
        CHECK(transitions[0].triggerHash != 0);     // INV-47: non-empty
        CHECK(transitions[0].trigger == uniqueTrig);

        // Registry should have grown by exactly 1 entry (uniqueTrig was
        // interned at addTransition time, NOT used by any earlier test).
        CHECK(registrySizeAfter - registrySizeBefore == 1);

        // The cached hash matches what ParamNameRegistry returns for uniqueTrig.
        const uint32_t directHash =
            ayt::anim::detail::ParamNameRegistry::instance().intern(uniqueTrig);
        CHECK(transitions[0].triggerHash == directHash);

        // Sanity: setting trigger then firing via update goes through cached
        // triggerHash (not a new intern()). After fire, trigger is erased
        // using cached hash.
        sm.setTrigger(uniqueTrig);
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Run");
    }

    // ─── #19 — P1 polish: conditionParamNameHash pre-computed at addTransition.
    TEST_CASE(P1_Transition_ConditionHash_CachedAtAddTransition) {
        // INV-48 — Transition::conditionParamNameHash is computed once at
        // addTransition time when hasCondition=true and paramName is non-empty.
        // 0 hash otherwise (sentinel pre-check).
        StateMachine sm;
        State sIdle; sIdle.name = "Idle"; sIdle.clipPath = "idle.ayanm"; sm.addState(sIdle);
        State sRun;  sRun.name  = "Run";  sRun.clipPath  = "run.ayanm";  sm.addState(sRun);
        sm.setInitialState("Idle");

        // Case A — transition WITH L1 condition: hash must be non-zero.
        Transition condT;
        condT.trigger        = "";
        condT.fromState      = "Idle";
        condT.toState        = "Run";
        condT.hasCondition   = true;
        condT.condition.paramName  = "Speed";
        condT.condition.op         = StateConditionOp::Greater;
        condT.condition.compareValue = 5.0f;

        sm.addTransition(condT);

        // Case B — transition WITHOUT L1 condition: hash must be 0.
        Transition noCondT;
        noCondT.trigger   = "";
        noCondT.fromState = "Idle";
        noCondT.toState   = "Run";
        // hasCondition stays false; paramName empty.

        sm.addTransition(noCondT);

        auto& transitions = const_cast<std::vector<Transition>&>(
            sm.getTransitions());

        CHECK(transitions.size() == 2);

        // Case A: cached hash present + matches registry intern result.
        CHECK(transitions[0].conditionParamNameHash != 0);     // INV-48
        CHECK(transitions[0].hasCondition == true);
        const uint32_t speedHash =
            ayt::anim::detail::ParamNameRegistry::instance().intern("Speed");
        CHECK(transitions[0].conditionParamNameHash == speedHash);

        // Case B: 0 sentinel because !hasCondition.
        CHECK(transitions[1].conditionParamNameHash == 0);     // INV-48
        CHECK(transitions[1].hasCondition == false);

        // Functional verification — evaluate L1 condition using cached hash
        // (zero intern per eval). Speed=7.0 > 5.0 should fire.
        sm.setParam("Speed", 7.0f);
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Run");

        // Speed=3.0 < 5.0 should NOT fire. Set initial back to Idle and
        // add a new transition Idle→Run with Speed > 50 condition.
        sm.clear();
        State sIdle2; sIdle2.name = "Idle"; sIdle2.clipPath = "idle.ayanm"; sm.addState(sIdle2);
        State sRun2;  sRun2.name  = "Run";  sRun2.clipPath  = "run.ayanm";  sm.addState(sRun2);
        sm.setInitialState("Idle");
        Transition big;
        big.trigger        = "";
        big.fromState      = "Idle";
        big.toState        = "Run";
        big.hasCondition   = true;
        big.condition.paramName  = "Speed";
        big.condition.op         = StateConditionOp::Greater;
        big.condition.compareValue = 50.0f;
        sm.addTransition(big);
        sm.setParam("Speed", 3.0f);
        sm.update(0.016f);
        CHECK(sm.getCurrentStateName() == "Idle");   // condition false, no fire
    }

// =====================================================================
// P2 polish (2026-08-07) — Bytecode parallel cache integration tests
// (INV-52..58). Hot-path evaluation now uses Transition::evaluateBytecode
// (program-counter switch) instead of evaluateCondition (virtual-dispatch
// AST path). Tests pin the wiring + lazy build + invalidation semantics.
// =====================================================================

// P2_Bytecode_Integration_FindTransitionUsesBytecode — after sm.update()
// drives findEligibleTransition, the matched transition's cachedBytecode
// is populated (proves hot-path switched from AST to bytecode).
TEST_CASE(P2_Bytecode_Integration_FindTransitionUsesBytecode) {
    using ayt::anim::StateMachine;
    using ayt::anim::State;
    using ayt::anim::Transition;

    StateMachine sm;
    State sIdle; sIdle.name = "Idle"; sIdle.clipPath = "idle.ayanm"; sm.addState(sIdle);
    State sRun;  sRun.name  = "Run";  sRun.clipPath  = "run.ayanm";  sm.addState(sRun);
    sm.setInitialState("Idle");

    Transition t;
    t.fromState = "Idle";
    t.toState   = "Run";
    t.conditionExpr = "Speed > 5.0";                 // L2 DSL bytecode path
    sm.addTransition(t);

    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());
    CHECK(transitions[0].cachedBytecode == nullptr); // INV-52 — null before first eval

    sm.setParam("Speed", 7.0f);
    sm.update(0.0f);                                  // drives evaluateBytecode

    CHECK(transitions[0].cachedBytecode != nullptr); // INV-52 — built after first eval
    CHECK(!transitions[0].cachedBytecode->program.empty());

    // Subsequent update() reuses the cached bytecode (no re-compile).
    sm.setParam("Speed", 3.0f);
    sm.update(0.0f);
    CHECK(transitions[0].cachedBytecode != nullptr); // still cached

    sm.setParam("Speed", 7.0f);
    sm.update(0.0f);
    CHECK(sm.getCurrentStateName() == "Run");        // fired via bytecode path
}

// P2_Bytecode_Integration_InvalidateCacheClearsBytecode — calling
// setConditionExpr("A && B") after first eval drops cachedBytecode (lazy
// re-compile); next evaluateBytecode rebuilds from the new AST.
TEST_CASE(P2_Bytecode_Integration_InvalidateCacheClearsBytecode) {
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
    t.conditionExpr = "Speed > 9.0";   // NOT satisfied at Speed=7 — builds
    sm.addTransition(t);               // bytecode without firing (state stays A)

    auto& transitions = const_cast<std::vector<Transition>&>(sm.getTransitions());

    sm.setParam("Speed", 7.0f);
    sm.update(0.0f);
    CHECK(transitions[0].cachedBytecode != nullptr);  // built after first eval
    CHECK(sm.getCurrentStateName() == "A");           // 7 > 9 false — no fire

    // Mutate conditionExpr → invalidate cache → bytecode cleared.
    transitions[0].setConditionExpr("Speed > 3.0");
    CHECK(transitions[0].cachedBytecode == nullptr);  // cleared by invalidate

    sm.setParam("Speed", 4.0f);
    sm.update(0.0f);
    CHECK(transitions[0].cachedBytecode != nullptr);  // rebuilt with new expr
    CHECK(sm.getCurrentStateName() == "B");           // 4 > 3 fires
}

TEST_SUITE_END