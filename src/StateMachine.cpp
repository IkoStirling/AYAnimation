// StateMachine.cpp — P3.1 (2026-08-06) L1 简单状态机 implementation +
// P3.2 (2026-08-06) L3 子状态机 extensions (sub-machine entry + recursive
// setTrigger/setParam + child-first transition fallback + active child
// resolution) + P3.x (2026-08-07) L2 Condition DSL cache layer
// (setConditionExpr / invalidateConditionCache / evaluateCondition with
//  lazy parse + dirty flag) + P3.x刀 N+1.B (2026-08-07) Time-in-State
// Query (update top +dt + fireTransition reset + ctx plumbing) +
// P0 polish (2026-08-07) Flat-array params/triggers + global name
// registry (replace unordered_map<string,float> + unordered_set<string>
// with hash-based vector lookups; see design §4.18 + INV-43..46).

#include <ayanimation/ConditionParser.h>
#include <ayanimation/StateMachine.h>
#include <algorithm>
#include <cassert>
#include <cstdio>

namespace ayt::anim
{

// === P0 polish (2026-08-07) + P1 polish (2026-08-07) — ParamNameRegistry
// out-of-line storage. The registry is declared in ParamNameRegistry.h;
// only the static kEmpty sentinel needs a definition in one TU. Returns ""
// for unknown hashes — debug-only. Path was ayt::anim::detail before the
// P1 polish header split; same definition site here.
const std::string ayt::anim::detail::ParamNameRegistry::kEmpty;

StateMachine::StateMachine() {
    // P0 polish (INV-45, INV-46) — pre-reserve capacity so the first
    // few setParam/setTrigger calls don't reallocate. Production per-
    // entity params usually ≤ 8; triggers ≤ 4.
    _params.reserve(8);
    _triggers.reserve(4);
}

// === P0 polish (2026-08-07) — flat-array helpers ========================

std::size_t StateMachine::findParamIndex(uint32_t hash) const {
    // Linear scan. With pre-reserved capacity 8 and N ≤ 8 production,
    // this is cache-friendly and beats hash lookup for N ≤ 8.
    for (std::size_t i = 0; i < _params.size(); ++i) {
        if (_params[i].hash == hash) return i;
    }
    return static_cast<std::size_t>(-1);
}

void StateMachine::setParamByHash(uint32_t hash, float value) {
    // INV-43 — hash 0 is reserved as empty-slot sentinel. Real names
    // are guaranteed non-zero by FNV-1a baseline (2166136261u).
    assert(hash != 0 && "StateMachine::setParamByHash: hash 0 is reserved");
    const std::size_t idx = findParamIndex(hash);
    if (idx != static_cast<std::size_t>(-1)) {
        _params[idx].value = value;
    } else {
        _params.push_back({ hash, value });
    }
}

float StateMachine::getParamByHash(uint32_t hash) const {
    // INV-43 — hash 0 is unrecoverable; return 0.0f (INV-23 fail-soft).
    if (hash == 0) return 0.0f;
    const std::size_t idx = findParamIndex(hash);
    return idx == static_cast<std::size_t>(-1) ? 0.0f : _params[idx].value;
}

void StateMachine::addTriggerHash(uint32_t hash) {
    // INV-43 — hash 0 is reserved.
    assert(hash != 0 && "StateMachine::addTriggerHash: hash 0 is reserved");
    // Binary search for insertion point to keep sorted (INV-46).
    auto it = std::lower_bound(_triggers.begin(), _triggers.end(), hash);
    if (it == _triggers.end() || *it != hash) {
        _triggers.insert(it, hash);
    }
}

bool StateMachine::hasTriggerHash(uint32_t hash) const {
    if (hash == 0) return false;
    auto it = std::lower_bound(_triggers.begin(), _triggers.end(), hash);
    return it != _triggers.end() && *it == hash;
}

void StateMachine::eraseTriggerHash(uint32_t hash) {
    if (hash == 0) return;
    auto it = std::lower_bound(_triggers.begin(), _triggers.end(), hash);
    if (it != _triggers.end() && *it == hash) {
        _triggers.erase(it);
    }
}

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

