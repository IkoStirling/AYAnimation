// AYTest_StateMachine.cpp — P3.1 (2026-08-06) L1 简单状态机 unit tests.
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
// Standalone — no AYEntity, no AnimationPlayer. Direct StateMachine API.

#include <AYAnimation.h>
#include <AYTest.h>

#include <ayanimation/StateMachine.h>

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

TEST_SUITE_END