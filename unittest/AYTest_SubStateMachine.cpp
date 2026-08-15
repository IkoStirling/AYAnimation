// AYTest_SubStateMachine.cpp — P3.2 (2026-08-06) L3 子状态机 unit tests.
//
// 12 cases pinning INV-27..31 contracts from design.md §4.15:
//   INV-27 State.isSubMachine=true ⇒ State.clipPath is ignored
//   INV-28 setTrigger / setParam propagate into ACTIVE child only
//   INV-29 Transition fallback: child first, then parent
//   INV-30 getActiveLeafStateName returns deepest leaf (depth ≤ 2)
//   INV-31 _currentChildIndex set/reset on entry/exit of sub-machine state
//
// Standalone — no AYEntity, no AnimationPlayer. Direct StateMachine API.

#include <AYResource/assetsImpl/Animation.h>
#include <AYTest.h>

#include <AYAnimation/StateMachine.h>

#include <memory>
#include <string>

using namespace ayt::anim;

namespace
{

// Build a child locomotion SM (Idle ↔ Walk ↔ Run with trigger "Run"/"Walk").
std::unique_ptr<StateMachine> makeLocomotionChild()
{
    auto child = std::make_unique<StateMachine>();
    State s_idle;  s_idle.name = "Idle";  s_idle.clipPath = "idle.ayanm";
    State s_walk;  s_walk.name = "Walk";  s_walk.clipPath = "walk.ayanm";
    State s_run;   s_run.name  = "Run";   s_run.clipPath  = "run.ayanm";
    child->addState(s_idle);
    child->addState(s_walk);
    child->addState(s_run);

    Transition t1; t1.fromState = "Idle"; t1.toState = "Walk"; t1.trigger = "Walk";
    Transition t2; t2.fromState = "Walk"; t2.toState = "Run";  t2.trigger = "Run";
    Transition t3; t3.fromState = "Run";  t3.toState = "Idle"; t3.trigger = "Stop";
    child->addTransition(t1);
    child->addTransition(t2);
    child->addTransition(t3);

    child->setInitialState("Idle");
    return child;
}

// Build a root SM that has a "Move" sub-machine entry state pointing to a
// locomotion SM (passed in already constructed). StateMachine is
// non-copyable (P3.2 NEW), so caller must use std::move from a unique_ptr.
std::unique_ptr<StateMachine> makeRootWithMoveEntryUPtr(std::unique_ptr<StateMachine> loco)
{
    auto root = std::make_unique<StateMachine>();

    State s_idle;
    s_idle.name     = "Idle";
    s_idle.clipPath = "root_idle.ayanm";

    State s_move;
    s_move.name           = "Move";
    s_move.isSubMachine   = true;
    // subMachineIndex must be set BEFORE addState because addState asserts
    // a non-negative index when isSubMachine=true. addSubMachine returns
    // the index of the just-pushed child, so we addSubMachine first, then
    // assign s_move.subMachineIndex from that index, then addState.
    // Note: locoIdx will be used by the second addState below.
    root->addState(s_idle);
    const int locoIdx = root->addSubMachine(std::move(loco));
    s_move.subMachineIndex = locoIdx;
    root->addState(s_move);

    Transition idleToMove;
    idleToMove.fromState = "Idle";
    idleToMove.toState   = "Move";
    idleToMove.trigger   = "Move";
    Transition moveToIdle;
    moveToIdle.fromState = "Move";
    moveToIdle.toState   = "Idle";
    moveToIdle.trigger   = "Stop";
    root->addTransition(idleToMove);
    root->addTransition(moveToIdle);

    root->setInitialState("Idle");
    return root;
}

} // namespace

