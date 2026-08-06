// StateMachine.cpp — P3.1 (2026-08-06) L1 简单状态机 implementation.

#include <ayanimation/StateMachine.h>
#include <algorithm>
#include <cassert>

namespace ayt::anim
{

StateMachine::StateMachine() = default;

void StateMachine::addState(const State& s) {
    assert(!s.name.empty() && "StateMachine::addState: empty state name");
    assert(_stateIndexByName.count(s.name) == 0 &&
           "StateMachine::addState: duplicate state name");
    _stateIndexByName[s.name] = _states.size();
    _states.push_back(s);
}

void StateMachine::addTransition(const Transition& t) {
    // INV-24 — debug-only assert; runtime findEligibleTransition is forgiving.
    assert(!t.toState.empty() &&
           "StateMachine::addTransition: empty toState");
    assert(_stateIndexByName.count(t.toState) > 0 &&
           "StateMachine::addTransition: toState not in state list");
    if (!t.fromState.empty()) {
        assert(_stateIndexByName.count(t.fromState) > 0 &&
               "StateMachine::addTransition: fromState not in state list");
    }
    _transitions.push_back(t);
}

void StateMachine::setInitialState(const std::string& name) {
    assert(_stateIndexByName.count(name) > 0 &&
           "StateMachine::setInitialState: name not in state list");
    _initialState = name;
    _currentState = name;
    _prevStateName = name;
    _initialized = true;
}

void StateMachine::clear() {
    _states.clear();
    _transitions.clear();
    _stateIndexByName.clear();
    _triggers.clear();
    _params.clear();
    _initialState.clear();
    _currentState.clear();
    _prevStateName.clear();
    _pendingToState.clear();
    _transitioning = false;
    _transitionElapsed = 0.0f;
    _transitionDuration = 0.0f;
    _transitionedThisFrame = false;
    _initialized = false;
}

void StateMachine::setTrigger(const std::string& name) {
    if (!name.empty()) {
        _triggers.insert(name);
    }
}

void StateMachine::setParam(const std::string& name, float value) {
    if (!name.empty()) {
        _params[name] = value;
    }
}

float StateMachine::getParam(const std::string& name) const {
    auto it = _params.find(name);
    return (it != _params.end()) ? it->second : 0.0f;
}

void StateMachine::update(float dt) {
    _transitionedThisFrame = false;

    // INV-18 — empty state machine is a no-op.
    if (_states.empty()) return;

    // Lazy-init: if user never called setInitialState, take the first
    // added state as the initial. Mirrors UE Entry Node behavior.
    if (!_initialized) {
        _currentState = _states.front().name;
        _prevStateName = _currentState;
        _initialized = true;
    }

    // (1) Advance transition clock if mid-transition.
    if (_transitioning) {
        _transitionElapsed += dt;
        if (_transitionElapsed >= _transitionDuration) {
            // INV-22 — transition complete; currentState advances.
            _prevStateName = _currentState;
            _currentState = _pendingToState;
            _pendingToState.clear();
            _transitioning = false;
            _transitionElapsed = 0.0f;
            _transitionDuration = 0.0f;
            _transitionedThisFrame = true;
        }
        // No new transitions during the transition window (UE rule).
        return;
    }

    // (2) Look for an eligible transition from the current state.
    const Transition* t = findEligibleTransition();
    if (t != nullptr) {
        fireTransition(*t);
        _transitionedThisFrame = true;
    }
}

bool StateMachine::evaluateCondition(const StateCondition& c) const {
    auto it = _params.find(c.paramName);
    if (it == _params.end()) {
        // INV-23 — unknown param fail-soft (matches P2.2 ISkeletonMask).
        return false;
    }
    const float v = it->second;
    switch (c.op) {
        case StateConditionOp::Greater:   return v >  c.compareValue;
        case StateConditionOp::Less:      return v <  c.compareValue;
        case StateConditionOp::Equals:    return std::fabs(v - c.compareValue) <  1e-6f;
        case StateConditionOp::NotEqual:  return std::fabs(v - c.compareValue) >= 1e-6f;
    }
    return false;
}

const Transition* StateMachine::findEligibleTransition() const {
    // Author order matters — first match wins.
    for (const auto& t : _transitions) {
        // INV-21 — fromState=="" wildcard; trigger=="" automatic.
        const bool fromMatches =
            t.fromState.empty() || t.fromState == _currentState;
        if (!fromMatches) continue;

        const bool triggerOk =
            t.trigger.empty() ? true : (_triggers.count(t.trigger) > 0);
        if (!triggerOk) continue;

        if (t.hasCondition && !evaluateCondition(t.condition)) continue;

        return &t;
    }
    return nullptr;
}

void StateMachine::fireTransition(const Transition& t) {
    // INV-19 — prevStateName captures the state we're leaving.
    _prevStateName = _currentState;
    if (t.duration <= 0.0f) {
        // Instant cut.
        _currentState = t.toState;
        _pendingToState.clear();
        _transitioning = false;
        _transitionDuration = 0.0f;
        _transitionElapsed = 0.0f;
    } else {
        // INV-22 — cross-fade: latch pendingToState; currentState updates
        // only when duration elapses (StateMachine::update step 1).
        _pendingToState = t.toState;
        _transitioning = true;
        _transitionDuration = t.duration;
        _transitionElapsed = 0.0f;
    }
    // INV-20 — trigger consumed on first eligible transition.
    if (!t.trigger.empty()) {
        _triggers.erase(t.trigger);
    }
}

} // namespace ayt::anim