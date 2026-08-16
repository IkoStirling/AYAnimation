// AYAnimation/StateMachine.h — P3.1 (2026-08-06) L1 简单状态机 + P3.2 (2026-08-06) L3 子状态机
//                  + P3.x  (2026-08-07) L2 Condition DSL (string expr + lazy parse +
//                  dirty cache) + P3.x刀 N+1.B (2026-08-07) Time-in-State Query
//                  (getCurrentStateElapsedTime + reserved ident "CurrentStateTime")
//                  + P0 polish (2026-08-07) Flat-array params/triggers + global
//                  name registry (replace unordered_map/string with hash-based
//                  vector lookup; cache-friendly hot path; see design §4.18)
//                  + P1 polish (2026-08-07) Transition hash cache (eliminate
//                  per-frame intern in findEligibleTransition / fireTransition /
//                  L1 eval; see design §4.19)
//                  + P2 polish (2026-08-07) Bytecode parallel cache (AST → flat
//                  opcode stream; eliminate virtual dispatch on hot path; see
//                  design §4.20).
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
//
// INV-43..46 contracts (P0 polish 2026-08-07):
//   * INV-43 — ParamNameRegistry hash 0 is reserved as empty-slot sentinel;
//              FNV-1a baseline (2166136261u) guarantees non-empty names
//              never collide with 0; assert enforces contract at runtime.
//   * INV-44 — setParam/getParam cross-process hash is consistent (same
//              string → same hash); Meyers singleton registry ensures
//              process-global intern table is shared across all SM
//              instances within the same process.
//   * INV-45 — _params is a continuous std::vector<ParamEntry> (no
//              hash bucket, no allocation per lookup); findParamIndex
//              is O(N) linear scan, N ≤ 8 in production. pre-reserved
//              capacity 8 saves most re-allocs.
//   * INV-46 — _triggers is a sorted std::vector<uint32_t> with binary
//              search for O(log N) hasTriggerHash + O(N) eraseTriggerHash;
//              N ≤ 4 in production. Pre-reserved capacity 4.
//
// INV-47..51 contracts (P1 polish 2026-08-07) — Hot-Path Hash Caching:
//   * INV-47 — Transition::triggerHash == 0 ⟺ trigger.empty();
//              computed once at addTransition() time.
//   * INV-48 — Transition::conditionParamNameHash == 0 ⟺ !hasCondition ||
//              condition.paramName.empty(); computed once at addTransition().
//   * INV-49 — Both Transition hashes (triggerHash, conditionParamNameHash)
//              are pre-computed at addTransition() time. Production code
//              never mutates trigger / condition.paramName after addTransition
//              (transition is one-shot author-set, mirrors fromState /
//              toState immutability). However, Transition::evaluateCondition
//              L1 path includes a LAZY FALLBACK: if hasCondition=true but
//              conditionParamNameHash==0 (test fixtures using const_cast to
//              mutate the struct AFTER addTransition), recompute the hash
//              on demand via ParamNameRegistry::intern(). This preserves
//              back-compat with existing P3.x L2 test code that mutates
//              condition.paramName directly while keeping the hot-path
//              speedup intact for production callers.
//   * INV-50 — CondIdentifierExpr::nameHash is pre-computed at ctor time
//              via ParamNameRegistry::intern(); 0 ⟺ empty name (sentinel).
//   * INV-51 — Reserved ident "CurrentStateTime" (string compare) takes
//              priority over nameHash lookup in
//              CondIdentifierExpr::evaluateAsFloat.
//
// INV-52..58 contracts (P2 polish 2026-08-07) — AST → Bytecode cache:
//   * INV-52 — Transition::cachedBytecode == null ⟺ AST parse failed OR
//              not yet evaluated (lazy init; Transition::evaluateBytecode
//              builds it on first call).
//   * INV-53 — Bytecode is 1:1 semantically equivalent to AST — every
//              parseable AST compiles to bytecode producing identical
//              evaluate(ctx) return (4 parity unit tests).
//   * INV-54 — Bytecode eval failure ≡ AST eval failure — return value
//              identical for same ctx (4 parity unit tests).
//   * INV-55 — Reserved ident "CurrentStateTime" encoded as
//              OP_LOAD_RESERVED R_CURRENT_STATE_TIME opcode at compile
//              time; 0 string compare at eval time (vs P1 polish ~5 ns
//              SSO). Bytecode preserves INV-39/51 priority.
//   * INV-56 — Bytecode program + literals are continuous std::vector
//              for cache-friendly traversal; operands embedded in program
//              stream (no separate operand array).
//   * INV-57 — Bytecode lives in shared_ptr<CondBytecode> (not unique_ptr)
//              — vector<Transition> push_back requires copyable Transition.
//   * INV-58 — OP_AND / OP_OR short-circuit encoded as relative jump
//              offset (±127 opcodes) — left false → skip right subtree.