    // P1 polish (2026-08-07) — pre-compute triggerHash and
    // conditionParamNameHash ONCE here, eliminating per-frame
    // ParamNameRegistry::intern() calls in findEligibleTransition,
    // fireTransition, and Transition::evaluateCondition L1 path.
    // Both hashes are immutable after addTransition() (INV-49); if
    // host code mutates trigger / condition.paramName directly after
    // construction, the cache becomes stale (documented in StateMachine.h).
    Transition withHashes = t;
    withHashes.triggerHash = t.trigger.empty()
        ? 0u
        : detail::ParamNameRegistry::instance().intern(t.trigger);
    withHashes.conditionParamNameHash =
        (t.hasCondition && !t.condition.paramName.empty())
        ? detail::ParamNameRegistry::instance().intern(t.condition.paramName)
        : 0u;
    _transitions.push_back(std::move(withHashes));
}

void StateMachine::setInitialState(const std::string& name) {
    assert(_stateIndexByName.count(name) > 0 &&
           "StateMachine::setInitialState: name not in state list");
    _initialState = name;
    _currentState = name;
    _prevStateName = name;
    _initialized = true;
    // P3.x刀 N+1.B NEW — reset time-in-state when initial state set.
    _currentStateEnterTime = 0.0f;

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
    // P3.x刀 N+1.B NEW — clear time-in-state accumulator on clear().
    _currentStateEnterTime = 0.0f;

    // P3.2 NEW — recursively destroy sub-machines (unique_ptr dtor handles).
    _children.clear();
    _currentChildIndex = -1;
}

void StateMachine::setTrigger(const std::string& name) {
    if (name.empty()) return;
    // P0 polish (2026-08-07) — intern to hash, store in sorted vector.
    const uint32_t hash = detail::ParamNameRegistry::instance().intern(name);
    addTriggerHash(hash);

    // P3.2 NEW (INV-28) — propagate into active child only.
    if (auto* child = activeChild()) {
        child->setTrigger(name);
    }
}

void StateMachine::setParam(const std::string& name, float value) {
    if (name.empty()) return;
    // P0 polish (2026-08-07) — intern to hash, write into flat array.
    const uint32_t hash = detail::ParamNameRegistry::instance().intern(name);
    setParamByHash(hash, value);

    // P3.2 NEW (INV-28) — propagate into active child only.
    if (auto* child = activeChild()) {
        child->setParam(name, value);
    }
}

float StateMachine::getParam(const std::string& name) const {
    if (name.empty()) return 0.0f;
    // P0 polish (2026-08-07) — intern to hash, lookup in flat array.
    const uint32_t hash = detail::ParamNameRegistry::instance().intern(name);
    return getParamByHash(hash);
}

