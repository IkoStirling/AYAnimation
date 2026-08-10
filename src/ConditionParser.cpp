// ConditionParser.cpp — P3.x (2026-08-07) L2 Condition DSL implementation
//                    + P2 polish (2026-08-07) compileToBytecode (AST → flat
//                      opcode stream).
//
// Mini Lexer + precedence-climbing Parser for transition condition
// expressions. Patterned after AYShader AYLexer / AYParser token stream
// + climb; entirely independent (no link to AYShader / AYScript / AYGraph).
//
// Grammar (low → high precedence):
//   1. Or   (||)
//   2. And  (&&)
//   3. Not / Neg  (! / -)   unary
//   4. Compare (>, <, ==, !=)
//   5. Add / Sub  (+ / -)        (P5 polish, INV-69)
//   6. Mul / Div  (* / /)        (P5 polish, INV-69)
//   7. Primary (literal, ident, paren)
//
// Failure mode: lexer pushes Unknown token + records diagnostic; parser
// records first error and returns nullptr. Caller (Transition::evaluate-
// Condition) fails soft — no assert, no throw, evaluate returns false
// (INV-33).
//
// P2 polish — compileToBytecode. Post-order walk of the AST emits a flat
// opcode stream + literal table. Short-circuit OP_AND/OR encoded with
// a signed relative jump offset that the evaluator skips when the left
// operand is decisive (INV-58). See CondBytecode.h for opcode set + layout.

#include <ayanimation/CondBytecode.h>
#include <ayanimation/ConditionExpr.h>
#include <ayanimation/ConditionParser.h>
#include <ayanimation/StateMachine.h>  // P0 polish — for detail::ParamNameRegistry

#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ayt::anim
{

// =====================================================================
// AST evaluate impls (header declared evaluate / evaluateAsFloat).
// =====================================================================

bool CondIdentifierExpr::evaluate(const ConditionEvalCtx& ctx) const {
    return evaluateAsFloat(ctx) != 0.0f;
}

float CondIdentifierExpr::evaluateAsFloat(const ConditionEvalCtx& ctx) const {
    // P3.x刀 N+1.B — INV-39 reserved ident. "CurrentStateTime" is a
    // hard-coded SM-internal state variable; it shadows any user
    // param registered with setParam("CurrentStateTime", ...).
    // Mirrors UE FAnimNode_StateMachine::GetCurrentStateElapsedTime
    // access pattern (state-machine internal state, not a free param).
    //
    // P1 polish (2026-08-07) — string compare is cheap (~5ns SSO for
    // "CurrentStateTime"); reserved ident priority preserved (INV-51).
    if (name == "CurrentStateTime") {
        return ctx.currentStateTime;
    }
    if (ctx.params == nullptr) return 0.0f;       // INV-23 fail-soft
    // P0 polish (2026-08-07) — walk the flat params vector (N ≤ 8
    // production) using the FNV-1a hash.
    // P1 polish (2026-08-07) — use pre-computed nameHash from ctor
    // (INV-50), eliminating per-eval ParamNameRegistry::intern() call.
    // Production state machine: 100 entities × 3 idents × 60 fps =
    // 18,000 intern/秒 saved per-frame.
    const uint32_t identifierHash = nameHash;
    if (identifierHash == 0) return 0.0f;          // empty name → INV-23 fail-soft
    for (const auto& entry : *ctx.params) {
        if (entry.hash == identifierHash) return entry.value;
    }
    return 0.0f;                                   // INV-23 fail-soft
}

bool CondLiteralExpr::evaluate(const ConditionEvalCtx& /*ctx*/) const {
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value);
    }
    return std::get<float>(value) != 0.0f;
}

float CondLiteralExpr::evaluateAsFloat(const ConditionEvalCtx& /*ctx*/) const {
    if (std::holds_alternative<float>(value)) {
        return std::get<float>(value);
    }
    return std::get<bool>(value) ? 1.0f : 0.0f;
}

bool CondUnaryExpr::evaluate(const ConditionEvalCtx& ctx) const {
    switch (op) {
        case CondOp::Not: return !operand->evaluate(ctx);
        // P5 polish — unary minus coerces to bool like any float value:
        // "-A" as a standalone condition reads as (-A != 0.0f).
        case CondOp::Neg: return operand->evaluateAsFloat(ctx) != 0.0f;
        default:          return false;
    }
}