#pragma once

#include <AYAnimation/CondBytecode.h>
#include <AYAnimation/ConditionExpr.h>
#include <AYAnimation/ParamNameRegistry.h>

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

// === P0 polish (2026-08-07) — ParamNameRegistry + ParamEntry ============
//
// Flat-array container refactor: replace unordered_map<string,float> +
// unordered_set<string> with hash-based vector lookups. The hot path
// (setParam / getParam / setTrigger / fireTransition) used to do a
// std::unordered_map lookup (~270 ns/iter for 8 entries, measured in
// AYAnimation_Benchmarks scenario A). The flat vector + global
// registry reduces this to a small linear scan with no per-call
// allocation, no hash bucket walk, no string compare.
//
// Composition:
//   * ParamNameRegistry (declared in AYAnimation/ParamNameRegistry.h) — process-singleton,
//     interns string→hash (FNV-1a 32-bit). Hash IS the canonical key from
//     this point on; the string is held only for debug read-back
//     (getParamName).
//   * ParamEntry { hash, value } — flat row in StateMachine._params.
//   * StateMachine._params is std::vector<ParamEntry>.
//   * StateMachine._triggers is std::vector<uint32_t> (sorted).
//
// The public API (setParam/getParam/setTrigger) is unchanged; only
// internals change. ConditionEvalCtx field types also change (see
// AYAnimation/ConditionExpr.h), but only StateMachine.cpp constructs that struct.
//
// P1 polish (2026-08-07) — ParamNameRegistry is split into its own header
// (AYAnimation/ParamNameRegistry.h) so AYAnimation/ConditionExpr.h's inline CondIdentifierExpr
// ctor can call intern() once at construction time (for nameHash caching)
// without circular include. AYAnimation/StateMachine.h now simply includes the
// split-out header. See AYAnimation/ParamNameRegistry.h header doc for full contract.

