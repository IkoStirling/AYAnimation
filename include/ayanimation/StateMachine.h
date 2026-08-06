// StateMachine.h — P3.1 (2026-08-06) L1 简单状态机.
//
// Mirrors UE UAnimStateMachine shape (subset — L1 only; L3 nested / L4
// MotionMatching deferred). Standalone class — NOT an AnimationPlayer
// subclass; composition: AnimationPlayer plays the clip, StateMachine
// decides which clip. AYEntity StateMachineSystem bridges the two.
//
// INV-18..26 contracts:
//   * INV-18 — single current state at any moment after update()
//   * INV-19 — didTransitionThisFrame true iff state changed in update()
//   * INV-20 — trigger fires once on first eligible transition, auto-erases
//   * INV-21 — fromState="" matches any; trigger="" means automatic
//   * INV-22 — cross-fade: currentState updates only when duration elapses
//   * INV-23 — unknown param in condition returns false (fail-soft)
//   * INV-24 — addTransition rejects unknown toState (debug assert)
//   * INV-25 — StateMachineSystem runs at priority 460 (after AnimSystem 450)
//   * INV-26 — System pushes new clip via player.play(clip) on transition

#pragma once

#include <cmath>
#include <cstddef>
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

// One state in the graph. POD.
struct State {
    std::string name;             // unique; used by Transition::fromState / toState
    std::string clipPath;         // AYResource path for IAnimation (.ayanm)
    bool        loop              = true;
    float       playRate          = 1.0f;
    float       entryTime         = 0.0f;
    float       exitTime          = 0.0f;
};

// Transition between states. POD.
struct Transition {
    std::string    trigger;        // "" = automatic (fires when condition true)
    std::string    fromState;      // "" = ANY (from any state)
    std::string    toState;        // must match an existing State::name
    float          duration        = 0.0f;   // 0 = instant cut; >0 = cross-fade
    bool           hasCondition    = false;
    StateCondition condition;
};

class StateMachine {
public:
    StateMachine();
    ~StateMachine() = default;

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

private:
    bool evaluateCondition(const StateCondition& c) const;
    const Transition* findEligibleTransition() const;
    void fireTransition(const Transition& t);

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
};

} // namespace ayt::anim