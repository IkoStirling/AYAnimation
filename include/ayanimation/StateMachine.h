// StateMachine.h — P3.1 (2026-08-06) L1 简单状态机 + P3.2 (2026-08-06) L3 子状态机
//                  + P3.x  (2026-08-07) L2 Condition DSL (string expr + lazy parse +
//                  dirty cache).
//
// Mirrors UE UAnimStateMachine shape (subset — L1 + L3; L4 MotionMatching /
// multi-graph / BlendTree inside SM / parallel states deferred). Standalone
// class — NOT an AnimationPlayer subclass; composition: AnimationPlayer
// plays the clip, StateMachine decides which clip. AYEntity
// StateMachineSystem bridges the two.
//
// INV-18..26 contracts (P3.1 L1):
//   * INV-18 — single current state at any moment after update()
//   * INV-19 — didTransitionThisFrame true iff state changed in update()
//   * INV-20 — trigger fires once on first eligible transition, auto-erases
//   * INV-21 — fromState="" matches any; trigger="" means automatic
//   * INV-22 — cross-fade: currentState updates only when duration elapses
//   * INV-23 — unknown param in condition returns false (fail-soft)
//   * INV-24 — addTransition rejects unknown toState (debug assert)
//   * INV-25 — StateMachineSystem runs at priority 460 (after AnimSystem 450)
//   * INV-26 — System pushes new clip via player.play(clip) on transition
//
// INV-27..31 contracts (P3.2 L3):
//   * INV-27 — State.isSubMachine=true ⇒ State.clipPath is IGNORED (host
//              MUST NOT call player.play() for sub-machine entry; child SM owns
//              clip selection via its own transitions)
//   * INV-28 — setTrigger / setParam propagate into the ACTIVE child SM only
//              (not all children); UE-pattern sharing parent→child signals
//   * INV-29 — Transition fallback order: child first, then parent; if
//              child's findEligibleTransition returns non-null, parent's
//              check is skipped that frame (no race)
//   * INV-30 — getActiveLeafStateName() returns the deepest current state;
//              recursion depth ≤ 2 (root → child only in P3.2; deeper
//              recursion is not error but behavior undefined)
//   * INV-31 — When entering a sub-machine entry state, _currentChildIndex
//              is set BEFORE next update(); when leaving (parent transition
//              fires from a sub-machine entry to non-sub-machine state),
//              _currentChildIndex is reset to -1

#pragma once

