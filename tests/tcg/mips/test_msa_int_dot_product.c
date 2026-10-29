/*
 * MIPS MSA tests: Int Dot Product instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define main test_msa_dotp_s_d
#include "user/ase/msa/int-dot-product/test_msa_dotp_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dotp_s_h
#include "user/ase/msa/int-dot-product/test_msa_dotp_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dotp_s_w
#include "user/ase/msa/int-dot-product/test_msa_dotp_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dotp_u_d
#include "user/ase/msa/int-dot-product/test_msa_dotp_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dotp_u_h
#include "user/ase/msa/int-dot-product/test_msa_dotp_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dotp_u_w
#include "user/ase/msa/int-dot-product/test_msa_dotp_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dpadd_s_d
#include "user/ase/msa/int-dot-product/test_msa_dpadd_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dpadd_s_h
#include "user/ase/msa/int-dot-product/test_msa_dpadd_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dpadd_s_w
#include "user/ase/msa/int-dot-product/test_msa_dpadd_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dpadd_u_d
#include "user/ase/msa/int-dot-product/test_msa_dpadd_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dpadd_u_h
#include "user/ase/msa/int-dot-product/test_msa_dpadd_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dpadd_u_w
#include "user/ase/msa/int-dot-product/test_msa_dpadd_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dpsub_s_d
#include "user/ase/msa/int-dot-product/test_msa_dpsub_s_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dpsub_s_h
#include "user/ase/msa/int-dot-product/test_msa_dpsub_s_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dpsub_s_w
#include "user/ase/msa/int-dot-product/test_msa_dpsub_s_w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dpsub_u_d
#include "user/ase/msa/int-dot-product/test_msa_dpsub_u_d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dpsub_u_h
#include "user/ase/msa/int-dot-product/test_msa_dpsub_u_h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_msa_dpsub_u_w
#include "user/ase/msa/int-dot-product/test_msa_dpsub_u_w.c"
#undef main
#undef TEST_COUNT_TOTAL


static const struct {
    const char *name;
    int32_t (*fn)(void);
} msa_tests[] = {
    { "dotp.s.d", test_msa_dotp_s_d },
    { "dotp.s.h", test_msa_dotp_s_h },
    { "dotp.s.w", test_msa_dotp_s_w },
    { "dotp.u.d", test_msa_dotp_u_d },
    { "dotp.u.h", test_msa_dotp_u_h },
    { "dotp.u.w", test_msa_dotp_u_w },
    { "dpadd.s.d", test_msa_dpadd_s_d },
    { "dpadd.s.h", test_msa_dpadd_s_h },
    { "dpadd.s.w", test_msa_dpadd_s_w },
    { "dpadd.u.d", test_msa_dpadd_u_d },
    { "dpadd.u.h", test_msa_dpadd_u_h },
    { "dpadd.u.w", test_msa_dpadd_u_w },
    { "dpsub.s.d", test_msa_dpsub_s_d },
    { "dpsub.s.h", test_msa_dpsub_s_h },
    { "dpsub.s.w", test_msa_dpsub_s_w },
    { "dpsub.u.d", test_msa_dpsub_u_d },
    { "dpsub.u.h", test_msa_dpsub_u_h },
    { "dpsub.u.w", test_msa_dpsub_u_w },
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
               "Int Dot Product", msa_tests[i].name);
        fail |= r;
    }

    return fail ? 1 : 0;
}
