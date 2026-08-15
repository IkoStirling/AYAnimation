// CondBytecode.cpp — P2 polish (2026-08-07) Program-counter switch evaluator.
//                    P5 polish (2026-08-10) arithmetic ops OP_ADD..OP_NEG.
//
// Walks the flat opcode stream emitted by compileToBytecode() (in
// ConditionParser.cpp). Single switch dispatch (no virtual calls); operands
// are read directly from the program stream in fixed positions matching the
// encoding documented in AYAnimation/CondBytecode.h.
//
// Stack machine: a FIXED-SIZE float stack array (capacity 16; AST depth
// ≤ 5 production). No heap allocation on the hot path — the earlier
// std::vector<float> stack with reserve(8) cost ~8x in the debug
// benchmark (1133 ns vs 144 ns AST).
//
// On any opcode read error / unknown opcode / stack underflow / overflow,
// returns false (fail-soft — mirrors INV-33 philosophy: never crash, never
// throw).

#include <AYAnimation/CondBytecode.h>
#include <AYAnimation/ConditionExpr.h>  // full ConditionEvalCtx def

#include <cmath>
#include <cstdint>
#include <cstring>

namespace ayt::anim
{
namespace
{
constexpr std::size_t kStackCapacity = 16;
}

bool CondBytecode::evaluate(const ConditionEvalCtx& ctx) const {
    // Fixed array stack — depth ≤ AST depth ≤ 5 production; +11 safety.
    // sp is the next free slot; guard pushes with sp >= kStackCapacity
    // (fail-soft, never write past the array).
    float stack[kStackCapacity];
    std::size_t sp = 0;

    const uint8_t* pc   = program.data();
    const uint8_t* end  = pc + program.size();

    while (pc < end) {
        const CondOpByte op = static_cast<CondOpByte>(*pc++);
        switch (op) {
            // === Binary logical (with short-circuit encoding) ==========
            // Encoding: OP_AND/OR, int8_t relJump  (signed right-subtree
            // byte count). Left evaluated first; if decisive, pc advances
            // by relJump bytes to skip the right subtree (INV-58).
            case CondOpByte::OP_AND: {
                if (pc >= end || sp == 0) return false;   // malformed/underflow
                const int8_t relJump = static_cast<int8_t>(*pc++);
                const bool left = (stack[sp - 1] != 0.0f);
                --sp;
                if (!left) {
                    // Short-circuit: skip right subtree.
                    pc += relJump;
                    stack[sp++] = 0.0f;                   // false
                } else {
                    // Fall through into right subtree — already in program
                    // stream at current pc; loop continues.
                }
                break;
            }
            case CondOpByte::OP_OR: {
                if (pc >= end || sp == 0) return false;
                const int8_t relJump = static_cast<int8_t>(*pc++);
                const bool left = (stack[sp - 1] != 0.0f);
                --sp;
                if (left) {
                    pc += relJump;                        // short-circuit skip
                    stack[sp++] = 1.0f;                   // true
                }
                break;
            }

            // === Unary logical =========================================
            case CondOpByte::OP_NOT: {
                if (sp == 0) return false;
                stack[sp - 1] = (stack[sp - 1] != 0.0f) ? 0.0f : 1.0f;
                break;
            }

            // === Arithmetic (P5 polish, INV-68) =========================
            // Same stack shape as comparison: pops 2, pushes 1. Net -1
            // per op — never risks the fixed-stack capacity.
            case CondOpByte::OP_ADD: {
                if (sp < 2) return false;
                const float b = stack[--sp];
                stack[sp - 1] += b;
                break;
            }
            case CondOpByte::OP_SUB: {
                if (sp < 2) return false;
                const float b = stack[--sp];
                stack[sp - 1] -= b;
                break;
            }
            case CondOpByte::OP_MUL: {
                if (sp < 2) return false;
                const float b = stack[--sp];
                stack[sp - 1] *= b;
                break;
            }
            case CondOpByte::OP_DIV: {
                if (sp < 2) return false;
                const float b = stack[--sp];
                // INV-67 — div-by-zero fail-soft (0.0f), mirrors AST arm.
                // Never inf/nan (would poison downstream fabs-epsilon
                // comparisons).
                stack[sp - 1] = (b == 0.0f) ? 0.0f : stack[sp - 1] / b;
                break;
            }

            // === Unary arithmetic (P5 polish, INV-66) ==================
            case CondOpByte::OP_NEG: {
                if (sp == 0) return false;
                stack[sp - 1] = -stack[sp - 1];
                break;
            }

            // === Comparison (no operand; pops 2, pushes 1) =============
            case CondOpByte::OP_GT: {
                if (sp < 2) return false;
                const float b = stack[--sp];
                const float a = stack[sp - 1];
                stack[sp - 1] = (a > b ? 1.0f : 0.0f);
                break;
            }
            case CondOpByte::OP_LT: {
                if (sp < 2) return false;
                const float b = stack[--sp];
                const float a = stack[sp - 1];
                stack[sp - 1] = (a < b ? 1.0f : 0.0f);
                break;
            }
            case CondOpByte::OP_EQ: {
                if (sp < 2) return false;
                const float b = stack[--sp];
                const float a = stack[sp - 1];
                stack[sp - 1] = (std::fabs(a - b) < 1e-6f ? 1.0f : 0.0f);
                break;
            }
            case CondOpByte::OP_NE: {
                if (sp < 2) return false;
                const float b = stack[--sp];
                const float a = stack[sp - 1];
                stack[sp - 1] = (std::fabs(a - b) >= 1e-6f ? 1.0f : 0.0f);
                break;
            }

            // === OP_LOAD_PARAM (u32 hash) → ctx.params linear scan =====
            case CondOpByte::OP_LOAD_PARAM: {
                if (pc + 4 > end) return false;
                uint32_t hash;
                std::memcpy(&hash, pc, 4);
                pc += 4;
                float v = 0.0f;                           // INV-23 fail-soft
                if (ctx.params != nullptr && hash != 0) {
                    for (const auto& entry : *ctx.params) {
                        if (entry.hash == hash) { v = entry.value; break; }
                    }
                }
                if (sp >= kStackCapacity) return false;   // overflow fail-soft
                stack[sp++] = v;
                break;
            }

            // === OP_LOAD_LITERAL (u32 idx) → literals[] table lookup ==
            case CondOpByte::OP_LOAD_LITERAL: {
                if (pc + 4 > end) return false;
                uint32_t idx;
                std::memcpy(&idx, pc, 4);
                pc += 4;
                if (idx >= literals.size()) return false;  // malformed table idx
                if (sp >= kStackCapacity) return false;    // overflow fail-soft
                stack[sp++] = literals[idx];               // float, verbatim
                break;
            }

            // === OP_LOAD_RESERVED (u8 rid) → ctx.currentStateTime =====
            // INV-55 — reserved ident encoded as dedicated opcode. 0
            // string compare at eval time (vs P1 polish ~5 ns SSO).
            case CondOpByte::OP_LOAD_RESERVED: {
                if (pc >= end) return false;
                const uint8_t rid = *pc++;
                if (rid == static_cast<uint8_t>(CondReservedId::R_CURRENT_STATE_TIME)) {
                    if (sp >= kStackCapacity) return false;  // overflow fail-soft
                    stack[sp++] = ctx.currentStateTime;
                } else {
                    return false;                         // unknown rid → fail-soft
                }
                break;
            }
        }
    }

    if (sp != 1) return false;                             // malformed; final value
    return stack[0] != 0.0f;
}

} // namespace ayt::anim