#include <ayanimation/ConditionExpr.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ayt::anim
{

// L1 condition operator. L2 upgrade to expression DSL — deferred.
enum class StateConditionOp : uint8_t {
    Greater   = 0,
    Less      = 1,
    Equals    = 2,
    NotEqual  = 3,
};

struct StateCondition {
    std::string       paramName;
    StateConditionOp  op            = StateConditionOp::Greater;
    float             compareValue  = 0.0f;
};

// One state in the graph. POD; mirrors UE UAnimState shape (subset).
struct State {
    std::string name;             // unique; used by Transition::fromState / toState
    std::string clipPath;         // AYResource path for IAnimation (.ayanm)
    bool        loop              = true;
    float       playRate          = 1.0f;
    float       entryTime         = 0.0f;
    float       exitTime          = 0.0f;

    // === P3.2 NEW (sub-machine entry) ===
    // When isSubMachine=true, the state represents an entry to a sub-graph
    // (added through StateMachine::addSubMachine). clipPath / loop /
    // playRate / entryTime / exitTime are IGNORED; the sub-machine drives
    // its own clip selection via its own transitions.
    bool        isSubMachine      = false;
    int         subMachineIndex   = -1;     // -1 if not a sub-machine
};

// Transition between states. POD + P3.x L2 DSL cache layer.
//
// L1 path (back-compat, INV-23..26 preserved): hasCondition=true + condition
// holds the single-predicate gate. Used when conditionExpr is empty.
//
// L2 path (P3.x NEW, INV-32..35): a non-empty conditionExpr is parsed lazily
// into cachedAst the first time evaluateCondition runs. The cache is dirty
// by default (conditionDirty=true) and is invalidated whenever the source
// string changes (via setConditionExpr, which auto-flags dirty) or via an
// explicit invalidateConditionCache() call.
//
// On parse failure (INV-33): cachedAst stays nullptr, conditionParseError
// captures the first diagnostic, the evaluate returns false unconditionally
// (transition never fires) — no assert, no throw, no crash.
struct Transition {
    std::string    trigger;        // "" = automatic (fires when condition true)
    std::string    fromState;      // "" = ANY (from any state)
    std::string    toState;        // must match an existing State::name
    float          duration        = 0.0f;   // 0 = instant cut; >0 = cross-fade
    bool           hasCondition    = false;
    StateCondition condition;

    // === P3.x L2 NEW — DSL source + cache layer ===
    std::string conditionExpr;                 // source string; "" = no L2 condition
    // shared_ptr (not unique_ptr) because Transition itself lives in
    // std::vector<Transition> via push_back (copy required). shared_ptr
    // retains single-owner RAII semantics for the AST; the cache field is
    // mutable to allow lazy init in const evaluateCondition().
    mutable std::shared_ptr<CondExprAst> cachedAst;       // lazy parse result
    mutable bool      conditionDirty    = true;          // first evaluate triggers parse
    mutable std::string conditionParseError;             // last parse diagnostic ("" = OK)

    // Setter: replaces conditionExpr and auto-flags dirty (INV-34). Skips
    // assignment when the new string is identical to the current one.
    void setConditionExpr(std::string s);

    // Explicit invalidation. Use after mutating conditionExpr in place (not
    // recommended) or from editor / hot-reload flows.
    void invalidateConditionCache();

    // Unified entry point. When conditionExpr is non-empty, routes through
    // the L2 path (lazy parse + cache). When empty, falls back to L1
    // single-predicate semantics (or true if hasCondition is also false —
    // INV-32). Parse failures (INV-33) return false.
    bool evaluateCondition(const ConditionEvalCtx& ctx) const;
};

class StateMachine {
public:
    StateMachine();
    ~StateMachine() = default;

    // P3.2 NEW — non-copyable (vector<unique_ptr<StateMachine>> as member
    // forbids implicit copy; make it explicit to silence C2672 in callers).
    StateMachine(const StateMachine&) = delete;
    StateMachine& operator=(const StateMachine&) = delete;
    StateMachine(StateMachine&&) = default;
    StateMachine& operator=(StateMachine&&) = default;

    // === Authoring API ===
    void addState(const State& s);
    void addTransition(const Transition& t);
    void setInitialState(const std::string& name);
    void clear();

    // === Runtime API ===
    void update(float dt);
    void setTrigger(const std::string& name);

    // === Parameters (L1 condition eval) ===
    void  setParam(const std::string& name, float value);
    float getParam(const std::string& name) const;

    // === Read-back ===
    const std::string& getCurrentStateName()  const { return _currentState; }
    const std::string& getPreviousStateName() const { return _prevStateName; }
    bool  isTransitioning() const { return _transitioning; }
    float getTransitionElapsed() const { return _transitionElapsed; }
    std::size_t getStateCount() const { return _states.size(); }
    std::size_t getTransitionCount() const { return _transitions.size(); }

    // === Internal hooks ===
    bool didTransitionThisFrame() const { return _transitionedThisFrame; }

    // === Debugging / serialization ===
    const std::vector<State>&      getStates()      const { return _states; }
    const std::vector<Transition>& getTransitions() const { return _transitions; }

    // === Sub-state machine API (P3.2 NEW) ===
    // Takes ownership of `sm` and appends to `_children`. Returns the
    // assigned child index. Caller must NOT touch `sm` after this call.
    int addSubMachine(std::unique_ptr<StateMachine> sm);

    // Read-back: -1 if no active child.
    int getCurrentChildIndex() const { return _currentChildIndex; }

    // Lookup: returns nullptr if `idx` is out of range or invalid.
    StateMachine*       getSubMachine(int idx);
    const StateMachine* getSubMachine(int idx) const;

    // Active child (when _currentChildIndex >= 0) or nullptr.
    StateMachine*       getActiveSubMachine();
    const StateMachine* getActiveSubMachine() const;

    // Deepest leaf state name across the active path. Returns parent's
    // _currentState when no active child (INV-30). When the active child
    // has not yet been ticked (lazy-init pending), returns the parent's
    // _currentState until the next update().
    std::string getActiveLeafStateName() const;

    // Sub-machine entry count (size of _children).
    std::size_t getSubMachineCount() const { return _children.size(); }

private:
    bool evaluateCondition(const StateCondition& c) const;
    const Transition* findEligibleTransition() const;
    void fireTransition(const Transition& t);

    // P3.2 NEW: helper that returns the sub-machine index of `stateName`
    // if it is a sub-machine entry state, else -1.
    int findSubMachineIndex(const std::string& stateName) const;

    // P3.2 NEW: like findEligibleTransition but routes through the active
    // child first. Used by parent's update() for child-first fallback
    // (INV-29). When no active child, falls back to self.
    const Transition* findEligibleTransitionForSelf() const;

    // P3.2 NEW: returns the active child pointer or nullptr; bounds-checked.
    StateMachine*       activeChild();
    const StateMachine* activeChild() const;

    std::vector<State>       _states;
    std::vector<Transition>  _transitions;
    std::unordered_map<std::string, std::size_t> _stateIndexByName;
    std::unordered_set<std::string>              _triggers;
    std::unordered_map<std::string, float>       _params;
    std::string _initialState;
    std::string _currentState;
    std::string _prevStateName;
    bool   _transitioning          = false;
    float  _transitionElapsed      = 0.0f;
    float  _transitionDuration     = 0.0f;
    std::string _pendingToState;
    bool   _transitionedThisFrame  = false;
    bool   _initialized            = false;

    // === P3.2 NEW fields ===
    std::vector<std::unique_ptr<StateMachine>> _children;
    int  _currentChildIndex = -1;          // -1 = no active child
};

} // namespace ayt::anim