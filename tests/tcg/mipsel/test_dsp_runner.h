/*
 * Helpers to run several DSP ASE tests from the same process: assert()
 * only counts failures, and RUN_TEST() reports one status line per test,
 * after resetting DSPControl, in TAP format. A test calling RUN_TEST() must
 * end with DSP_TAP_PLAN().
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef TEST_DSP_RUNNER_H
#define TEST_DSP_RUNNER_H

#include <stdio.h>

static int dsp_failures;
static int dsp_tests;

#undef assert
#define assert(cond) \
    do { \
        if (!(cond)) { \
            printf("# FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            dsp_failures++; \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        int ret_; \
        dsp_failures = 0; \
        __asm__ volatile("wrdsp $zero, 0x3f\n\t"); \
        ret_ = fn(); \
        printf("%s %d - %s\n", (ret_ || dsp_failures) ? "not ok" : "ok", \
               ++dsp_tests, #fn); \
        ret |= ret_ || dsp_failures; \
    } while (0)

#define DSP_TAP_PLAN() printf("1..%d\n", dsp_tests)

#endif