// P5 polish (2026-08-10) — Neg returns -operand as a float value (INV-66),
// so "-Speed + 1" computes correctly. Not is bool-valued; falls through
// to the 0.0f default (INV-64 type-mismatch contract).
float CondUnaryExpr::evaluateAsFloat(const ConditionEvalCtx& ctx) const {
    if (op == CondOp::Neg) return -operand->evaluateAsFloat(ctx);
    return 0.0f;
}

bool CondBinaryExpr::evaluate(const ConditionEvalCtx& ctx) const {
    switch (op) {
        // INV-34 short-circuit semantics
        case CondOp::And: return left->evaluate(ctx) && right->evaluate(ctx);
        case CondOp::Or:  return left->evaluate(ctx) || right->evaluate(ctx);

        case CondOp::GT:  return left->evaluateAsFloat(ctx) >  right->evaluateAsFloat(ctx);
        case CondOp::LT:  return left->evaluateAsFloat(ctx) <  right->evaluateAsFloat(ctx);
        case CondOp::EQ:
            return std::fabs(left->evaluateAsFloat(ctx) - right->evaluateAsFloat(ctx)) < 1e-6f;
        case CondOp::NE:
            return std::fabs(left->evaluateAsFloat(ctx) - right->evaluateAsFloat(ctx)) >= 1e-6f;

        // P5 polish — arithmetic in bool context coerces (INV-64).
        case CondOp::Add:
        case CondOp::Sub:
        case CondOp::Mul:
        case CondOp::Div:
            return evaluateAsFloat(ctx) != 0.0f;

        case CondOp::Not:
            // Not is unary only; binary Not in this arm is undefined.
            return false;
        case CondOp::Neg:
            // Neg is unary only; binary Neg in this arm is undefined.
            return false;
    }
    return false;
}

// P5 polish (2026-08-10) — arithmetic float evaluation (INV-64). The
// comparison/logical arms fall through to 0.0f — the documented
// type-mismatch contract (bool sub-expressions are not float-valued),
// identical to the base-class default.
float CondBinaryExpr::evaluateAsFloat(const ConditionEvalCtx& ctx) const {
    switch (op) {
        case CondOp::Add: return left->evaluateAsFloat(ctx) + right->evaluateAsFloat(ctx);
        case CondOp::Sub: return left->evaluateAsFloat(ctx) - right->evaluateAsFloat(ctx);
        case CondOp::Mul: return left->evaluateAsFloat(ctx) * right->evaluateAsFloat(ctx);
        case CondOp::Div: {
            const float b = right->evaluateAsFloat(ctx);
            if (b == 0.0f) return 0.0f;        // INV-67 — div-by-zero fail-soft
            return left->evaluateAsFloat(ctx) / b;
        }
        default:
            // Comparison / logical — not float-valued (INV-64).
            return 0.0f;
    }
}

// =====================================================================
// Lexer
// =====================================================================

namespace {

enum class CondTokenKind : uint8_t {
    Number,
    True,
    False,
    Ident,
    GT,    // >
    LT,    // <
    EQ,    // ==
    NE,    // !=
    And,   // &&
    Or,    // ||
    Not,   // !
    Plus,  // +   (P5 polish — arithmetic)
    Minus, // -   (P5 polish — binary sub or unary neg)
    Star,  // *   (P5 polish)
    Slash, // /   (P5 polish)
    LParen,// (
    RParen,// )
    End,
    Unknown,
};

struct CondToken {
    CondTokenKind kind = CondTokenKind::End;
    std::string   text;
    float         number = 0.0f;
    std::size_t   line = 1;
    std::size_t   col  = 1;
};

class CondLexer {
public:
    explicit CondLexer(const std::string& src) : _src(src) {}

