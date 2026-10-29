/*
 * MIPS MSA tests: Int Average instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define main test_msa_ave_s_b
#include "user/ase/msa/int-average/test_msa_ave_s_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_s_d
#include "user/ase/msa/int-average/test_msa_ave_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_s_h
#include "user/ase/msa/int-average/test_msa_ave_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_s_w
#include "user/ase/msa/int-average/test_msa_ave_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_u_b
#include "user/ase/msa/int-average/test_msa_ave_u_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_u_d
#include "user/ase/msa/int-average/test_msa_ave_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_u_h
#include "user/ase/msa/int-average/test_msa_ave_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_ave_u_w
#include "user/ase/msa/int-average/test_msa_ave_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_s_b
#include "user/ase/msa/int-average/test_msa_aver_s_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_s_d
#include "user/ase/msa/int-average/test_msa_aver_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_s_h
#include "user/ase/msa/int-average/test_msa_aver_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_s_w
#include "user/ase/msa/int-average/test_msa_aver_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_u_b
#include "user/ase/msa/int-average/test_msa_aver_u_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_u_d
#include "user/ase/msa/int-average/test_msa_aver_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_u_h
#include "user/ase/msa/int-average/test_msa_aver_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_aver_u_w
#include "user/ase/msa/int-average/test_msa_aver_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "ave.s.b", test_msa_ave_s_b },
    { "ave.s.d", test_msa_ave_s_d },
    { "ave.s.h", test_msa_ave_s_h },
    { "ave.s.w", test_msa_ave_s_w },
    { "ave.u.b", test_msa_ave_u_b },
    { "ave.u.d", test_msa_ave_u_d },
    { "ave.u.h", test_msa_ave_u_h },
    { "ave.u.w", test_msa_ave_u_w },
    { "aver.s.b", test_msa_aver_s_b },
    { "aver.s.d", test_msa_aver_s_d },
    { "aver.s.h", test_msa_aver_s_h },
    { "aver.s.w", test_msa_aver_s_w },
    { "aver.u.b", test_msa_aver_u_b },
    { "aver.u.d", test_msa_aver_u_d },
    { "aver.u.h", test_msa_aver_u_h },
    { "aver.u.w", test_msa_aver_u_w },
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
               "Int Average", msa_tests[i].name);
        fail |= r;
    }

    return fail ? 1 : 0;
}
