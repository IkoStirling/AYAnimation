// ConditionParser.cpp — P3.x (2026-08-07) L2 Condition DSL implementation.
//
// Mini Lexer + precedence-climbing Parser for transition condition
// expressions. Patterned after AYShader AYLexer / AYParser token stream
// + climb; entirely independent (no link to AYShader / AYScript / AYGraph).
//
// Grammar (low → high precedence):
//   1. Or   (||)
//   2. And  (&&)
//   3. Not  (!)   unary
//   4. Compare (>, <, ==, !=)
//   5. Primary (literal, ident, paren)
//
// Failure mode: lexer pushes Unknown token + records diagnostic; parser
// records first error and returns nullptr. Caller (Transition::evaluate-
// Condition) fails soft — no assert, no throw, evaluate returns false
// (INV-33).

#include <ayanimation/ConditionExpr.h>
#include <ayanimation/ConditionParser.h>

#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
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
    if (name == "CurrentStateTime") {
        return ctx.currentStateTime;
    }
    if (ctx.params == nullptr) return 0.0f;       // INV-23 fail-soft
    auto it = ctx.params->find(name);
    if (it == ctx.params->end()) return 0.0f;     // INV-23 fail-soft
    return it->second;
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
    if (op != CondOp::Not) return false;
    return !operand->evaluate(ctx);
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

        case CondOp::Not:
            // Not is unary only; binary Not in this arm is undefined.
            return false;
    }
    return false;
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

            // Number literal (including leading '-' for negative literals).
            if (std::isdigit(static_cast<unsigned char>(c)) ||
                (c == '-' && i + 1 < _src.size() &&
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
        if (_pos < _tokens.size() && _tokens[_pos].kind == Kind::Not) {
            ++_pos;  // consume '!'
            auto operand = parseUnary();   // right-associative
            if (operand == nullptr) return nullptr;
            return std::make_unique<CondUnaryExpr>(CondOp::Not, std::move(operand));
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
            // Unary Not is handled separately in parseUnary.
            case Kind::GT:
            case Kind::LT:
            case Kind::EQ:
            case Kind::NE:
                return 4;
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
            default:        return CondOp::Not;
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

} // namespace ayt::anim