    // Tokenize the entire source. Populates _tokens; first error string
    // (if any) goes into outErr. Unknown characters are tokenized as
    // Unknown (don't fail the lexer; parser will report the error).
    void tokenize(std::vector<CondToken>& outTokens, std::string& outErr) {
        std::size_t i = 0;
        std::size_t line = 1;
        std::size_t col  = 1;
        while (i < _src.size()) {
            const char c = _src[i];

            // Whitespace
            if (c == ' ' || c == '\t' || c == '\r') {
                ++i; ++col;
                continue;
            }
            if (c == '\n') {
                ++i;
                ++line;
                col = 1;
                continue;
            }

            // Line comment `// ... \n`
            if (c == '/' && i + 1 < _src.size() && _src[i + 1] == '/') {
                while (i < _src.size() && _src[i] != '\n') {
                    ++i; ++col;
                }
                continue;
            }

            // P5 polish (INV-65) — disambiguate '-' between signed number
            // literal vs binary subtraction. A '-' followed by a digit is a
            // negative literal ONLY when the previous token cannot end an
            // expression (i.e. '-' can't be binary). So "A-3" lexes as
            // [A][-][3] (binary Sub) while "-3" / "2 * -3" keep the signed
            // literal (preserves pre-P5 AST shape for negative literals).
            const bool prevEndsExpr = !outTokens.empty() &&
                (outTokens.back().kind == CondTokenKind::Number ||
                 outTokens.back().kind == CondTokenKind::True ||
                 outTokens.back().kind == CondTokenKind::False ||
                 outTokens.back().kind == CondTokenKind::Ident ||
                 outTokens.back().kind == CondTokenKind::RParen);

            // Number literal (including leading '-' for negative literals).
            if (std::isdigit(static_cast<unsigned char>(c)) ||
                (c == '-' && !prevEndsExpr && i + 1 < _src.size() &&
                 std::isdigit(static_cast<unsigned char>(_src[i + 1])))) {
                CondToken t;
                t.line = line; t.col = col;
                std::size_t start = i;
                if (c == '-') { ++i; ++col; }   // consume leading sign
                while (i < _src.size() &&
                       (std::isdigit(static_cast<unsigned char>(_src[i])) ||
                        _src[i] == '.' ||
                        _src[i] == 'e' || _src[i] == 'E' ||
                        _src[i] == '+' || _src[i] == '-')) {
                    // The +/- in exponent is consumed only after [eE].
                    if ((_src[i] == '+' || _src[i] == '-') &&
                        !(i > start && (_src[i - 1] == 'e' || _src[i - 1] == 'E'))) {
                        break;
                    }
                    ++i; ++col;
                }
                t.text = _src.substr(start, i - start);
                t.number = std::stof(t.text);
                t.kind = CondTokenKind::Number;
                outTokens.push_back(std::move(t));
                continue;
            }

            // Identifier / keyword
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                CondToken t;
                t.line = line; t.col = col;
                std::size_t start = i;
                while (i < _src.size() &&
                       (std::isalnum(static_cast<unsigned char>(_src[i])) ||
                        _src[i] == '_')) {
                    ++i; ++col;
                }
                t.text = _src.substr(start, i - start);
                if      (t.text == "true")  t.kind = CondTokenKind::True;
                else if (t.text == "false") t.kind = CondTokenKind::False;
                else                        t.kind = CondTokenKind::Ident;
                outTokens.push_back(std::move(t));
                continue;
            }

            // Operators / punctuation
            CondToken t;
            t.line = line; t.col = col;
            const char c1 = c;
            const char c2 = (i + 1 < _src.size()) ? _src[i + 1] : '\0';

            if (c1 == '>' && c2 == '=') { /* >= not in L2 scope */ }
            if (c1 == '<' && c2 == '=') { /* <= not in L2 scope */ }

            if (c1 == '=' && c2 == '=') {
                t.kind = CondTokenKind::EQ; t.text = "==";
                i += 2; col += 2;
            } else if (c1 == '!' && c2 == '=') {
                t.kind = CondTokenKind::NE; t.text = "!=";
                i += 2; col += 2;
            } else if (c1 == '&' && c2 == '&') {
                t.kind = CondTokenKind::And; t.text = "&&";
                i += 2; col += 2;
            } else if (c1 == '|' && c2 == '|') {
                t.kind = CondTokenKind::Or; t.text = "||";
                i += 2; col += 2;
            } else if (c1 == '!') {
                t.kind = CondTokenKind::Not; t.text = "!";
                ++i; ++col;
            } else if (c1 == '>') {
                t.kind = CondTokenKind::GT; t.text = ">";
                ++i; ++col;
            } else if (c1 == '<') {
                t.kind = CondTokenKind::LT; t.text = "<";
                ++i; ++col;
            } else if (c1 == '(') {
                t.kind = CondTokenKind::LParen; t.text = "(";
                ++i; ++col;
            } else if (c1 == ')') {
                t.kind = CondTokenKind::RParen; t.text = ")";
                ++i; ++col;
            } else if (c1 == '+') {
                // P5 polish — arithmetic. Only binary '+' exists (no unary
                // plus); parsePrimary will reject leading '+' cleanly.
                t.kind = CondTokenKind::Plus; t.text = "+";
                ++i; ++col;
            } else if (c1 == '-') {
                // P5 polish — binary subtraction, or unary minus when the
                // lexer couldn't fold it into a negative literal (INV-65/66).
                t.kind = CondTokenKind::Minus; t.text = "-";
                ++i; ++col;
            } else if (c1 == '*') {
                t.kind = CondTokenKind::Star; t.text = "*";
                ++i; ++col;
            } else if (c1 == '/') {
                t.kind = CondTokenKind::Slash; t.text = "/";
                ++i; ++col;
            } else {
                // Unknown char — tokenize but record.
                t.kind = CondTokenKind::Unknown;
                t.text = std::string(1, c1);
                if (outErr.empty()) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf),
                                  "line %zu col %zu: unknown character '%c'",
                                  line, col, c1);
                    outErr = buf;
                }
                ++i; ++col;
            }
            outTokens.push_back(std::move(t));
        }

        CondToken end;
        end.kind = CondTokenKind::End;
        end.line = line; end.col = col;
        outTokens.push_back(end);
    }

