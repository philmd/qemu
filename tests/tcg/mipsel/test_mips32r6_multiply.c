/*
 * MIPS32R6 tests: multiply instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>

#include "../mips/include/wrappers_mips32r6.h"
#include "../mips/include/test_inputs_32.h"

typedef void (*binary_op_32)(const uint32_t *, const uint32_t *, uint32_t *);
typedef uint32_t (*ref_op_32)(uint32_t, uint32_t);
typedef int (*valid_op_32)(uint32_t, uint32_t);

static uint32_t ref_mul(uint32_t a, uint32_t b)
{
    return (uint32_t)((int64_t)(int32_t)a * (int32_t)b);
}

static uint32_t ref_muh(uint32_t a, uint32_t b)
{
    return (uint32_t)(((int64_t)(int32_t)a * (int32_t)b) >> 32);
}

static uint32_t ref_mulu(uint32_t a, uint32_t b)
{
    return (uint32_t)((uint64_t)a * b);
}

static uint32_t ref_muhu(uint32_t a, uint32_t b)
{
    return (uint32_t)(((uint64_t)a * b) >> 32);
}

/* Division by zero and INT_MIN / -1 have UNPREDICTABLE results. */
static int valid_udiv(uint32_t a, uint32_t b) { return b != 0; }
static int valid_sdiv(uint32_t a, uint32_t b)
{
    return b != 0 && !(a == 0x80000000u && b == 0xffffffffu);
}

static uint32_t ref_div(uint32_t a, uint32_t b)
{
    return (uint32_t)((int32_t)a / (int32_t)b);
}

static uint32_t ref_mod(uint32_t a, uint32_t b)
{
    return (uint32_t)((int32_t)a % (int32_t)b);
}

static uint32_t ref_divu(uint32_t a, uint32_t b) { return a / b; }
static uint32_t ref_modu(uint32_t a, uint32_t b) { return a % b; }

static int check_op(const char *name, binary_op_32 op, ref_op_32 ref,
                    valid_op_32 valid, const uint32_t *input, size_t count)
{
    int fail = 0;

    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < count; j++) {
            uint32_t a = input[i], b = input[j], res = 0;

            if (valid && !valid(a, b)) {
                continue;
            }
            op(&a, &b, &res);
            if (res != ref(a, b)) {
                printf("# %s: %08x, %08x: got %08x expected %08x\n",
                       name, a, b, res, ref(a, b));
                fail = 1;
            }
        }
    }
    return fail;
}

static int run(const char *name, binary_op_32 op, ref_op_32 ref,
               valid_op_32 valid)
{
    static int count;
    int ret = 0;

    ret |= check_op(name, op, ref, valid, b32_pattern, PATTERN_INPUTS_32_COUNT);
    ret |= check_op(name, op, ref, valid, b32_random, RANDOM_INPUTS_32_COUNT);
    printf("%s %d - MIPS32R6 multiply %s\n", ret ? "not ok" : "ok", ++count,
           name);
    return ret;
}

int main(void)
{
    int ret = 0;

    printf("TAP version 14\n1..8\n");
    ret |= run("MUL", do_mips32r6_MUL, ref_mul, NULL);
    ret |= run("MUH", do_mips32r6_MUH, ref_muh, NULL);
    ret |= run("MULU", do_mips32r6_MULU, ref_mulu, NULL);
    ret |= run("MUHU", do_mips32r6_MUHU, ref_muhu, NULL);
    ret |= run("DIV", do_mips32r6_DIV, ref_div, valid_sdiv);
    ret |= run("MOD", do_mips32r6_MOD, ref_mod, valid_sdiv);
    ret |= run("DIVU", do_mips32r6_DIVU, ref_divu, valid_udiv);
    ret |= run("MODU", do_mips32r6_MODU, ref_modu, valid_udiv);

    return ret ? 1 : 0;
}
