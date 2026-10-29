/*
 * MIPS32R6 tests: logical instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>

#include "../mips/include/wrappers_mips32r6.h"
#include "../mips/include/test_inputs_32.h"

typedef void (*binary_op_32)(const uint32_t *, const uint32_t *, uint32_t *);
typedef uint32_t (*ref_op_32)(uint32_t, uint32_t);

static uint32_t ref_and(uint32_t a, uint32_t b) { return a & b; }
static uint32_t ref_nor(uint32_t a, uint32_t b) { return ~(a | b); }
static uint32_t ref_or(uint32_t a, uint32_t b) { return a | b; }
static uint32_t ref_xor(uint32_t a, uint32_t b) { return a ^ b; }

static int check_op(const char *name, binary_op_32 op, ref_op_32 ref,
                    const uint32_t *input, size_t count)
{
    int fail = 0;

    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < count; j++) {
            uint32_t a = input[i], b = input[j], res = 0;

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

static int run(const char *name, binary_op_32 op, ref_op_32 ref)
{
    static int count;
    int ret = 0;

    ret |= check_op(name, op, ref, b32_pattern, PATTERN_INPUTS_32_SHORT_COUNT);
    ret |= check_op(name, op, ref, b32_random, RANDOM_INPUTS_32_SHORT_COUNT);
    printf("%s %d - MIPS32R6 logic %s\n", ret ? "not ok" : "ok", ++count, name);
    return ret;
}

int main(void)
{
    int ret = 0;

    printf("TAP version 14\n1..4\n");
    ret |= run("AND", do_mips32r6_AND, ref_and);
    ret |= run("NOR", do_mips32r6_NOR, ref_nor);
    ret |= run("OR", do_mips32r6_OR, ref_or);
    ret |= run("XOR", do_mips32r6_XOR, ref_xor);

    return ret ? 1 : 0;
}