private:
    const std::string& _src;
};

// =====================================================================
// Parser
// =====================================================================

class CondParserImpl {
public:
    CondParserImpl(const std::vector<CondToken>& tokens, std::string& err)
        : _tokens(tokens), _err(err) {}

    std::unique_ptr<CondExprAst> parseAll() {
        auto result = parseExpression(/*minPrec=*/1);
        if (result == nullptr) return nullptr;
        if (_pos < _tokens.size() && _tokens[_pos].kind != CondTokenKind::End) {
            recordErr("unexpected token '%s'", _tokens[_pos].text.c_str());
            return nullptr;
        }
        return result;
    }

private:
    using Kind = CondTokenKind;

    // precedence-climbing core. minPrec is the lowest precedence allowed
    // at this level (1 = top-level OR).
    std::unique_ptr<CondExprAst> parseExpression(int minPrec) {
        auto left = parseUnary();
        if (left == nullptr) return nullptr;

        while (_pos < _tokens.size()) {
            const Kind k = _tokens[_pos].kind;
            const int prec = precedence(k);
            if (prec < minPrec) break;
            if (k == Kind::RParen || k == Kind::End) break;

            const CondOp op = binaryOpFor(k);
            if (op == CondOp::Not && k != Kind::Not) {
                // Should not happen; Not is unary only.
            }

            ++_pos;  // consume operator
            auto right = parseExpression(prec + 1);
            if (right == nullptr) return nullptr;

            left = std::make_unique<CondBinaryExpr>(
                std::move(left), op, std::move(right));
        }
        return left;
    }

    std::unique_ptr<CondExprAst> parseUnary() {
        if (_pos < _tokens.size()) {
            // P5 polish (INV-66) — unary minus. Prefix position means it
            // binds tighter than ANY binary op ("-A * B" = "(-A) * B").
            if (_tokens[_pos].kind == Kind::Minus) {
                ++_pos;  // consume '-'
                auto operand = parseUnary();   // right-associative
                if (operand == nullptr) return nullptr;
                return std::make_unique<CondUnaryExpr>(CondOp::Neg, std::move(operand));
            }
            if (_tokens[_pos].kind == Kind::Not) {
                ++_pos;  // consume '!'
                auto operand = parseUnary();   // right-associative
                if (operand == nullptr) return nullptr;
                return std::make_unique<CondUnaryExpr>(CondOp::Not, std::move(operand));
            }
        }
        return parsePrimary();
    }

