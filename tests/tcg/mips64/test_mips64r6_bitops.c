/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MIPS64R6 tests: bit count and bit swap */

#define main test_mips64r6_bitswap
#include "test_mips64r6_bitswap.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dbitswap
#include "test_mips64r6_dbitswap.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_clo
#include "test_mips64r6_clo.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_clz
#include "test_mips64r6_clz.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dclo
#include "test_mips64r6_dclo.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dclz
#include "test_mips64r6_dclz.c"
#undef main
#undef TEST_COUNT_TOTAL

static const struct {
    const char *name;
    int32_t (*fn)(void);
} r6_tests[] = {
    { "bitswap", test_mips64r6_bitswap },
    { "dbitswap", test_mips64r6_dbitswap },
    { "clo", test_mips64r6_clo },
    { "clz", test_mips64r6_clz },
    { "dclo", test_mips64r6_dclo },
    { "dclz", test_mips64r6_dclz },
};

int main(void)
{
    unsigned count = sizeof(r6_tests) / sizeof(r6_tests[0]);
    int fail = 0;
    unsigned i;

    printf("TAP version 14\n1..%u\n", count);
    for (i = 0; i < count; i++) {
        int r = r6_tests[i].fn();

        printf("%s %u - MIPS64R6 bit count and bit swap %s\n", r ? "not ok" : "ok", i + 1,
               r6_tests[i].name);
        fail |= r;
    }

    return fail ? 1 : 0;
}
