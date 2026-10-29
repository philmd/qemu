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

/* JIALC rt, offset: call to GPR[rt] + offset, link in $ra. */
#define JIALC(bias, off, seen, expected, tmp)                           \
    __asm__ volatile (                                                  \
        ".set push\n\t"                                                 \
        ".set noreorder\n\t"                                            \
        "move %0, $zero\n\t"                                            \
        "lapc %2, 1f\n\t"                                               \
        "addiu %2, %2, " #bias "\n\t"                                   \
        "jialc %2, " #off "\n\t"                                        \
        "2: bc 3f\n\t"                                                  \
        "1: move %0, $ra\n\t"                                           \
        "jrc $ra\n\t"                                                   \
        "3: lapc %1, 2b\n\t"                                            \
        ".set pop\n\t"                                                  \
        : "=&r" (seen), "=&r" (expected), "=&r" (tmp)                   \
        :                                                               \
        : "ra")

static int test_jialc(void)
{
    uint32_t seen, expected, tmp;
    int ret = 0;

    JIALC(0, 0, seen, expected, tmp);
    ret |= seen != expected;
    JIALC(-8, 8, seen, expected, tmp);
    ret |= seen != expected;
    JIALC(8, -8, seen, expected, tmp);
    ret |= seen != expected;
    if (ret) {
        printf("# JIALC: last ra 0x%08x expected 0x%08x\n", seen, expected);
    }
    return ret;
}

/* JIC rt, offset: jump to GPR[rt] + offset, no link ($ra unchanged). */
#define JIC(bias, off, res, ra_before, ra_after, tmp)                   \
    __asm__ volatile (                                                  \
        ".set push\n\t"                                                 \
        ".set noreorder\n\t"                                            \
        "move %0, $zero\n\t"                                            \
        "move %1, $ra\n\t"                                              \
        "lapc %3, 1f\n\t"                                               \
        "addiu %3, %3, " #bias "\n\t"                                   \
        "jic %3, " #off "\n\t"                                          \
        "addiu %0, %0, 2\n\t"                                           \
        "1: addiu %0, %0, 1\n\t"                                        \
        "move %2, $ra\n\t"                                              \
        ".set pop\n\t"                                                  \
        : "=&r" (res), "=&r" (ra_before), "=&r" (ra_after), "=&r" (tmp))

static int test_jic(void)
{
    uint32_t res, ra_before, ra_after, tmp;
    int ret = 0;

    JIC(0, 0, res, ra_before, ra_after, tmp);
    ret |= res != 1 || ra_before != ra_after;
    JIC(-4, 4, res, ra_before, ra_after, tmp);
    ret |= res != 1 || ra_before != ra_after;
    JIC(4, -4, res, ra_before, ra_after, tmp);
    ret |= res != 1 || ra_before != ra_after;
    if (ret) {
        printf("# JIC: last res %u (expected 1) ra 0x%08x -> 0x%08x\n",
               res, ra_before, ra_after);
    }
    return ret;
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

    printf("TAP version 14\n1..3\n");
    ret |= run("BALC", test_balc);
    ret |= run("JIALC", test_jialc);
    ret |= run("JIC", test_jic);

    return ret ? 1 : 0;
}