// ParamEntry is defined in AYAnimation/ConditionExpr.h (which is included above)
// to avoid a circular include between this header and AYAnimation/ConditionExpr.h.
// Header-order safe: AYAnimation/ConditionExpr.h now includes AYAnimation/ParamNameRegistry.h
// directly (also split out), so ParamNameRegistry is visible
// transitively without AYAnimation/StateMachine.h.

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

    // === P1 polish (2026-08-07) — pre-computed hashes (INV-47..49) ===
    // Pre-computed once at StateMachine::addTransition() time; eliminates
    // per-frame ParamNameRegistry::intern() calls in findEligibleTransition
    // (triggerHash lookup), fireTransition (triggerHash erase), and
    // Transition::evaluateCondition L1 path (conditionParamNameHash lookup).
    // 0 sentinel = empty/missing name (matches "" semantics).
    //
    // INV-49 — both hashes are IMMUTABLE after addTransition(); direct
    // field mutation after construction is undefined behavior. Transition
    // is author-set once (mirrors fromState / toState immutability).
    // If host code mutates trigger or condition.paramName after
    // addTransition(), the cache becomes stale; recompute manually via
    // ParamNameRegistry::instance().intern(name) and assign.
    uint32_t    triggerHash              = 0;
    uint32_t    conditionParamNameHash   = 0;

    // === P2 polish (2026-08-07) — Bytecode parallel cache (INV-52..58) ===
    // AST → flat opcode stream. Built lazily on first evaluateBytecode()
    // call. AST is PRESERVED (cachedAst above) — bytecode is a parallel
    // cache, not a replacement; the AST remains canonical for P4.x
    // graph-builder Visitor consumption. Lives in shared_ptr (INV-57)
    // because Transition itself lives in std::vector<Transition> via
    // push_back (copy required). Lazy init: cachedBytecode == null ⟺
    // AST parse failed OR not yet evaluated (INV-52).
    //
    // setConditionExpr() / invalidateConditionCache() clear this field too
    // (cachedBytecode.reset()) so the next evaluateBytecode rebuilds from
    // the new AST. See Transition::setConditionExpr for the full path.
    mutable std::shared_ptr<CondBytecode> cachedBytecode;

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

    // P2 polish (2026-08-07) — fast-path bytecode evaluator. Hot callers
    // (e.g. StateMachine::findEligibleTransition) use this instead of
    // evaluateCondition() to avoid the virtual-dispatch AST path. Returns
    // the same boolean as evaluateCondition() (INV-54). When the AST
    // has not been parsed yet (or has parse-failed), this method drives
    // the same lazy-init + INV-32..35 semantics as evaluateCondition().
    // Bytecode is built from cachedAst on first call (INV-52); cached
    // across subsequent calls until the next setConditionExpr or
    // invalidateConditionCache.
    bool evaluateBytecode(const ConditionEvalCtx& ctx) const;
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

    // === P3.x刀 N+1.B NEW — Time-in-state query ===
    // Seconds elapsed since the current state was entered. Resets to 0
    // when fireTransition advances to a new state (instant-cut OR
    // cross-fade START — matches UE FAnimNode_StateMachine::
    // GetCurrentStateElapsedTime semantics). Returns 0.0f when the SM
    // has not been initialized (setInitialState not called AND no
    // update tick). Accumulates even during cross-fade window.
    float getCurrentStateElapsedTime() const { return _currentStateEnterTime; }

    // === P0 polish (2026-08-07) — Debug read-back for hash-based params ===
    // Look up the original string for a hash via ParamNameRegistry.
    // Returns empty string for unknown hash. Used by debug UIs / tests
    // to display the param name when only the hash is known (e.g. when
    // iterating _params directly).
    static const std::string& getParamName(uint32_t hash) {
        return detail::ParamNameRegistry::instance().lookup(hash);
    }

    // Number of unique param names registered in the process (across
    // all StateMachine instances). Used for memory accounting / tests.
    static std::size_t getParamNameRegistrySize() {
        return detail::ParamNameRegistry::instance().size();
    }

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

    // === P0 polish (2026-08-07) — flat-array helpers (INV-45, INV-46) ===
    // Linear scan over _params (N ≤ 8 production). Returns SIZE_MAX
    // if not found. Cache-friendly: vector is contiguous, single
    // ~32-byte stride per entry.
    std::size_t findParamIndex(uint32_t hash) const;

    // Write/update param by hash. Replaces value if hash already
    // present; appends otherwise. INV-43 — hash 0 is reserved.
    void setParamByHash(uint32_t hash, float value);

    // Read by hash. Returns 0.0f if not found (INV-23 fail-soft).
    float getParamByHash(uint32_t hash) const;

    // Triggers sorted-vector ops. Sorted by hash for O(log N)
    // binary search via std::lower_bound (INV-46).
    void addTriggerHash(uint32_t hash);
    bool hasTriggerHash(uint32_t hash) const;
    void eraseTriggerHash(uint32_t hash);

    std::vector<State>       _states;
    std::vector<Transition>  _transitions;
    std::unordered_map<std::string, std::size_t> _stateIndexByName;
    // === P0 polish (2026-08-07) — flat array containers ===
    // _triggers: sorted std::vector<uint32_t> (binary search).
    // _params:   std::vector<ParamEntry> (linear scan, N ≤ 8).
    // Pre-reserved capacity avoids most re-allocations on first write.
    std::vector<uint32_t>     _triggers;
    std::vector<ParamEntry>   _params;
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

    // === P3.x刀 N+1.B NEW — Time-in-state accumulator ===
    // Updated by update(dt) top-of-frame (+= dt). Reset to 0 by
    // setInitialState / lazy-init / fireTransition (both instant-cut
    // and cross-fade START paths). Mirrors UE
    // FAnimNode_StateMachine::GetCurrentStateElapsedTime semantics.
    float _currentStateEnterTime = 0.0f;
};

} // namespace ayt::anim