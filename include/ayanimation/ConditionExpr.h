// ConditionExpr.h — P3.x (2026-08-07) L2 Condition DSL AST +
//                    P3.x刀 N+1.B (2026-08-07) Time-in-State Query.
//
// Standalone mini AST for transition condition expressions. Patterned after
// AYShader's AYAst.h (Visitor mode for P4.x graph-builder hook) and AYScript
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
// Operators supported (8): > < == != && || ! ( )
// Literals: float / bool (true / false)
// Identifiers: bare param names looked up in ConditionEvalCtx::params;
//              "CurrentStateTime" is a reserved ident (INV-39).
//
// Out of scope (defer to P3.x刀 N+2 / P4.x):
//   * arithmetic ( + - * / )
//   * function calls / member access
//   * string / int param types

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace ayt::anim
{

// Binary / unary operator kinds. Same enum used in CondBinaryExpr and
// CondUnaryExpr (Not only on unary).
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
// StateMachine.h) so ConditionEvalCtx can use std::vector<ParamEntry>
// without a circular include. StateMachine.h already includes this
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
};

struct CondUnaryExpr final : public CondExprAst {
    CondOp                        op;   // only CondOp::Not is valid
    std::unique_ptr<CondExprAst> operand;

    CondUnaryExpr(CondOp oper, std::unique_ptr<CondExprAst> o)
        : op(oper), operand(std::move(o)) {}

    bool evaluate(const ConditionEvalCtx& ctx) const override;
    void accept(CondVisitor& v) const override { v.visit(*this); }
};

struct CondIdentifierExpr final : public CondExprAst {
    std::string name;

    explicit CondIdentifierExpr(std::string n) : name(std::move(n)) {}

    bool evaluate(const ConditionEvalCtx& ctx) const override;
    void accept(CondVisitor& v) const override { v.visit(*this); }

    // Helper used by CondBinaryExpr comparison arms. Returns the param value
    // from ctx.params, or 0.0f when missing (INV-23 fail-soft). Coerces
    // bool-style usage: ident alone in a bool context reads as
    // (value != 0.0f).
    //
    // P3.x刀 N+1.B — reserved ident "CurrentStateTime" (INV-39) takes
    // priority over user params lookup; same name user param is shadowed.
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