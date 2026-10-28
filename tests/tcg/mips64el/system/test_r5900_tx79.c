/*
 * Test the TX79 instructions decoded by target/mips/tcg/tx79.decode.
 *
 * The instruction words are emitted directly because the normal MIPS64
 * assembler does not know the TX79 mnemonics.  LQ and SQ are used to move
 * values into and out of the emulated 128-bit GPRs.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t lo;
    uint64_t hi;
} __attribute__((aligned(16))) tx79_qword;

#define TX79_STR1(x) #x
#define TX79_STR(x) TX79_STR1(x)

#define TX79_R(rs, rt, rd, sub, func) \
    ((0x1cU << 26) | ((rs) << 21) | ((rt) << 16) | ((rd) << 11) | \
     ((sub) << 6) | (func))
#define TX79_LDST(op, base, rt, offset) \
    (((op) << 26) | ((base) << 21) | ((rt) << 16) | ((offset) & 0xffffU))

#define TX79_BINOP_WORD(INSN, A, B)                                      \
    ({                                                                   \
        tx79_qword _result;                                              \
        asm volatile(                                                     \
            "move $7, %[a]\n"                                           \
            ".word " TX79_STR(TX79_LDST(0x1e, 7, 8, 0)) "\n"           \
            "move $7, %[b]\n"                                           \
            ".word " TX79_STR(TX79_LDST(0x1e, 7, 9, 0)) "\n"           \
            "move $7, %[a]\n"                                           \
            ".word " TX79_STR(TX79_LDST(0x1e, 7, 10, 0)) "\n"          \
            ".word " TX79_STR(INSN) "\n"                                \
            "move $7, %[result]\n"                                      \
            ".word " TX79_STR(TX79_LDST(0x1f, 7, 10, 0)) "\n"          \
            :                                                             \
            : [a] "r" (A), [b] "r" (B), [result] "r" (&_result)        \
            : "$7", "$8", "$9", "$10", "memory");                    \
        _result;                                                          \
    })

#define TX79_UNARY_WORD(INSN, A)                                         \
    ({                                                                   \
        tx79_qword _result;                                              \
        asm volatile(                                                     \
            "move $7, %[a]\n"                                           \
            ".word " TX79_STR(TX79_LDST(0x1e, 7, 8, 0)) "\n"           \
            ".word " TX79_STR(INSN) "\n"                                \
            "move $7, %[result]\n"                                      \
            ".word " TX79_STR(TX79_LDST(0x1f, 7, 10, 0)) "\n"          \
            :                                                             \
            : [a] "r" (A), [result] "r" (&_result)                      \
            : "$7", "$8", "$10", "memory");                            \
        _result;                                                          \
    })

#define TX79_LQ_OFFSET(OFFSET, BASE)                                     \
    ({                                                                   \
        tx79_qword _result;                                              \
        asm volatile(                                                     \
            "move $7, %[base]\n"                                        \
            ".word " TX79_STR(TX79_LDST(0x1e, 7, 8, OFFSET)) "\n"      \
            "move $7, %[result]\n"                                      \
            ".word " TX79_STR(TX79_LDST(0x1f, 7, 8, 0)) "\n"           \
            :                                                             \
            : [base] "r" (BASE), [result] "r" (&_result)                \
            : "$7", "$8", "memory");                                   \
        _result;                                                          \
    })

#define TX79_SQ_OFFSET(OFFSET, VALUE, BASE)                               \
    do {                                                                   \
        asm volatile(                                                       \
            "move $7, %[value]\n"                                        \
            ".word " TX79_STR(TX79_LDST(0x1e, 7, 8, 0)) "\n"            \
            "move $7, %[base]\n"                                         \
            ".word " TX79_STR(TX79_LDST(0x1f, 7, 8, OFFSET)) "\n"        \
            :                                                               \
            : [value] "r" (VALUE), [base] "r" (BASE)                     \
            : "$7", "$8", "memory");                                    \
    } while (0)

static int check_qword(const char *name, tx79_qword got, tx79_qword expected)
{
    if (got.lo == expected.lo && got.hi == expected.hi) {
        return 0;
    }

    fprintf(stderr, "%s: got %016llx:%016llx expected %016llx:%016llx\n",
            name, (unsigned long long)got.hi, (unsigned long long)got.lo,
            (unsigned long long)expected.hi,
            (unsigned long long)expected.lo);
    return 1;
}

static uint64_t lane_mask(unsigned bits)
{
    return (1ULL << bits) - 1;
}

static int64_t sign_extend_lane(uint64_t value, unsigned bits)
{
    uint64_t sign = 1ULL << (bits - 1);

    value &= lane_mask(bits);
    return (int64_t)((value ^ sign) - sign);
}

static tx79_qword ref_sub(tx79_qword a, tx79_qword b, unsigned bits)
{
    tx79_qword result = { 0, 0 };
    uint64_t mask = lane_mask(bits);

    for (unsigned half = 0; half < 2; half++) {
        uint64_t av = half ? a.hi : a.lo;
        uint64_t bv = half ? b.hi : b.lo;
        uint64_t *dst = half ? &result.hi : &result.lo;

        for (unsigned lane = 0; lane < 64 / bits; lane++) {
            uint64_t shift = lane * bits;
            *dst |= (((av >> shift) - (bv >> shift)) & mask) << shift;
        }
    }

    return result;
}

static tx79_qword ref_compare(tx79_qword a, tx79_qword b, unsigned bits,
                              int equal)
{
    tx79_qword result = { 0, 0 };
    uint64_t mask = lane_mask(bits);

    for (unsigned half = 0; half < 2; half++) {
        uint64_t av = half ? a.hi : a.lo;
        uint64_t bv = half ? b.hi : b.lo;
        uint64_t *dst = half ? &result.hi : &result.lo;

        for (unsigned lane = 0; lane < 64 / bits; lane++) {
            uint64_t shift = lane * bits;
            int64_t as = sign_extend_lane(av >> shift, bits);
            int64_t bs = sign_extend_lane(bv >> shift, bits);
            int match = equal ? as == bs : as > bs;

            if (match) {
                *dst |= mask << shift;
            }
        }
    }

    return result;
}

static tx79_qword ref_pext(tx79_qword a, tx79_qword b, unsigned bits,
                           int upper)
{
    tx79_qword result = { 0, 0 };
    uint64_t av = upper ? a.hi : a.lo;
    uint64_t bv = upper ? b.hi : b.lo;

    for (unsigned lane = 0; lane < 64 / (2 * bits); lane++) {
        uint64_t shift = lane * bits;
        uint64_t dst_shift = lane * 2 * bits;
        uint64_t mask = lane_mask(bits);

        result.lo |= ((bv >> shift) & mask) << dst_shift;
        result.lo |= ((av >> shift) & mask) << (dst_shift + bits);
        result.hi |= ((bv >> (shift + 32)) & mask) << dst_shift;
        result.hi |= ((av >> (shift + 32)) & mask) << (dst_shift + bits);
    }

    return result;
}

static tx79_qword ref_logic(tx79_qword a, tx79_qword b, char op)
{
    tx79_qword result;

    switch (op) {
    case '&':
        result.lo = a.lo & b.lo;
        result.hi = a.hi & b.hi;
        break;
    case '|':
        result.lo = a.lo | b.lo;
        result.hi = a.hi | b.hi;
        break;
    case '^':
        result.lo = a.lo ^ b.lo;
        result.hi = a.hi ^ b.hi;
        break;
    default:
        result.lo = ~(a.lo | b.lo);
        result.hi = ~(a.hi | b.hi);
        break;
    }

    return result;
}

static tx79_qword ref_pcpyh(tx79_qword a)
{
    uint64_t lo = a.lo & 0xffff;
    uint64_t hi = a.hi & 0xffff;
    tx79_qword result = {
        lo * 0x0001000100010001ULL,
        hi * 0x0001000100010001ULL,
    };

    return result;
}

static tx79_qword ref_prot3w(tx79_qword a)
{
    tx79_qword result = {
        (a.lo >> 32) | ((a.hi & 0xffffffff) << 32),
        (a.lo & 0xffffffff) | (a.hi & 0xffffffff00000000ULL),
    };

    return result;
}

static void tx79_set_hilo1(uint64_t hi, uint64_t lo)
{
    asm volatile(
        "move $8, %[lo]\n"
        ".word " TX79_STR(TX79_R(8, 0, 0, 0, 0x13)) "\n"
        "move $8, %[hi]\n"
        ".word " TX79_STR(TX79_R(8, 0, 0, 0, 0x11)) "\n"
        :
        : [lo] "r" (lo), [hi] "r" (hi)
        : "$8");
}

static tx79_qword tx79_get_hilo1(void)
{
    tx79_qword result;

    asm volatile(
        ".word " TX79_STR(TX79_R(0, 0, 8, 0, 0x12)) "\n"
        "move %[lo], $8\n"
        ".word " TX79_STR(TX79_R(0, 0, 8, 0, 0x10)) "\n"
        "move %[hi], $8\n"
        : [lo] "=r" (result.lo), [hi] "=r" (result.hi)
        :
        : "$8");

    return result;
}

static int test_hilo1(void)
{
    const uint64_t hi = 0x8899aabbccddeeffULL;
    const uint64_t lo = 0x0011223344556677ULL;
    tx79_qword got;

    tx79_set_hilo1(hi, lo);
    got = tx79_get_hilo1();
    if (got.lo != lo || got.hi != hi) {
        fprintf(stderr, "HI1/LO1 move instructions failed\n");
        return 1;
    }

    return 0;
}

static int test_parallel(void)
{
    const tx79_qword a = {
        0x800000007fffffffULL,
        0xff0080007fff8000ULL,
    };
    const tx79_qword b = {
        0x00000000ffffffffULL,
        0x01008000ffff0001ULL,
    };
    tx79_qword got;

    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 9, 8), &a, &b);
    if (check_qword("PSUBB", got, ref_sub(a, b, 8))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 5, 8), &a, &b);
    if (check_qword("PSUBH", got, ref_sub(a, b, 16))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 1, 8), &a, &b);
    if (check_qword("PSUBW", got, ref_sub(a, b, 32))) {
        return 1;
    }

    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 2, 8), &a, &b);
    if (check_qword("PCGTW", got, ref_compare(a, b, 32, 0))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 6, 8), &a, &b);
    if (check_qword("PCGTH", got, ref_compare(a, b, 16, 0))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 10, 8), &a, &b);
    if (check_qword("PCGTB", got, ref_compare(a, b, 8, 0))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 2, 40), &a, &b);
    if (check_qword("PCEQW", got, ref_compare(a, b, 32, 1))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 6, 40), &a, &b);
    if (check_qword("PCEQH", got, ref_compare(a, b, 16, 1))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 10, 40), &a, &b);
    if (check_qword("PCEQB", got, ref_compare(a, b, 8, 1))) {
        return 1;
    }

    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 18, 8), &a, &b);
    if (check_qword("PEXTLW", got, ref_pext(a, b, 32, 0))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 22, 8), &a, &b);
    if (check_qword("PEXTLH", got, ref_pext(a, b, 16, 0))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 26, 8), &a, &b);
    if (check_qword("PEXTLB", got, ref_pext(a, b, 8, 0))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 18, 40), &a, &b);
    if (check_qword("PEXTUW", got, ref_pext(a, b, 32, 1))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 19, 8), &a, &b);
    if (check_qword("PPACW", got, (tx79_qword) {
            (uint32_t)b.lo | ((uint64_t)(uint32_t)b.hi << 32),
            (uint32_t)a.lo | ((uint64_t)(uint32_t)a.hi << 32),
        })) {
        return 1;
    }

    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 18, 9), &a, &b);
    if (check_qword("PAND", got, ref_logic(a, b, '&'))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 19, 9), &a, &b);
    if (check_qword("PXOR", got, ref_logic(a, b, '^'))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 18, 41), &a, &b);
    if (check_qword("POR", got, ref_logic(a, b, '|'))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 19, 41), &a, &b);
    if (check_qword("PNOR", got, ref_logic(a, b, 'n'))) {
        return 1;
    }

    got = TX79_UNARY_WORD(TX79_R(0, 8, 10, 27, 41), &a);
    if (check_qword("PCPYH", got, ref_pcpyh(a))) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 14, 9), &a, &b);
    if (check_qword("PCPYLD", got, (tx79_qword) { b.lo, a.lo })) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 9, 10, 14, 41), &a, &b);
    if (check_qword("PCPYUD", got, (tx79_qword) { a.hi, b.hi })) {
        return 1;
    }
    got = TX79_UNARY_WORD(TX79_R(0, 8, 10, 31, 9), &a);
    if (check_qword("PROT3W", got, ref_prot3w(a))) {
        return 1;
    }

    got = TX79_BINOP_WORD(TX79_R(0, 9, 10, 14, 9), &a, &b);
    if (check_qword("PCPYLD rt-only", got, (tx79_qword) { b.lo, 0 })) {
        return 1;
    }
    got = TX79_BINOP_WORD(TX79_R(8, 0, 10, 14, 41), &a, &b);
    if (check_qword("PCPYUD rs-only", got, (tx79_qword) { a.hi, 0 })) {
        return 1;
    }
    got = TX79_UNARY_WORD(TX79_R(0, 0, 10, 27, 41), &a);
    if (check_qword("PCPYH zero", got, (tx79_qword) { 0, 0 })) {
        return 1;
    }

    got = TX79_BINOP_WORD(TX79_R(8, 9, 0, 1, 8), &a, &b);
    if (check_qword("rd zero", got, (tx79_qword) {
            a.lo, a.hi,
        })) {
        return 1;
    }

    return 0;
}

static int test_quadword_memory(void)
{
    static uint8_t memory[65536 + 64] __attribute__((aligned(16)));
    static const int offsets[] = { -32768, -1, 0, 1, 32767 };
    const tx79_qword value = {
        0x0123456789abcdefULL,
        0xfedcba9876543210ULL,
    };
    uint8_t *base = memory + 32784;

    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        int offset = offsets[i];
        uintptr_t address = (uintptr_t)base + (intptr_t)offset;
        uint8_t *aligned = (uint8_t *)(address & ~(uintptr_t)0xf);
        tx79_qword got;

        memset(memory, 0, sizeof(memory));
        memcpy(aligned, &value, sizeof(value));
        switch (offset) {
        case -32768:
            got = TX79_LQ_OFFSET(-32768, base);
            break;
        case -1:
            got = TX79_LQ_OFFSET(-1, base);
            break;
        case 0:
            got = TX79_LQ_OFFSET(0, base);
            break;
        case 1:
            got = TX79_LQ_OFFSET(1, base);
            break;
        default:
            got = TX79_LQ_OFFSET(32767, base);
            break;
        }
        if (check_qword("LQ offset", got, value)) {
            return 1;
        }

        memset(memory, 0, sizeof(memory));
        switch (offset) {
        case -32768:
            TX79_SQ_OFFSET(-32768, &value, base);
            break;
        case -1:
            TX79_SQ_OFFSET(-1, &value, base);
            break;
        case 0:
            TX79_SQ_OFFSET(0, &value, base);
            break;
        case 1:
            TX79_SQ_OFFSET(1, &value, base);
            break;
        default:
            TX79_SQ_OFFSET(32767, &value, base);
            break;
        }
        if (memcmp(aligned, &value, sizeof(value)) != 0) {
            fprintf(stderr, "SQ offset %d failed\n", offset);
            return 1;
        }
    }

    return 0;
}

int main(void)
{
    if (test_hilo1() || test_parallel() || test_quadword_memory()) {
        return 1;
    }

    return 0;
}
