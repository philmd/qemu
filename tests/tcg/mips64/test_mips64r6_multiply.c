/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MIPS64R6 tests: multiply */

#define main test_mips64r6_mul
#include "test_mips64r6_mul.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_muh
#include "test_mips64r6_muh.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_mulu
#include "test_mips64r6_mulu.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_muhu
#include "test_mips64r6_muhu.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dmul
#include "test_mips64r6_dmul.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dmuh
#include "test_mips64r6_dmuh.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dmulu
#include "test_mips64r6_dmulu.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dmuhu
#include "test_mips64r6_dmuhu.c"
#undef main
#undef TEST_COUNT_TOTAL

static const struct {
    const char *name;
    int32_t (*fn)(void);
} r6_tests[] = {
    { "mul", test_mips64r6_mul },
    { "muh", test_mips64r6_muh },
    { "mulu", test_mips64r6_mulu },
    { "muhu", test_mips64r6_muhu },
    { "dmul", test_mips64r6_dmul },
    { "dmuh", test_mips64r6_dmuh },
    { "dmulu", test_mips64r6_dmulu },
    { "dmuhu", test_mips64r6_dmuhu },
};

int main(void)
{
    unsigned count = sizeof(r6_tests) / sizeof(r6_tests[0]);
    int fail = 0;
    unsigned i;

    printf("TAP version 14\n1..%u\n", count);
    for (i = 0; i < count; i++) {
        int r = r6_tests[i].fn();

        printf("%s %u - MIPS64R6 multiply %s\n", r ? "not ok" : "ok", i + 1,
               r6_tests[i].name);
        fail |= r;
    }

    return fail ? 1 : 0;
}