    std::unique_ptr<CondExprAst> parsePrimary() {
        if (_pos >= _tokens.size()) {
            recordErr("unexpected end of input");
            return nullptr;
        }
        const CondToken& t = _tokens[_pos];
        switch (t.kind) {
            case Kind::Number: {
                ++_pos;
                return std::make_unique<CondLiteralExpr>(t.number);
            }
            case Kind::True: {
                ++_pos;
                return std::make_unique<CondLiteralExpr>(true);
            }
            case Kind::False: {
                ++_pos;
                return std::make_unique<CondLiteralExpr>(false);
            }
            case Kind::Ident: {
                ++_pos;
                return std::make_unique<CondIdentifierExpr>(t.text);
            }
            case Kind::LParen: {
                ++_pos;  // consume '('
                auto inner = parseExpression(/*minPrec=*/1);
                if (inner == nullptr) return nullptr;
                if (_pos >= _tokens.size() || _tokens[_pos].kind != Kind::RParen) {
                    recordErr("expected ')'");
                    return nullptr;
                }
                ++_pos;  // consume ')'
                return inner;
            }
            default: {
                recordErr("unexpected token '%s'", t.text.c_str());
                return nullptr;
            }
        }
    }

    static int precedence(Kind k) {
        switch (k) {
            case Kind::Or:  return 1;
            case Kind::And: return 2;
            // Unary Not / Neg are handled separately in parseUnary.
            case Kind::GT:
            case Kind::LT:
            case Kind::EQ:
            case Kind::NE:
                return 4;
            // P5 polish (INV-69) — arithmetic binds tighter than comparison:
            // "A + B > C" parses as "(A + B) > C".
            case Kind::Plus:
            case Kind::Minus:
                return 5;
            case Kind::Star:
            case Kind::Slash:
                return 6;
            default:
                return 0;
        }
    }

    static CondOp binaryOpFor(Kind k) {
        switch (k) {
            case Kind::GT:  return CondOp::GT;
            case Kind::LT:  return CondOp::LT;
            case Kind::EQ:  return CondOp::EQ;
            case Kind::NE:  return CondOp::NE;
            case Kind::And: return CondOp::And;
            case Kind::Or:  return CondOp::Or;
            // P5 polish — arithmetic (INV-64).
            case Kind::Plus:  return CondOp::Add;
            case Kind::Minus: return CondOp::Sub;
            case Kind::Star:  return CondOp::Mul;
            case Kind::Slash: return CondOp::Div;
            default:          return CondOp::Not;   // invalid-binary sentinel
        }
    }

    void recordErr(const char* fmt, ...) {
        if (!_err.empty()) return;
        char locBuf[64];
        if (_pos < _tokens.size()) {
            const CondToken& t = _tokens[_pos];
            std::snprintf(locBuf, sizeof(locBuf),
                          "line %zu col %zu: ",
                          t.line, t.col);
        } else {
            std::snprintf(locBuf, sizeof(locBuf), "line EOF: ");
        }
        char tail[192];
        std::va_list args;
        va_start(args, fmt);
        std::vsnprintf(tail, sizeof(tail), fmt, args);
        va_end(args);
        _err = locBuf;
        _err += tail;
    }

    const std::vector<CondToken>& _tokens;
    std::string& _err;
    std::size_t  _pos = 0;
};

}  // namespace

// =====================================================================
// ConditionParser::parse — public entry
// =====================================================================

std::unique_ptr<CondExprAst> ConditionParser::parse(
    const std::string& src,
    std::string& outErr)
{
    outErr.clear();
    if (src.empty()) {
        // INV-32 — empty expression is legal (means "no condition"); the
        // Transition layer treats empty conditionExpr as true. Return
        // nullptr here; the caller distinguishes empty-src from parse-fail.
        return nullptr;
    }

    try {
        std::vector<CondToken> tokens;
        CondLexer lexer(src);
        lexer.tokenize(tokens, outErr);

        CondParserImpl parser(tokens, outErr);
        return parser.parseAll();
    } catch (const std::exception& e) {
        outErr = std::string("exception: ") + e.what();
        return nullptr;
    } catch (...) {
        outErr = "unknown exception";
        return nullptr;
    }
}

