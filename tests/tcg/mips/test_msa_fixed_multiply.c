/*
 * MIPS MSA tests: Fixed Multiply instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define main test_msa_madd_q_h
#include "user/ase/msa/fixed-multiply/test_msa_madd_q_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_madd_q_w
#include "user/ase/msa/fixed-multiply/test_msa_madd_q_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_maddr_q_h
#include "user/ase/msa/fixed-multiply/test_msa_maddr_q_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_maddr_q_w
#include "user/ase/msa/fixed-multiply/test_msa_maddr_q_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_msub_q_h
#include "user/ase/msa/fixed-multiply/test_msa_msub_q_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_msub_q_w
#include "user/ase/msa/fixed-multiply/test_msa_msub_q_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_msubr_q_h
#include "user/ase/msa/fixed-multiply/test_msa_msubr_q_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_msubr_q_w
#include "user/ase/msa/fixed-multiply/test_msa_msubr_q_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mul_q_h
#include "user/ase/msa/fixed-multiply/test_msa_mul_q_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mul_q_w
#include "user/ase/msa/fixed-multiply/test_msa_mul_q_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mulr_q_h
#include "user/ase/msa/fixed-multiply/test_msa_mulr_q_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mulr_q_w
#include "user/ase/msa/fixed-multiply/test_msa_mulr_q_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "madd.q.h", test_msa_madd_q_h },
    { "madd.q.w", test_msa_madd_q_w },
    { "maddr.q.h", test_msa_maddr_q_h },
    { "maddr.q.w", test_msa_maddr_q_w },
    { "msub.q.h", test_msa_msub_q_h },
    { "msub.q.w", test_msa_msub_q_w },
    { "msubr.q.h", test_msa_msubr_q_h },
    { "msubr.q.w", test_msa_msubr_q_w },
    { "mul.q.h", test_msa_mul_q_h },
    { "mul.q.w", test_msa_mul_q_w },
    { "mulr.q.h", test_msa_mulr_q_h },
    { "mulr.q.w", test_msa_mulr_q_w },
};

int main(void)
{
    int fail = 0;
    unsigned i;

    printf("TAP version 14\n"
           "1..%u\n",
           (unsigned)(sizeof(msa_tests) / sizeof(msa_tests[0])));
    for (i = 0; i < sizeof(msa_tests) / sizeof(msa_tests[0]); i++) {
        int r = msa_tests[i].fn();

        printf("%s %u - MSA %s %s\n", r ? "not ok" : "ok", i + 1,
               "Fixed Multiply", msa_tests[i].name);
        fail |= r;
    }

    return fail ? 1 : 0;
}
