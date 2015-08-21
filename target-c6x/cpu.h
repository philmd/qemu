/*
 * C6X virtual CPU header
 *
 *  Copyright (c) 2011 Luc Michel
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef CPU_C6X_H
#define CPU_C6X_H

#define TARGET_LONG_BITS 32

#define ELF_MACHINE	EM_TI_C6000

#define CPUArchState struct CPUC6XState

#include "config.h"
#include "qemu-common.h"
#include "exec/cpu-defs.h"

#include "fpu/softfloat.h"

#define TARGET_HAS_ICE  1

#define NB_MMU_MODES 2

#define C6X_FILE_REGS_NB       32

#define C6X_MAXDS    6

/* Control registers */
typedef enum C6XControlRegs {
    /* C64x */
    CR_AMR = 0, CR_CSR, CR_GFPGFR, CR_ICR,
    CR_IER, CR_IFR, CR_IRP, CR_ISR,
    CR_ISTP, CR_NRP,

    /* C64x+ */
    CR_DIER, CR_DNUM, CR_ECR, CR_EFR,
    CR_GPLYA, CR_GPLYB, CR_IERR, CR_ILC,
    CR_ITSR, CR_NTSR, CR_REP, CR_RILC,
    CR_SSR, CR_TSCH, CR_TSCL, CR_TSR,

    C6X_CR_NUM,

    /* pce1 is handled by delay slot buffers */
    CR_PCE1
} C6XControlRegs;


typedef struct C6XTBModif {
    uint32_t val;
    uint32_t cf;
    uint32_t valid;
    struct C6XTBModif* next;
} C6XTBModif;

typedef struct C6XTBContext {
    C6XTBModif mod[C6X_MAXDS][C6X_FILE_REGS_NB*2];
    C6XTBModif* first[C6X_MAXDS];

    C6XTBModif cr_pce1_mod[C6X_MAXDS];

    unsigned int cur_cycle;

    /* Pending branches count in cr_pce1_mod */
    unsigned int pending_b;
} C6XTBContext;

typedef struct CPUC6XState {
    uint32_t aregs[C6X_MAXDS][C6X_FILE_REGS_NB];
    uint32_t bregs[C6X_MAXDS][C6X_FILE_REGS_NB];

    /* Conditional flags 
     * The maximum cf we will possibly need is 8 (max number of insn in a exec
     * packet) times the maximum delay slot of the ISA */
    uint32_t cf[C6X_MAXDS * 8];

    /* Program counter */
    uint32_t cr_pce1[C6X_MAXDS];
    unsigned int cr_pce1_cur;

    /* Control registers */
    uint32_t cregs[C6X_CR_NUM];

    uint32_t cpuid;

    C6XTBContext tb_context;

    CPU_COMMON

} CPUC6XState;

#include "cpu-qom.h"

C6XCPU *c6x_cpu_init(const char *cpu_model);
void c6x_translate_init(void);
int c6x_cpu_exec(CPUState *cpu);
void switch_mode(CPUC6XState *, int);

void c6x_cpu_list(FILE *f, fprintf_function cpu_fprintf);

#define CSR_CPUID_C64X          (0x0c01 << 16)
#define CSR_CPUID_C64X_PLUS     (0x1001 << 16)
#define CSR_CPUID_C64X_PLUS_SIM (0x4001 << 16)

#define CSR_M_CPUID             0xff000000
#define CSR_M_REVID             0x00ff0000
#define CSR_M_PWRD              0x0000fc00
#define CSR_M_SAT               0x00000200
#define CSR_M_EN                0x00000100
#define CSR_M_PCC               0x000000e0
#define CSR_M_DCC               0x0000001c
#define CSR_M_PGIE              0x00000002
#define CSR_M_GIE               0x00000001

#define TARGET_PAGE_BITS 10

#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32

C6XCPU *cpu_c6x_init(const char *model);
#define cpu_init(model) CPU(cpu_c6x_init(cpu_model))

#define cpu_exec c6x_cpu_exec
#define cpu_gen_code c6x_cpu_gen_code
#define cpu_signal_handler c6x_cpu_signal_handler
#define cpu_list c6x_cpu_list


/* MMU modes definitions */
#define MMU_MODE0_SUFFIX _kernel
#define MMU_MODE1_SUFFIX _user
#define MMU_USER_IDX 1
static inline int cpu_mmu_index (CPUC6XState *env)
{
    return 0;
}

#if defined(CONFIG_USER_ONLY)
static inline void cpu_clone_regs(CPUC6XState *env, target_ulong newsp)
{
    fprintf(stderr, "cpu clone regs\n");
}
#endif

#include "exec/cpu-all.h"


static inline void cpu_get_tb_cpu_state(CPUC6XState *env, target_ulong *pc,
                                        target_ulong *cs_base, int *flags)
{
    *pc = env->cr_pce1[0];
    *cs_base = 0;
    *flags = 0;
}


#include "exec/exec-all.h"

static inline void cpu_pc_from_tb(CPUC6XState *env, TranslationBlock *tb)
{
    env->cr_pce1[env->cr_pce1_cur] = tb->pc;
}

#endif
