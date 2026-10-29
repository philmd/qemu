/*
 * MIPS32R6 tests: PC-relative AUIPC and ALUIPC instructions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>

/*
 * Run INSN with the immediate IMM, and probe the address of INSN with a
 * second AUIPC (which yields its own address, +4 being INSN's size).
 */
#define PCREL(insn, imm, res, addr)                                     \
    __asm__ volatile (                                                  \
        ".set push\n\t"                                                 \
        ".set noreorder\n\t"                                            \
        insn " %0, " #imm "\n\t"                                        \
        "auipc %1, 0\n\t"                                               \
        ".set pop\n\t"                                                  \
        : "=r" (res), "=r" (addr))

static int count;

static int check(const char *name, uint32_t imm, uint32_t res, uint32_t probe,
                 int aligned)
{
    /* The probe AUIPC is the instruction after the tested one. */
    uint32_t addr = probe - 4;
    uint32_t expected = addr + (imm << 16);
    int ok;

    if (aligned) {
        expected &= ~0xffffu;
    }
    ok = res == expected;
    if (!ok) {
        printf("# %s imm=0x%04x at 0x%08x: got 0x%08x expected 0x%08x\n",
               name, imm, addr, res, expected);
    }
    return !ok;
}

#define CHECK_AUIPC(imm)                                                \
    do {                                                                \
        uint32_t res, probe;                                            \
        PCREL("auipc", imm, res, probe);                                \
        ret |= check("AUIPC", imm, res, probe, 0);                      \
    } while (0)

#define CHECK_ALUIPC(imm)                                               \
    do {                                                                \
        uint32_t res, probe;                                            \
        PCREL("aluipc", imm, res, probe);                               \
        ret |= check("ALUIPC", imm, res, probe, 1);                     \
    } while (0)

static int test_auipc(void)
{
    int ret = 0;

    CHECK_AUIPC(0);
    CHECK_AUIPC(1);
    CHECK_AUIPC(0x7fff);
    CHECK_AUIPC(0x8000);
    CHECK_AUIPC(0xffff);
    return ret;
}

static int test_aluipc(void)
{
    int ret = 0;

    CHECK_ALUIPC(0);
    CHECK_ALUIPC(1);
    CHECK_ALUIPC(0x7fff);
    CHECK_ALUIPC(0x8000);
    CHECK_ALUIPC(0xffff);
    return ret;
}

int main(void)
{
    int ret = 0, r;

    printf("TAP version 14\n1..2\n");
    r = test_auipc();
    printf("%s %d - MIPS32R6 AUIPC\n", r ? "not ok" : "ok", ++count);
    ret |= r;
    r = test_aluipc();
    printf("%s %d - MIPS32R6 ALUIPC\n", r ? "not ok" : "ok", ++count);
    ret |= r;

    return ret ? 1 : 0;
}
