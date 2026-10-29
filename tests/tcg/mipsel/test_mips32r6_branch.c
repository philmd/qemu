/*
 * MIPS32R6 tests: compact branches and jumps instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>

static int count;

/*
 * BALC: branch and link, no delay slot. The callee records $ra and returns;
 * $ra must be the address of the instruction following BALC (label 2).
 */
static int test_balc(void)
{
    uint32_t seen, expected;

    __asm__ volatile (
        ".set push\n\t"
        ".set noreorder\n\t"
        "move %0, $zero\n\t"
        "balc 1f\n\t"
        "2: bc 3f\n\t"
        "1: move %0, $ra\n\t"
        "jrc $ra\n\t"
        "3: lapc %1, 2b\n\t"
        ".set pop\n\t"
        : "=&r" (seen), "=&r" (expected)
        :
        : "ra");

    if (seen != expected) {
        printf("# BALC: ra 0x%08x expected 0x%08x\n", seen, expected);
    }
    return seen != expected;
}

static int run(const char *name, int (*test)(void))
{
    int ret = test();

    printf("%s %d - MIPS32R6 branch %s\n", ret ? "not ok" : "ok", ++count,
           name);
    return ret;
}

int main(void)
{
    int ret = 0;

    printf("TAP version 14\n1..1\n");
    ret |= run("BALC", test_balc);

    return ret ? 1 : 0;
}
