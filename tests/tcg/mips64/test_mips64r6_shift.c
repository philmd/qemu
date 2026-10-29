/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MIPS64R6 tests: variable shift */

#define main test_mips64r6_sllv
#include "test_mips64r6_sllv.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_srav
#include "test_mips64r6_srav.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_srlv
#include "test_mips64r6_srlv.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dsllv
#include "test_mips64r6_dsllv.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dsrav
#include "test_mips64r6_dsrav.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dsrlv
#include "test_mips64r6_dsrlv.c"
#undef main
#undef TEST_COUNT_TOTAL

static const struct {
    const char *name;
    int32_t (*fn)(void);
} r6_tests[] = {
    { "sllv", test_mips64r6_sllv },
    { "srav", test_mips64r6_srav },
    { "srlv", test_mips64r6_srlv },
    { "dsllv", test_mips64r6_dsllv },
    { "dsrav", test_mips64r6_dsrav },
    { "dsrlv", test_mips64r6_dsrlv },
};

int main(void)
{
    unsigned count = sizeof(r6_tests) / sizeof(r6_tests[0]);
    int fail = 0;
    unsigned i;

    printf("TAP version 14\n1..%u\n", count);
    for (i = 0; i < count; i++) {
        int r = r6_tests[i].fn();

        printf("%s %u - MIPS64R6 variable shift %s\n", r ? "not ok" : "ok", i + 1,
               r6_tests[i].name);
        fail |= r;
    }

    return fail ? 1 : 0;
}