void StateMachine::update(float dt) {
    _transitionedThisFrame = false;

    // INV-18 — empty state machine is a no-op.
    if (_states.empty()) return;

    // P3.x刀 N+1.B NEW — accumulate time-in-state at top of frame,
    // BEFORE any transition / lazy-init logic. Matches UE
    // FAnimNode_StateMachine::UpdateAnimation semantics — even during
    // cross-fade window the accumulator keeps advancing.
    _currentStateEnterTime += dt;

    // Lazy-init: if user never called setInitialState, take the first
    // added state as the initial. Mirrors UE Entry Node behavior.
    if (!_initialized) {
        _currentState = _states.front().name;
        _prevStateName = _currentState;
        _initialized = true;
        // P3.x刀 N+1.B NEW — reset on lazy-init (we just "entered" this
        // state for the first time). The += dt above this point will
        // already have advanced the clock by 1 frame, but for a brand-
        // new SM that's the right model: "elapsed since first update".
        _currentStateEnterTime = 0.0f;
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
    // P0 polish (2026-08-07) — intern c.paramName to hash, lookup in flat array.
    const uint32_t hash = detail::ParamNameRegistry::instance().intern(c.paramName);
    const std::size_t idx = findParamIndex(hash);
    if (idx == static_cast<std::size_t>(-1)) {
        // INV-23 — unknown param fail-soft (matches P2.2 ISkeletonMask).
        return false;
    }
    const float v = _params[idx].value;
    switch (c.op) {
        case StateConditionOp::Greater:   return v >  c.compareValue;
        case StateConditionOp::Less:      return v <  c.compareValue;
        case StateConditionOp::Equals:    return std::fabs(v - c.compareValue) <  1e-6f;
        case StateConditionOp::NotEqual:  return std::fabs(v - c.compareValue) >= 1e-6f;
    }
    return false;
}

const Transition* StateMachine::findEligibleTransition() const {
    // P3.x L2 — evaluation context built here so Transition::evaluateCondition
    // can read SM internals via ConditionEvalCtx (params / triggers) without
    // exposing them to callers. currentState + currentStateTime were reserved
    // fields in P3.x; P3.x刀 N+1.B plumbs the live time-in-state value here
    // so CondIdentifierExpr's "CurrentStateTime" reserved ident can read it.
    //
    // P0 polish (2026-08-07) — ctx field types changed:
    //   params:   const std::vector<ParamEntry>* (was unordered_map<string,float>*)
    //   triggers: const std::vector<uint32_t>*    (was unordered_set<string>*)
    const ConditionEvalCtx ctx{
        &_params,
        &_triggers,
        _currentState,
        getCurrentStateElapsedTime(),
    };

    // Author order matters — first match wins.
    for (const auto& t : _transitions) {
        // INV-21 — fromState=="" wildcard; trigger=="" automatic.
        const bool fromMatches =
            t.fromState.empty() || t.fromState == _currentState;
        if (!fromMatches) continue;

        // P1 polish (2026-08-07) — use pre-computed triggerHash from
        // addTransition (was: per-frame intern). 0 hash ⟺ empty trigger,
        // matches "" semantics (INV-47).
        //
        // Lazy fallback: if trigger is non-empty but triggerHash is 0
        // (transition was mutated directly via const_cast after
        // addTransition), recompute the hash on demand. Production
        // paths never reach here because addTransition pre-computes
        // the hash. Test fixtures (e.g. P3.x L2 back-compat tests)
        // that mutate trigger via const_cast AFTER addTransition fall
        // through to this lazy recompute.
        uint32_t lookupHash = t.triggerHash;
        if (lookupHash == 0 && !t.trigger.empty()) {
            lookupHash = detail::ParamNameRegistry::instance().intern(t.trigger);
        }
        const bool triggerOk = t.trigger.empty() ? true
            : hasTriggerHash(lookupHash);
        if (!triggerOk) continue;

        // P3.x: dispatch via Transition::evaluateCondition (L1 or L2 path).
        if (!t.evaluateCondition(ctx)) continue;

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
        // P3.x刀 N+1.B NEW — reset time-in-state on new state entry.
        _currentStateEnterTime = 0.0f;
        // P3.2 NEW (INV-31) — instant cut also updates _currentChildIndex
        // since the new state may be a sub-machine entry (or non).
        _currentChildIndex = findSubMachineIndex(_currentState);
    } else {
        // INV-22 — cross-fade: latch pendingToState; currentState updates
        // only when duration elapses (StateMachine::update step 1).
        // P3.x刀 N+1.B NEW — reset on transition START, not on
        // cross-fade COMPLETE. Matches UE GetCurrentStateElapsedTime:
        // user reads "time since fire", not "time since blend complete".
        _currentStateEnterTime = 0.0f;
        _pendingToState = t.toState;
        _transitioning = true;
        _transitionDuration = t.duration;
        _transitionElapsed = 0.0f;
    }
    // INV-20 — trigger consumed on first eligible transition.
    // P0 polish (2026-08-07) — binary-search erase from sorted vector.
    // P1 polish (2026-08-07) — use pre-computed triggerHash from
    // addTransition (was: per-frame intern). Lazy fallback if trigger
    // was mutated via const_cast AFTER addTransition (INV-49).
    if (!t.trigger.empty()) {
        uint32_t eraseHash = t.triggerHash;
        if (eraseHash == 0) {
            eraseHash = detail::ParamNameRegistry::instance().intern(t.trigger);
        }
        eraseTriggerHash(eraseHash);
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

// === P3.x L2 NEW — Transition DSL cache layer ===========================

void Transition::setConditionExpr(std::string s) {
    // INV-34 — auto-flag dirty only when the source actually changed.
    // Same-string assignments are no-ops to avoid redundant re-parses.
    if (conditionExpr == s) return;
    conditionExpr = std::move(s);
    invalidateConditionCache();
}

void Transition::invalidateConditionCache() {
    conditionDirty = true;
    // Note: cachedAst is intentionally NOT reset here. It stays alive so a
    // concurrent evaluate() reading it (through Transition::evaluateCondition
    // — called from findEligibleTransition) can complete safely. The next
    // call to evaluateCondition will swap it out via unique_ptr assignment
    // (RAII handles the destroy of the old AST).
}

bool Transition::evaluateCondition(const ConditionEvalCtx& ctx) const {
    // === L2 path — DSL expression ===
    // INV-32 / INV-33 / INV-34 / INV-35 contract surface.
    if (!conditionExpr.empty()) {
        if (conditionDirty) {
            // Lazy parse. cachedAst is assigned (RAII destroys old AST if any).
            std::string err;
            cachedAst = ConditionParser::parse(conditionExpr, err);
            conditionParseError = err;
            conditionDirty = false;

            if (cachedAst == nullptr) {
                // INV-33 — parse failure. Surface to stderr for debug, then
                // return false (transition NEVER fires). NOT a crash, NOT
                // an assert. Subsequent evaluate() calls hit the same path
                // because conditionDirty=false + cachedAst=null ⇒ permanent
                // false (the cache does not re-attempt; a new string is
                // needed to retry).
                if (!err.empty()) {
                    std::fprintf(stderr,
                                 "[AYAnimation L2] condition parse fail: %s\n",
                                 err.c_str());
                }
                return false;
            }
        }

        if (cachedAst == nullptr) {
            // INV-33 / INV-35 — cached failure stays permanent until the
            // source string changes (setConditionExpr flags dirty again).
            return false;
        }

        return cachedAst->evaluate(ctx);
    }

    // === L1 path — single predicate (back-compat, INV-23 preserved) ===
    if (!hasCondition) {
        // INV-32 — no condition at all ⇒ evaluates true unconditionally.
        return true;
    }
    // The L1 evaluator is private on StateMachine; replicate its body here
    // so callers (findEligibleTransition) can route through Transition::
    // evaluateCondition uniformly. Mirrors StateMachine::evaluateCondition
    // body — same fail-soft semantics.
    //
    // P0 polish (2026-08-07) — hash-based lookup via ParamNameRegistry.
    // ctx.params is now std::vector<ParamEntry>* (was unordered_map*), so
    // we walk the vector linearly after the string-to-hash intern.
    //
    // P1 polish (2026-08-07) — use pre-computed conditionParamNameHash
    // from addTransition (was: per-frame intern). 0 hash ⟺ !hasCondition
    // OR empty paramName (INV-48 sentinel pre-check).
    //
    // Lazy fallback: if hasCondition=true but conditionParamNameHash is
    // still 0 (transition was mutated directly via const_cast — e.g. in
    // test fixtures or via direct field write after addTransition),
    // recompute the hash on demand. This preserves the hot-path speedup
    // (production paths never hit this branch) while remaining
    // back-compat with test code that mutates struct fields directly.
    if (ctx.params == nullptr) return false;
    uint32_t condHash = conditionParamNameHash;
    if (condHash == 0 && hasCondition && !condition.paramName.empty()) {
        // INV-49 — direct field mutation fallback. Production paths
        // never reach here because addTransition pre-computes the hash.
        // Test fixtures (e.g. P3.x L2 back-compat tests) that mutate
        // condition.paramName via const_cast AFTER addTransition fall
        // through to this lazy recompute.
        condHash = detail::ParamNameRegistry::instance().intern(condition.paramName);
    }
    if (condHash == 0) return false;       // INV-48 sentinel pre-check
    float v = 0.0f;
    bool found = false;
    for (const auto& entry : *ctx.params) {
        if (entry.hash == condHash) { v = entry.value; found = true; break; }
    }
    if (!found) return false;       // INV-23
    switch (condition.op) {
        case StateConditionOp::Greater:   return v >  condition.compareValue;
        case StateConditionOp::Less:      return v <  condition.compareValue;
        case StateConditionOp::Equals:    return std::fabs(v - condition.compareValue) <  1e-6f;
        case StateConditionOp::NotEqual:  return std::fabs(v - condition.compareValue) >= 1e-6f;
    }
    return false;
}

} // namespace ayt::anim