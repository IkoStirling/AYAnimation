// StateMachine.cpp — P3.1 (2026-08-06) L1 简单状态机 implementation +
// P3.2 (2026-08-06) L3 子状态机 extensions (sub-machine entry + recursive
// setTrigger/setParam + child-first transition fallback + active child
// resolution).

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
    // P3.2 — caller MAY set isSubMachine=true before subMachineIndex is
    // assigned (typical pattern: addSubMachine first, then patch state via
    // getStates(), then addState). We don't assert here; findSubMachineIndex
    // returns -1 when the index is invalid, and setInitialState / update()
    // silently skip activation in that case.
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

    // P3.2 NEW — if the initial state is a sub-machine entry, activate the
    // child immediately so the first update() ticks it.
    _currentChildIndex = findSubMachineIndex(name);
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

    // P3.2 NEW — recursively destroy sub-machines (unique_ptr dtor handles).
    _children.clear();
    _currentChildIndex = -1;
}

void StateMachine::setTrigger(const std::string& name) {
    if (name.empty()) return;
    _triggers.insert(name);

    // P3.2 NEW (INV-28) — propagate into active child only.
    if (auto* child = activeChild()) {
        child->setTrigger(name);
    }
}

void StateMachine::setParam(const std::string& name, float value) {
    if (name.empty()) return;
    _params[name] = value;

    // P3.2 NEW (INV-28) — propagate into active child only.
    if (auto* child = activeChild()) {
        child->setParam(name, value);
    }
}

float StateMachine::getParam(const std::string& name) const {
    auto it = _params.find(name);
    return it == _params.end() ? 0.0f : it->second;
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
        // P3.2 NEW — activate child if the lazily-initialized initial
        // state is a sub-machine entry.
        _currentChildIndex = findSubMachineIndex(_currentState);
    }

    // === P3.2 NEW: tick active child sub-SM first (if any). ===
    // The child gets dt first; its transition may change its own
    // _currentState / _transitioning but does NOT mutate the parent's
    // state graph.
    if (auto* child = activeChild()) {
        child->update(dt);
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

            // P3.2 NEW (INV-31) — if the new state is a sub-machine entry,
            // activate the child; if it's a non-sub-machine state, clear
            // the active child.
            _currentChildIndex = findSubMachineIndex(_currentState);
        }
        // No new transitions during the transition window (UE rule).
        return;
    }

    // (2) Look for an eligible transition (INV-29 — child first, then parent).
    const Transition* t = findEligibleTransitionForSelf();
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
        // P3.2 NEW (INV-31) — instant cut also updates _currentChildIndex
        // since the new state may be a sub-machine entry (or non).
        _currentChildIndex = findSubMachineIndex(_currentState);
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

// === P3.2 NEW implementations ============================================

int StateMachine::addSubMachine(std::unique_ptr<StateMachine> sm) {
    assert(sm != nullptr && "StateMachine::addSubMachine: null pointer");
    const int idx = static_cast<int>(_children.size());
    _children.push_back(std::move(sm));
    return idx;
}

StateMachine* StateMachine::getSubMachine(int idx) {
    if (idx < 0 || idx >= static_cast<int>(_children.size())) return nullptr;
    return _children[idx].get();
}

const StateMachine* StateMachine::getSubMachine(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(_children.size())) return nullptr;
    return _children[idx].get();
}

StateMachine* StateMachine::activeChild() {
    return const_cast<StateMachine*>(
        static_cast<const StateMachine*>(this)->activeChild());
}

const StateMachine* StateMachine::activeChild() const {
    if (_currentChildIndex < 0) return nullptr;
    if (_currentChildIndex >= static_cast<int>(_children.size())) return nullptr;
    return _children[_currentChildIndex].get();
}

StateMachine* StateMachine::getActiveSubMachine() { return activeChild(); }
const StateMachine* StateMachine::getActiveSubMachine() const { return activeChild(); }

int StateMachine::findSubMachineIndex(const std::string& stateName) const {
    auto it = _stateIndexByName.find(stateName);
    if (it == _stateIndexByName.end()) return -1;
    const State& s = _states[it->second];
    if (!s.isSubMachine) return -1;
    if (s.subMachineIndex < 0) return -1;
    if (s.subMachineIndex >= static_cast<int>(_children.size())) return -1;
    return s.subMachineIndex;
}

const Transition* StateMachine::findEligibleTransitionForSelf() const {
    // P3.2 NEW (INV-29) — child first, parent fallback.
    // The active child has already been ticked above in update() step
    // before transitioning; here we ask the child for its own eligible
    // transition only if it hasn't already fired one this frame. Since
    // child->update() returns void, we can't know that — so we let the
    // child search its own transitions again here. Child update() will
    // have advanced any cross-fade clock it owned, but findEligibleTransition
    // is read-only and idempotent, so re-searching is safe.
    if (auto* child = const_cast<StateMachine*>(activeChild())) {
        // (child may have transitioned this frame; search fresh.)
        if (const Transition* t = child->findEligibleTransition()) {
            return t;
        }
    }
    // Parent fallback.
    return findEligibleTransition();
}

std::string StateMachine::getActiveLeafStateName() const {
    // INV-30 — recursion depth ≤ 2. P3.2 limits sub-machines to 1 level;
    // if a child itself has its own active grandchild (not standard P3.2
    // usage), recursion handles it but may be deep.
    if (auto* child = const_cast<StateMachine*>(activeChild())) {
        // Defer to parent until the child has been ticked at least once
        // (so its lazy-init has set _currentState to a real state name).
        if (child->_initialized) {
            return child->getActiveLeafStateName();
        }
    }
    return _currentState;
}

} // namespace ayt::anim