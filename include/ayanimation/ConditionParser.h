// ConditionParser.h — P3.x (2026-08-07) L2 Condition DSL parser API.
//
// Mini lexer + precedence-climbing parser for transition condition
// expressions (e.g. "Speed > 5.0 && IsGrounded"). Patterned after
// AYShader AYLexer / AYParser but ENTIRELY INDEPENDENT — no link / no
// include of AYShader / AYScript / AYGraph. L2 ships standalone.
//
// Grammar (8 operators, 2 literal kinds, 1 primary chain):
//   expression  := or-expr
//   or-expr     := and-expr ( '||' and-expr )*
//   and-expr    := unary-expr ( '&&' unary-expr )*
//   unary-expr  := '!' unary-expr | primary
//   primary     := NUMBER | 'true' | 'false' | IDENT | '(' expression ')'
//
// On parse failure returns nullptr AND sets outErr to a one-line diagnostic
// of the form "line N col M: unexpected token X" (or similar). Subsequent
// errors are not collected (fail-fast); one is enough for the author.
//
// INV-33 honored by the caller (Transition::evaluateCondition): on nullptr
// return, conditionParseError is captured + transition evaluate yields
// false (never asserts, never crashes).

#pragma once

#include <ayanimation/ConditionExpr.h>

#include <memory>
#include <string>

namespace ayt::anim
{

class ConditionParser {
public:
    // Parse a condition expression. Returns the AST root or nullptr.
    // On success, outErr is cleared. On failure, outErr receives a
    // single-line diagnostic with line + col + reason.
    static std::unique_ptr<CondExprAst> parse(
        const std::string& src,
        std::string& outErr);
};

} // namespace ayt::anim