// =====================================================================
// P2 polish (2026-08-07) — compileToBytecode (AST → flat opcode stream)
// =====================================================================
//
// Post-order walk. For each AST node:
//   * Leaves: emit one LOAD_* opcode (LOAD_PARAM / LOAD_LITERAL /
//     LOAD_RESERVED for "CurrentStateTime" — INV-55).
//   * Unary Not / Neg: compile operand, then emit OP_NOT / OP_NEG
//     (P5 polish — Neg is unary minus).
//   * Binary GT/LT/EQ/NE + Add/Sub/Mul/Div (P5 polish): compile left,
//     compile right, emit OP_*. Stack machine: 2 values on stack, 1
//     result pushed by opcode.
//   * Binary And/Or with short-circuit (INV-58): compile left; emit
//     OP_AND/OR placeholder (with placeholder jump offset = 0); compile
//     right; compute right-subtree byte count; patch placeholder jump.
//
// Branch jump encoding: OP_AND/OR is followed by 1 signed byte that is
// the relative byte count of the right subtree. When the evaluator
// decides short-circuit, it advances pc by that many bytes — the right
// subtree is skipped. Limit: right subtree ≤ 127 bytes (INV-58 ±127).
//
// Compile never throws. Returns nullptr only if ast is null.

namespace
{

// Reserved ident string constant for INV-55 dispatch.
constexpr const char* kReservedCurrentStateTime = "CurrentStateTime";

// Compile an AST node into `prog` (program bytes) + `lits` (literal table).
// Recursive post-order walk. The size of the right-subtree of OP_AND/OR is
// patched AFTER the right subtree is compiled (we don't know its size in
// advance; the placeholder sits 1 byte after the OP_AND/OR opcode byte).
//
// `jumpPatchSite` is the byte offset within prog where the int8_t jump
// offset lives for OP_AND/OR — used by the caller after right-subtree
// compilation to back-patch.
void compileNode(const CondExprAst* node,
                 std::vector<uint8_t>& prog,
                 std::vector<float>& lits,
                 std::size_t& jumpPatchSite)
{
    jumpPatchSite = static_cast<std::size_t>(-1);  // no patch site by default

    if (node == nullptr) return;  // defensive — caller guards

    if (auto* bin = dynamic_cast<const CondBinaryExpr*>(node)) {
        // Compile left first.
        std::size_t leftJumpSite;
        compileNode(bin->left.get(), prog, lits, leftJumpSite);

        if (bin->op == CondOp::And || bin->op == CondOp::Or) {
            // Emit placeholder opcode + placeholder jump (0 for now).
            const CondOpByte opByte = (bin->op == CondOp::And)
                ? CondOpByte::OP_AND : CondOpByte::OP_OR;
            prog.push_back(static_cast<uint8_t>(opByte));
            const std::size_t jumpSite = prog.size();
            prog.push_back(0);                 // placeholder; patched below
            // Compile right subtree.
            std::size_t rightJumpSite;
            compileNode(bin->right.get(), prog, lits, rightJumpSite);
            // Compute right-subtree byte count.
            const std::size_t rightSize = prog.size() - (jumpSite + 1);
            // INV-58 — ±127 limit. Production AST depth ≤ 5; right
            // subtree size well under 64 bytes. If overflow (e.g.
            // pathological 1000-token expression), clamp to 127 and
            // the evaluator will mis-skip — document as production safe
            // (right subtree < 127 bytes always).
            prog[jumpSite] = (rightSize > 127)
                ? static_cast<uint8_t>(127)
                : static_cast<uint8_t>(rightSize);
            jumpPatchSite = jumpSite;          // unused; reserved
            return;
        }

        // Comparison (GT / LT / EQ / NE): compile RIGHT subtree too —
        // post-order emission: left, right, then the comparison opcode
        // (the evaluator pops 2 and pushes 1). The right subtree must
        // be compiled BEFORE the opcode byte, and both operands must
        // have been emitted for the stack machine to see 2 values.
        std::size_t rightJumpSite;
        compileNode(bin->right.get(), prog, lits, rightJumpSite);
        switch (bin->op) {
            case CondOp::GT: prog.push_back(static_cast<uint8_t>(CondOpByte::OP_GT)); return;
            case CondOp::LT: prog.push_back(static_cast<uint8_t>(CondOpByte::OP_LT)); return;
            case CondOp::EQ: prog.push_back(static_cast<uint8_t>(CondOpByte::OP_EQ)); return;
            case CondOp::NE: prog.push_back(static_cast<uint8_t>(CondOpByte::OP_NE)); return;
            // P5 polish — arithmetic (INV-68): same stack shape as
            // comparison — 2 on stack, 1 result pushed.
            case CondOp::Add: prog.push_back(static_cast<uint8_t>(CondOpByte::OP_ADD)); return;
            case CondOp::Sub: prog.push_back(static_cast<uint8_t>(CondOpByte::OP_SUB)); return;
            case CondOp::Mul: prog.push_back(static_cast<uint8_t>(CondOpByte::OP_MUL)); return;
            case CondOp::Div: prog.push_back(static_cast<uint8_t>(CondOpByte::OP_DIV)); return;
            case CondOp::Not: /* should not be binary */ return;
            default: return;                    // And/Or handled above
        }
    }

    if (auto* un = dynamic_cast<const CondUnaryExpr*>(node)) {
        std::size_t innerJumpSite;
        compileNode(un->operand.get(), prog, lits, innerJumpSite);
        // Not / Neg are the only valid unary ops (per AST contract).
        if (un->op == CondOp::Not) {
            prog.push_back(static_cast<uint8_t>(CondOpByte::OP_NOT));
        } else if (un->op == CondOp::Neg) {
            prog.push_back(static_cast<uint8_t>(CondOpByte::OP_NEG));
        }
        return;
    }

    if (auto* ident = dynamic_cast<const CondIdentifierExpr*>(node)) {
        // INV-55 — reserved ident "CurrentStateTime" encoded as dedicated
        // opcode (0 string compare at eval time).
        if (ident->name == kReservedCurrentStateTime) {
            prog.push_back(static_cast<uint8_t>(CondOpByte::OP_LOAD_RESERVED));
            prog.push_back(static_cast<uint8_t>(CondReservedId::R_CURRENT_STATE_TIME));
            return;
        }
        // User param — emit LOAD_PARAM with cached FNV-1a hash from P1 polish.
        prog.push_back(static_cast<uint8_t>(CondOpByte::OP_LOAD_PARAM));
        uint32_t hash = ident->nameHash;            // P1 polish cached
        const std::size_t site = prog.size();
        prog.resize(site + 4);
        std::memcpy(&prog[site], &hash, 4);
        return;
    }

    if (auto* lit = dynamic_cast<const CondLiteralExpr*>(node)) {
        prog.push_back(static_cast<uint8_t>(CondOpByte::OP_LOAD_LITERAL));
        const std::size_t idx = lits.size();
        // Flat float table (INV-56): bools stored as 1.0f/0.0f — the
        // evaluator coerces with `!= 0.0f` exactly like the AST's
        // CondLiteralExpr::evaluate (no tag bit, no bit-30 sign scheme —
        // that was broken for |v| >= 2.0 where bit 30 is IEEE-754 exponent).
        if (std::holds_alternative<bool>(lit->value)) {
            lits.push_back(std::get<bool>(lit->value) ? 1.0f : 0.0f);
        } else {
            lits.push_back(std::get<float>(lit->value));
        }
        const std::size_t site = prog.size();
        prog.resize(site + 4);
        uint32_t idx32 = static_cast<uint32_t>(idx);
        std::memcpy(&prog[site], &idx32, 4);
        return;
    }
}

}  // namespace

std::shared_ptr<CondBytecode> compileToBytecode(const CondExprAst* ast) {
    if (ast == nullptr) return nullptr;
    auto code = std::make_shared<CondBytecode>();
    std::size_t patchSite = 0;                     // unused — reserved
    compileNode(ast, code->program, code->literals, patchSite);
    return code;
}

} // namespace ayt::anim