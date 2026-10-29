/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MIPS64R6 tests: logical */

#define main test_mips64r6_and
#include "test_mips64r6_and.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_nor
#include "test_mips64r6_nor.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_or
#include "test_mips64r6_or.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_xor
#include "test_mips64r6_xor.c"
#undef main
#undef TEST_COUNT_TOTAL

static const struct {
    const char *name;
    int32_t (*fn)(void);
} r6_tests[] = {
    { "and", test_mips64r6_and },
    { "nor", test_mips64r6_nor },
    { "or", test_mips64r6_or },
    { "xor", test_mips64r6_xor },
};

int main(void)
{
    unsigned count = sizeof(r6_tests) / sizeof(r6_tests[0]);
    int fail = 0;
    unsigned i;

    printf("TAP version 14\n1..%u\n", count);
    for (i = 0; i < count; i++) {
        int r = r6_tests[i].fn();

        printf("%s %u - MIPS64R6 logical %s\n", r ? "not ok" : "ok", i + 1,
               r6_tests[i].name);
        fail |= r;
    }

    return fail ? 1 : 0;
}
