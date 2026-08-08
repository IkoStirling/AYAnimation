// CondBytecode.h — P2 polish (2026-08-07) Flat Bytecode for Condition DSL.
//
// Parallel cache representation of CondExprAst: instead of a virtual-dispatch
// AST, the same expression is compiled to a flat opcode stream + literal
// table. The evaluator is a single switch statement over opcodes — no
// virtual calls, cache-friendly contiguous memory. Measured debug-build
// speedup vs AST path: 1.34x (true) / 1.28x (short-circuit) on the hot
// per-frame evaluation path (Scenario G, see design §4.20); the gap
// widens under optimization.
//
// Architecture decision: AST is PRESERVED (parallel, not replacement).
// Reasons:
//   * CondVisitor graph-builder (P4.x editor) needs the AST for UI traversal.
//   * Back-compat: every existing AST test (12 evaluator + 6 cache + 4 back-
//     compat) keeps passing unchanged.
//   * Lazy build: bytecode is compiled from AST on first evaluate, so the
//     AST path stays the canonical "source of truth" for diagnostics.
//
// INV-52..58 contract surface (P2 polish, see design §4.20):
//   * INV-52 — Transition::cachedBytecode == null ⟺ AST parse failed OR not
//              yet evaluated (lazy init; see Transition::evaluateBytecode).
//   * INV-53 — Bytecode is 1:1 semantically equivalent to AST — every
//              parseable AST compiles to bytecode producing identical
//              evaluate(ctx) return (4 parity unit tests).
//   * INV-54 — Bytecode eval failure ≡ AST eval failure — return value
//              identical for same ctx (4 parity tests).
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

#include <cstdint>
#include <memory>
#include <vector>

namespace ayt::anim
{

struct ConditionEvalCtx;  // forward decl — note: tag is `struct` (matches
                            // ConditionExpr.h) for MSVC symbol compatibility
                            // (different tag kinds produce different mangled
                            // names even for the same logical type).

// === Opcode set ========================================================
// All opcodes are 1 byte. Operands (when present) follow immediately
// in the program stream in fixed order:
//   OP_AND / OP_OR          : OP, int8_t relJump  (signed right-subtree byte count)
//   OP_NOT                  : OP                   (no operand; takes 1 from stack)
//   OP_GT / OP_LT / OP_EQ / OP_NE : OP             (no operand; pops 2 from stack)
//   OP_LOAD_PARAM            : OP, uint32_t hash    (FNV-1a 32-bit, P1 polish)
//   OP_LOAD_LITERAL          : OP, uint32_t idx    (index into literals[])
//   OP_LOAD_RESERVED         : OP, uint8_t rid      (CondReservedId value)
enum class CondOpByte : uint8_t {
    OP_AND          = 0,
    OP_OR           = 1,
    OP_NOT          = 2,
    OP_GT           = 3,
    OP_LT           = 4,
    OP_EQ           = 5,   // |a - b| < 1e-6f
    OP_NE           = 6,   // |a - b| >= 1e-6f
    OP_LOAD_PARAM   = 7,   // u32 hash operand → ctx.params linear scan
    OP_LOAD_LITERAL = 8,   // u32 idx operand  → literals[] (flat float table)
    OP_LOAD_RESERVED = 9,  // u8 rid operand   → ctx.currentStateTime (INV-55)
};

// === Reserved ident IDs ================================================
// Future P3.x刀 N+1.1 expansion: add R_CURRENT_STATE_NAME, R_CURRENT_STATE_INDEX
// etc. OP_LOAD_RESERVED dispatch grows naturally; evaluator switch arms
// added in lockstep. (Currently only 1 reserved ident — INV-39.)
enum class CondReservedId : uint8_t {
    R_CURRENT_STATE_TIME = 0,
    // R_CURRENT_STATE_NAME  = 1,  // future P3.x刀 N+1.1
};

// === Literal table (INV-56) ============================================
// literals is a flat std::vector<float>. Bools are stored as 1.0f / 0.0f
// — the evaluator coerces with `!= 0.0f` exactly like the AST's
// CondLiteralExpr::evaluate (ConditionParser.cpp:81-86), so a separate
// tag bit is unnecessary and float bits are preserved verbatim
// (no sign/exponent mangling — the earlier bit-30 sign scheme was broken
// for |v| >= 2.0, where bit 30 is part of the IEEE-754 exponent).

// === CondBytecode (the flat-program representation) ====================
// Owns its program bytes + literal table. LIVES IN shared_ptr (INV-57) so
// vector<Transition> push_back can copy Transition by-value.
// program + literals are continuous std::vector (INV-56) — single alloc each.
struct CondBytecode {
    std::vector<uint8_t>  program;
    std::vector<float>    literals;

    CondBytecode() = default;

    // Evaluate the compiled program against `ctx`. Returns the boolean result.
    // Stack-machine: the evaluator uses a FIXED-SIZE float stack array
    // (capacity 16; AST depth ≤ 5 production) — no heap allocation on the
    // hot path (a std::vector stack with reserve(8) was ~8x slower in the
    // debug benchmark). On stack overflow / malformed program, returns
    // false (fail-soft, mirrors INV-33).
    bool evaluate(const ConditionEvalCtx& ctx) const;
};

// === Compile API ========================================================
// Implemented in ConditionParser.cpp. Post-order walk of the AST → flat
// program. Returns nullptr only if the AST is null (defensive).
//
// CompileToBytecode NEVER throws; it just walks node pointers and emits
// bytes. The caller (Transition::evaluateBytecode) is responsible for
// handling AST parse failures (cachedAst == nullptr path).
//
// NOTE — forward decl tag must be `class` to match ConditionExpr.h's
// `class CondExprAst` definition. MSVC mangles by first-seen tag kind in
// each TU: `struct CondExprAst;` here + `class CondExprAst` there = C4099
// + LNK2019 (different mangled names for the same logical type). Same
// rule as ConditionEvalCtx above, but in reverse.
class CondExprAst;
std::shared_ptr<CondBytecode> compileToBytecode(const CondExprAst* ast);

} // namespace ayt::anim