TEST_SUITE(SubStateMachineTests)

    // ─── #1 — addSubMachine stores and returns index. ────────────────
    TEST_CASE(L3_AddSubMachine_StoresAndReturnsIndex) {
        StateMachine root;
        auto childA = std::make_unique<StateMachine>();
        auto childB = std::make_unique<StateMachine>();
        const int idxA = root.addSubMachine(std::move(childA));
        const int idxB = root.addSubMachine(std::move(childB));
        CHECK(idxA == 0);
        CHECK(idxB == 1);
        CHECK(root.getSubMachineCount() == 2u);
        CHECK(root.getSubMachine(idxA) != nullptr);
        CHECK(root.getSubMachine(idxB) != nullptr);
        CHECK(root.getSubMachine(2) == nullptr);   // out of range
        CHECK(root.getSubMachine(-1) == nullptr);  // negative
        CHECK(root.getActiveSubMachine() == nullptr);  // no child active yet
        CHECK(root.getCurrentChildIndex() == -1);
    }

    // ─── #2 — setTrigger propagates to ACTIVE child. ─────────────────
    TEST_CASE(L3_SetTrigger_PropagatesToActiveChild) {
        auto child = makeLocomotionChild();
        // Capture the child pointer before move.
        StateMachine* childPtr = child.get();
        auto root = makeRootWithMoveEntryUPtr(std::move(child));

        // Enter Move (sub-machine entry) — set Trigger "Move".
        root->setTrigger("Move");
        root->update(0.0f);
        CHECK(root->getCurrentStateName() == "Move");
        CHECK(root->getActiveSubMachine() == childPtr);
        CHECK(root->getCurrentChildIndex() >= 0);

        // Now set "Walk" on root — should propagate to active child.
        // (Child's Idle→Walk transition uses trigger="Walk".)
        root->setTrigger("Walk");
        // We must check by ticking again — the child's transition fires.
        root->update(0.016f);
        CHECK(childPtr->getCurrentStateName() == "Walk");
    }

    // ─── #3 — setParam propagates to ACTIVE child. ───────────────────
    TEST_CASE(L3_SetParam_PropagatesToActiveChild) {
        auto child = makeLocomotionChild();
        StateMachine* childPtr = child.get();
        auto root = makeRootWithMoveEntryUPtr(std::move(child));

        // Activate child by triggering Move.
        root->setTrigger("Move");
        root->update(0.0f);
        CHECK(root->getCurrentStateName() == "Move");

        // Now setParam at root — child should see it.
        root->setParam("Speed", 7.0f);
        // The child should have received "Speed" = 7.0f.
        CHECK(childPtr->getParam("Speed") == 7.0f);
        // (No CHECK for fallback propagation to parent — irrelevant for L3.)
    }

    // ─── #4 — transition into sub-machine entry activates child. ─────
    TEST_CASE(L3_EnterSubMachineEntry_ActivatesChild) {
        auto child = makeLocomotionChild();
        StateMachine* childPtr = child.get();
        auto root = makeRootWithMoveEntryUPtr(std::move(child));

        CHECK(root->getCurrentChildIndex() == -1);  // start: no child

        root->setTrigger("Move");
        root->update(0.0f);
        CHECK(root->getCurrentStateName() == "Move");
        CHECK(root->getCurrentChildIndex() >= 0);
        CHECK(root->getActiveSubMachine() == childPtr);
    }

    // ─── #5 — child SM transition advances child's currentState. ──────
    TEST_CASE(L3_SubMachineTransition_AdvancesChildCurrentState) {
        auto child = makeLocomotionChild();
        StateMachine* childPtr = child.get();
        auto root = makeRootWithMoveEntryUPtr(std::move(child));

        root->setTrigger("Move");
        root->update(0.0f);                 // → Move + activate child
        CHECK(root->getCurrentStateName() == "Move");
        CHECK(childPtr->getCurrentStateName() == "Idle");

        root->setTrigger("Walk");
        root->update(0.016f);               // child: Idle→Walk
        CHECK(childPtr->getCurrentStateName() == "Walk");
        CHECK(root->getCurrentStateName()    == "Move");   // parent unchanged

        root->setTrigger("Run");
        root->update(0.016f);               // child: Walk→Run
        CHECK(childPtr->getCurrentStateName() == "Run");
        CHECK(root->getCurrentStateName()    == "Move");
    }

    // ─── #6 — fallback to parent when child has no eligible. ─────────
    TEST_CASE(L3_ChildTransition_FallbackToParent_WhenChildNoMatch) {
        auto child = makeLocomotionChild();
        StateMachine* childPtr = child.get();
        auto root = makeRootWithMoveEntryUPtr(std::move(child));

        // Activate Move.
        root->setTrigger("Move");
        root->update(0.0f);
        CHECK(root->getCurrentStateName() == "Move");
        CHECK(childPtr->getCurrentStateName() == "Idle");

        // Set a trigger on root that the parent recognizes ("Stop" → Move→Idle)
        // but the child does NOT recognize.
        root->setTrigger("Stop");
        root->update(0.016f);

        // Parent transition should have fired (Move→Idle); child deactivated.
        CHECK(root->getCurrentStateName() == "Idle");
        CHECK(root->getCurrentChildIndex() == -1);
        CHECK(root->getActiveSubMachine() == nullptr);
    }

    // ─── #7 — leaving sub-machine entry deactivates child. ───────────
    TEST_CASE(L3_ExitSubMachine_DeactivatesChild) {
        auto child = makeLocomotionChild();
        auto root = makeRootWithMoveEntryUPtr(std::move(child));

        root->setTrigger("Move");
        root->update(0.0f);
        CHECK(root->getCurrentStateName() == "Move");
        CHECK(root->getActiveSubMachine() != nullptr);

        // Stop → Move → Idle.
        root->setTrigger("Stop");
        root->update(0.016f);
        CHECK(root->getCurrentStateName() == "Idle");
        CHECK(root->getCurrentChildIndex() == -1);
        CHECK(root->getActiveSubMachine() == nullptr);
    }

    // ─── #8 — getActiveLeafStateName returns child currentState. ─────
    TEST_CASE(L3_GetActiveLeafStateName_ReturnsChildCurrentState) {
        auto child = makeLocomotionChild();
        auto root = makeRootWithMoveEntryUPtr(std::move(child));

        // Before Move — leaf = parent's currentState.
        CHECK(root->getActiveLeafStateName() == "Idle");

        // After entering Move — leaf = child's currentState.
        root->setTrigger("Move");
        root->update(0.0f);
        CHECK(root->getActiveLeafStateName() == "Idle");  // child just lazy-init'd

        // After child transition — leaf = child's new state.
        root->setTrigger("Walk");
        root->update(0.016f);
        CHECK(root->getActiveLeafStateName() == "Walk");
    }

    // ─── #9 — getActiveLeafStateName returns parent state when no child.
    TEST_CASE(L3_GetActiveLeafStateName_NoChild_ReturnsParentState) {
        StateMachine root;
        State a; a.name = "A";
        State b; b.name = "B";
        root.addState(a);
        root.addState(b);
        Transition t; t.fromState = "A"; t.toState = "B"; t.trigger = "Go";
        root.addTransition(t);
        root.setInitialState("A");

        CHECK(root.getActiveLeafStateName() == "A");

        root.setTrigger("Go");
        root.update(0.016f);
        CHECK(root.getActiveLeafStateName() == "B");
    }

    // ─── #10 — dt plumbing advances child cross-fade clock. ──────────
    TEST_CASE(L3_DtPlumbing_ChildCrossFadeAdvances) {
        auto child = std::make_unique<StateMachine>();
        State s_a; s_a.name = "A";
        State s_b; s_b.name = "B";
        child->addState(s_a);
        child->addState(s_b);
        Transition t; t.fromState = "A"; t.toState = "B"; t.trigger = "Go"; t.duration = 0.5f;
        child->addTransition(t);
        child->setInitialState("A");

        StateMachine* childPtr = child.get();
        auto root = std::make_unique<StateMachine>();
        State r_a; r_a.name = "A";
        root->addState(r_a);
        const int cIdx = root->addSubMachine(std::move(child));
        State r_b;
        r_b.name           = "B";
        r_b.isSubMachine   = true;
        r_b.subMachineIndex = cIdx;
        root->addState(r_b);
        Transition rt; rt.fromState = "A"; rt.toState = "B"; rt.trigger = "Enter";
        root->addTransition(rt);
        root->setInitialState("A");

        // Activate child B (sub-machine entry state).
        root->setTrigger("Enter");
        root->update(0.016f);                 // A→B instant, child activated
        CHECK(root->getCurrentStateName() == "B");
        CHECK(childPtr->isTransitioning() == false);

        // First update: child fires A→B with duration=0.5; the fire sets
        // _transitioning=true + _transitionElapsed=0 (same-frame semantics
        // — INV-22; clock advance is deferred to the NEXT update).
        childPtr->setTrigger("Go");
        root->update(0.1f);
        CHECK(childPtr->isTransitioning() == true);
        CHECK(childPtr->getTransitionElapsed() == 0.0f);  // same-frame fire

        // Second update: dt advances the cross-fade clock. If dt plumbing
        // were stubbed (P3.1 behavior, sm.update(0.0f)), elapsed would
        // stay at 0 — that's what this case pins.
        root->update(0.1f);
        CHECK(childPtr->isTransitioning() == true);
        CHECK(childPtr->getTransitionElapsed() > 0.0f);
        CHECK(childPtr->getTransitionElapsed() <= 0.1f);
    }

    // ─── #11 — sub-machine entry state: clipPath is ignored. ─────────
    TEST_CASE(L3_SubMachineState_ClipPathIgnored) {
        // Construct a state with both isSubMachine=true AND a non-empty
        // clipPath. The class itself doesn't enforce INV-27 — it's a
        // contract that the host (ECS bridge) MUST honor. Here we verify
        // the data is stored as-is but that the active child resolution
        // takes precedence.
        StateMachine root;
        auto child = std::make_unique<StateMachine>();
        State c_idle; c_idle.name = "CIdle";
        child->addState(c_idle);
        child->setInitialState("CIdle");
        const int cIdx = root.addSubMachine(std::move(child));

        State s; s.name = "Move";
        s.isSubMachine    = true;
        s.subMachineIndex = cIdx;
        s.clipPath        = "this/should/be/ignored.ayanm";
        root.addState(s);
        root.setInitialState("Move");

        // Confirm state stored with both fields.
        bool foundMove = false;
        for (const auto& rs : root.getStates()) {
            if (rs.name == "Move") {
                foundMove = true;
                CHECK(rs.isSubMachine == true);
                CHECK(rs.clipPath == "this/should/be/ignored.ayanm");
                CHECK(rs.subMachineIndex == cIdx);
            }
        }
        CHECK(foundMove);

        // Active child is set; leaf name comes from child, not parent clipPath.
        CHECK(root.getActiveSubMachine() != nullptr);
        CHECK(root.getActiveLeafStateName() == "CIdle");
    }

    // ─── #12 — clear() recursively destroys children. ────────────────
    TEST_CASE(L3_Clear_RecursivelyClearsChildren) {
        StateMachine root;
        auto child = makeLocomotionChild();
        StateMachine* childPtr = child.get();
        root.addSubMachine(std::move(child));
        CHECK(root.getSubMachineCount() == 1u);

        root.clear();
        CHECK(root.getSubMachineCount() == 0u);
        CHECK(root.getActiveSubMachine() == nullptr);
        CHECK(root.getCurrentChildIndex() == -1);
        // The child pointer is now dangling (child was unique_ptr inside root).
        // We can only verify root-level invariants; the child object has been
        // destructed by unique_ptr.
        (void)childPtr;
    }

TEST_SUITE_END