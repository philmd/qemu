/*
 * MIPS MSA tests: Int Divide and Modulo instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define main test_msa_div_s_b
#include "user/ase/msa/int-divide/test_msa_div_s_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_s_d
#include "user/ase/msa/int-divide/test_msa_div_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_s_h
#include "user/ase/msa/int-divide/test_msa_div_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_s_w
#include "user/ase/msa/int-divide/test_msa_div_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_u_b
#include "user/ase/msa/int-divide/test_msa_div_u_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_u_d
#include "user/ase/msa/int-divide/test_msa_div_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_u_h
#include "user/ase/msa/int-divide/test_msa_div_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_div_u_w
#include "user/ase/msa/int-divide/test_msa_div_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_s_b
#include "user/ase/msa/int-modulo/test_msa_mod_s_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_s_d
#include "user/ase/msa/int-modulo/test_msa_mod_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_s_h
#include "user/ase/msa/int-modulo/test_msa_mod_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_s_w
#include "user/ase/msa/int-modulo/test_msa_mod_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_u_b
#include "user/ase/msa/int-modulo/test_msa_mod_u_b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_u_d
#include "user/ase/msa/int-modulo/test_msa_mod_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_u_h
#include "user/ase/msa/int-modulo/test_msa_mod_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_mod_u_w
#include "user/ase/msa/int-modulo/test_msa_mod_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "div.s.b", test_msa_div_s_b },
    { "div.s.d", test_msa_div_s_d },
    { "div.s.h", test_msa_div_s_h },
    { "div.s.w", test_msa_div_s_w },
    { "div.u.b", test_msa_div_u_b },
    { "div.u.d", test_msa_div_u_d },
    { "div.u.h", test_msa_div_u_h },
    { "div.u.w", test_msa_div_u_w },
    { "mod.s.b", test_msa_mod_s_b },
    { "mod.s.d", test_msa_mod_s_d },
    { "mod.s.h", test_msa_mod_s_h },
    { "mod.s.w", test_msa_mod_s_w },
    { "mod.u.b", test_msa_mod_u_b },
    { "mod.u.d", test_msa_mod_u_d },
    { "mod.u.h", test_msa_mod_u_h },
    { "mod.u.w", test_msa_mod_u_w },
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
               "Int Divide and Modulo", msa_tests[i].name);
        fail |= r;
    }

    return fail ? 1 : 0;
}
