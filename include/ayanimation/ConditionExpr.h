// AYAnimation/ConditionExpr.h — P3.x (2026-08-07) L2 Condition DSL AST +
//                    P3.x刀 N+1.B (2026-08-07) Time-in-State Query.
//                    P5 polish (2026-08-10) DSL 四则运算 + - * / (INV-64..69).
//
// Standalone mini AST for transition condition expressions. Patterned after
// AYShader's AYShader\Ast.h (Visitor mode for P4.x graph-builder hook) and AYScript
// (Logia) bool-expression validation; we borrow the precedence-climbing
// pattern but NOT link/include either module. L2 ships independently.
//
// INV-23..35 contracts (P3.x L2; INV-23 inherited from P3.1):
//   * INV-23 (preserved) — CondIdentifierExpr::evaluateAsFloat on unknown
//                         param returns 0.0f (fail-soft)
//   * INV-32 — empty `Transition::conditionExpr` is equivalent to no condition
//              (transition evaluates true unconditionally)
//   * INV-33 — ConditionParser::parse failure ⇒ Transition caches nullptr +
//              conditionParseError non-empty + stderr one-liner; evaluate
//              returns false. No assert, no throw, no crash.
//   * INV-34 — Transition::setConditionExpr(s) auto-flags conditionDirty;
//              cachedAst is rebuilt lazily on next evaluate; old AST is
//              safely retained (RAII via unique_ptr swap).
//   * INV-35 — Cache invalidation semantics: conditionDirty=true ⇒ next
//              evaluate must parse; conditionDirty=false ⇒ use cachedAst;
//              cachedAst=null + conditionDirty=false ⇒ condition is
//              permanently false (INV-33 honored).
//
// INV-36..39 contracts (P3.x刀 N+1.B — Time-in-State Query):
//   * INV-36 — getCurrentStateElapsedTime() returns 0.0f when _initialized=false
//   * INV-37 — fireTransition (instant-cut AND cross-fade START) reset
//              _currentStateEnterTime to 0.0f
//   * INV-38 — _currentStateEnterTime accumulates dt in update() top-of-frame
//              (even during cross-fade window, matches UE semantics)
//   * INV-39 — reserved ident "CurrentStateTime" in CondIdentifierExpr routes
//              to ctx.currentStateTime (SHADOWS any user param of same name)
//
// INV-50..51 contracts (P1 polish 2026-08-07) — Hot-Path Hash Caching:
//   * INV-50 — CondIdentifierExpr::nameHash is pre-computed at ctor time
//              via ParamNameRegistry::intern(); 0 hash ⟺ empty name
//              (sentinel; FNV-1a baseline guarantees non-empty never 0).
//   * INV-51 — Reserved ident "CurrentStateTime" (string compare) takes
//              priority over nameHash lookup in
//              CondIdentifierExpr::evaluateAsFloat (matches INV-39).
//
// INV-64..69 contracts (P5 polish 2026-08-10 — DSL 四则运算):
//   * INV-64 — CondOp::Add/Sub/Mul/Div are binary FLOAT operators in
//              CondBinaryExpr; evaluateAsFloat computes the value,
//              evaluate() coerces (result != 0.0f). A comparison/logical
//              sub-expression in float context still yields 0.0f (base-class
//              default — documented type-mismatch, unchanged from pre-P5).
//   * INV-65 — Lexer disambiguation: '-' followed by a digit is a signed
//              number literal ONLY when it cannot be a binary operator
//              (previous token does not end an expression). "A-3" == "A - 3".
//   * INV-66 — Unary minus CondOp::Neg (CondUnaryExpr) binds tighter than
//              Mul/Div: "-A * B" parses as "(-A) * B" (parseUnary prefix).
//   * INV-67 — Division by zero (AST AND bytecode) yields 0.0f fail-soft —
//              never inf/nan, mirrors INV-23 philosophy.
//   * INV-68 — Bytecode OP_ADD/OP_SUB/OP_MUL/OP_DIV/OP_NEG are 1:1
//              semantically equivalent to their AST ops (parity tests).
//   * INV-69 — Precedence: arithmetic (Add/Sub=5, Mul/Div=6) binds tighter
//              than comparison (4): "A + B > C" parses as "(A + B) > C".
//
// Operators supported (13): > < == != && || ! + - * / ( )  (unary ! and -)
// Literals: float / bool (true / false)
// Identifiers: bare param names looked up in ConditionEvalCtx::params;
//              "CurrentStateTime" is a reserved ident (INV-39).
//
// Out of scope (defer to P3.x刀 N+2 / P4.x):
//   * function calls / member access
//   * string / int param types
//   * >= / <= (single-token) / % (modulo)

#pragma once

#include <AYAnimation/ParamNameRegistry.h>

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace ayt::anim
{

// Binary / unary operator kinds. Same enum used in CondBinaryExpr and
// CondUnaryExpr (Not/Neg unary only).
enum class CondOp : uint8_t {
    // comparison (binary)
    GT  = 0,
    LT  = 1,
    EQ  = 2,
    NE  = 3,
    // logical
    And = 4,
    Or  = 5,
    Not = 6,   // unary only
    // arithmetic (P5 polish 2026-08-10 — binary float ops, INV-64)
    Add = 7,
    Sub = 8,
    Mul = 9,
    Div = 10,  // b == 0 → 0.0f (INV-67)
    Neg = 11,  // unary minus (INV-66)
};

// Forward declarations of concrete AST node types.
struct CondBinaryExpr;
struct CondUnaryExpr;
struct CondIdentifierExpr;
struct CondLiteralExpr;

// Evaluation context passed down the AST during evaluate(). Points to SM
// internals (params / triggers). ConditionEvalCtx is cheap to construct.
struct ConditionEvalCtx {
    // P0 polish (2026-08-07) — field types changed from unordered_* to
    // flat-vector containers. Only StateMachine.cpp constructs this
    // struct (via findEligibleTransition); downstream users adapt.
    // Test helpers (e.g. AYTest_ConditionExpr.cpp makeCtx) must use
    // std::vector<ParamEntry> + ParamNameRegistry::intern() to build a
    // compatible params vector. See design §4.18 migration notes.
    using ParamsVector   = const std::vector<struct ParamEntry>*;
    using TriggersVector = const std::vector<uint32_t>*;
    ParamsVector        params  = nullptr;
    TriggersVector      triggers = nullptr;
    std::string  currentState;                        // reserved (P3.x刀 N+1)
    float        currentStateTime = 0.0f;             // reserved (P3.x刀 N+1)
};

// P0 polish (2026-08-07) — flat-array row. Defined here (not in
// AYAnimation/StateMachine.h) so ConditionEvalCtx can use std::vector<ParamEntry>
// without a circular include. AYAnimation/StateMachine.h already includes this
// header, so the type is visible transitively.
struct ParamEntry {
    uint32_t hash;    // 0 reserved (sentinel); consumers must reject hash 0
    float    value;
};

// Abstract AST base. Subclasses implement `evaluate` (recursive tree-walk)
// and `accept` (Visitor pattern — P4.x graph-builder hook).
class CondExprAst {
public:
    virtual ~CondExprAst() = default;

    // Recursive boolean evaluation. Short-circuit semantics live in
    // CondBinaryExpr for And/Or; unary / leaf nodes always evaluate.
    virtual bool evaluate(const ConditionEvalCtx& ctx) const = 0;

    // Visitor dispatch. P4.x will add a graph-builder visitor to convert
    // this tree into AYGraph nodes (post-order traversal, edge = tree edge).
    virtual void accept(class CondVisitor& v) const = 0;

    // Helper used by CondBinaryExpr comparison arms. CondIdentifierExpr
    // and CondLiteralExpr override; CondBinaryExpr / CondUnaryExpr are
    // not directly comparable (use evaluate(ctx) for boolean result).
    // Default returns 0.0f — comparisons on boolean sub-expressions are
    // type-mismatched and evaluate-condition via this path returns 0.0f,
    // which will compare false against any non-zero literal.
    virtual float evaluateAsFloat(const ConditionEvalCtx& ctx) const { (void)ctx; return 0.0f; }
};

// Visitor interface. P3.x ships the AST; P4.x adds the first visitor.
class CondVisitor {
public:
    virtual ~CondVisitor() = default;
    virtual void visit(const CondBinaryExpr&)     = 0;
    virtual void visit(const CondUnaryExpr&)      = 0;
    virtual void visit(const CondIdentifierExpr&) = 0;
    virtual void visit(const CondLiteralExpr&)    = 0;
};

// Concrete AST nodes.

struct CondBinaryExpr final : public CondExprAst {
    std::unique_ptr<CondExprAst> left;
    CondOp                        op;
    std::unique_ptr<CondExprAst> right;

    CondBinaryExpr(std::unique_ptr<CondExprAst> l,
                   CondOp                        oper,
                   std::unique_ptr<CondExprAst> r)
        : left(std::move(l)), op(oper), right(std::move(r)) {}

    bool evaluate(const ConditionEvalCtx& ctx) const override;
    void accept(CondVisitor& v) const override { v.visit(*this); }

    // P5 polish (2026-08-10) — arith ops override: Add/Sub/Mul/Div compute
    // the float value. Comparison/logical ops fall through to 0.0f (the
    // base-class default for bool-valued sub-expressions — INV-64).
    float evaluateAsFloat(const ConditionEvalCtx& ctx) const override;
};

struct CondUnaryExpr final : public CondExprAst {
    CondOp                        op;   // CondOp::Not / CondOp::Neg only
    std::unique_ptr<CondExprAst> operand;

    CondUnaryExpr(CondOp oper, std::unique_ptr<CondExprAst> o)
        : op(oper), operand(std::move(o)) {}

    bool evaluate(const ConditionEvalCtx& ctx) const override;
    void accept(CondVisitor& v) const override { v.visit(*this); }

    // P5 polish (2026-08-10) — Neg computes -operand as float (INV-66);
    // Not falls through to 0.0f (bool-valued, same default contract).
    float evaluateAsFloat(const ConditionEvalCtx& ctx) const override;
};

struct CondIdentifierExpr final : public CondExprAst {
    std::string name;
    // P1 polish (2026-08-07) — pre-computed FNV-1a hash of `name`,
    // computed ONCE at construction via ParamNameRegistry::intern().
    // Used by evaluateAsFloat() instead of per-eval intern() lookup.
    // INV-50: 0 hash ⟺ empty name (sentinel; FNV-1a baseline 2166136261u
    // guarantees non-empty names never collide with 0).
    uint32_t    nameHash = 0;

    // Constructor calls ParamNameRegistry::intern() once to populate
    // nameHash. For an empty name, the sentinel 0 is used (skips intern
    // to avoid inserting empty-string sentinel into the registry).
    explicit CondIdentifierExpr(std::string n)
        : name(std::move(n)),
          nameHash(name.empty() ? 0u
              : detail::ParamNameRegistry::instance().intern(name)) {}

    bool evaluate(const ConditionEvalCtx& ctx) const override;
    void accept(CondVisitor& v) const override { v.visit(*this); }

    // Helper used by CondBinaryExpr comparison arms. Returns the param value
    // from ctx.params, or 0.0f when missing (INV-23 fail-soft). Coerces
    // bool-style usage: ident alone in a bool context reads as
    // (value != 0.0f).
    //
    // P3.x刀 N+1.B — reserved ident "CurrentStateTime" (INV-39) takes
    // priority over user params lookup; same name user param is shadowed.
    //
    // P1 polish (2026-08-07) — uses pre-computed nameHash from ctor
    // (INV-50), eliminating per-eval intern() lookup (was: linear scan
    // of ParamNameRegistry._byHash on every evaluateAsFloat call).
    float evaluateAsFloat(const ConditionEvalCtx& ctx) const;
};

struct CondLiteralExpr final : public CondExprAst {
    std::variant<bool, float> value;

    explicit CondLiteralExpr(bool b) : value(b) {}
    explicit CondLiteralExpr(float f) : value(f) {}

    bool evaluate(const ConditionEvalCtx& ctx) const override;
    void accept(CondVisitor& v) const override { v.visit(*this); }

    // Helper for comparison arms. bool → 1.0f/0.0f; float → as-is.
    float evaluateAsFloat(const ConditionEvalCtx& ctx) const;
};

} // namespace ayt::anim