/*
 *  C6X translation
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
#include "cpu.h"
#include "disas/disas.h"
#include "exec/helper-proto.h"
#include "tcg-op.h"

#include "exec/cpu_ldst.h"

#include "exec/helper-gen.h"

// #define DELAY_SLOT_DEBUG
#define C6X_DEBUG_DISAS

#ifdef C6X_DEBUG_DISAS
# define C6X_DEBUG(fmt, ...)                            \
    qemu_log_mask(CPU_LOG_TB_OP,                        \
                  TARGET_FMT_lx ": %08x " fmt "\n",     \
                  dc->pc, dc->opcode , ## __VA_ARGS__)

#else
# define C6X_DEBUG(fmt, ...) do { } while(0)
#endif

#ifdef DELAY_SLOT_DEBUG
# define C6X_DS_DEBUG(fmt, ...)                         \
    qemu_log_mask(CPU_LOG_TB_OP,                        \
                  TARGET_FMT_lx ": %08x " fmt "\n",     \
                  dc->pc, dc->opcode , ## __VA_ARGS__)
#else
# define C6X_DS_DEBUG(fmt, ...) do { } while(0)
#endif

#define TRANSLATE_ABORT(fmt, ...) do {      \
    fprintf(stderr, fmt, ## __VA_ARGS__);   \
    abort();                                \
} while (0)

#define REG(f, r)   (regs[(f)].r_ptr[(r)])

#define DS_IDX(i)       ((i) % C6X_MAXDS)
#define DS_IDX_DELAY(i) ((ds_buffer_cur + (i) + 1) % C6X_MAXDS)

#ifndef DELAY_SLOT_DEBUG
#define ADD_DELAY_SLOTS_BUFFER(s, reg, tcg_v, d)    \
    do {                                            \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].ptr = \
            &(regs[s].r_ptr[reg]); \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].target = tcg_v; \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].cond_type = CT_NONE; \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].delay = d; \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].reg.f = s; \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].reg.r = reg; \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].next = \
            regs[s].ds_first[DS_IDX_DELAY(d)]; \
        regs[s].ds_first[DS_IDX_DELAY(d)] = \
            &(regs[s].ds_buffer[DS_IDX_DELAY(d)][reg]); \
    } while(0)

#else
#define ADD_DELAY_SLOTS_BUFFER(s, reg, tcg_v, d)    \
    do {                                            \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].ptr = \
            &(regs[s].r_ptr[reg]); \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].target = tcg_v; \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].cond_type = CT_NONE; \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].delay = d; \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].reg.f = s; \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].reg.r = reg; \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].next = \
            regs[s].ds_first[DS_IDX_DELAY(d)]; \
        regs[s].ds_first[DS_IDX_DELAY(d)] = \
            &(regs[s].ds_buffer[DS_IDX_DELAY(d)][reg]); \
        regs[s].ds_buffer[DS_IDX_DELAY(d)][reg].pc = dc->pc; \
    } while(0)
#endif

typedef enum CondType {
    CT_NONE,
    CT_COND,
    CT_INVCOND,
} CondType;

typedef enum C6XRegsFile {
    REGS_FILE_A = 0,
    REGS_FILE_B
} C6XRegsFile;

typedef struct C6XRegister {
    C6XRegsFile    f;
    uint32_t        r;
} C6XRegister;

static const C6XRegister reg_pce1 = { .f = 2, .r = 0 };

typedef struct DelaySlotBuffer {
    TCGv_i32*       ptr;
    TCGv_i32        target;

    unsigned int    delay;

    CondType        cond_type;
    TCGv_i32        cond_flag;

    uint32_t        branch_target, branch_inv_target;
    int             branch_type;

    /* Register being updated, kept here for simplicity */
    C6XRegister    reg;

    struct DelaySlotBuffer* next;

#ifdef DELAY_SLOT_DEBUG
    uint32_t         pc;
#endif
} DelaySlotBuffer;

typedef struct GPRPool {
    TCGv_i32        r_ptr[C6X_FILE_REGS_NB];

    TCGv_i32        pool[C6X_MAXDS][C6X_FILE_REGS_NB];
    unsigned int    pool_cur[C6X_FILE_REGS_NB];

    DelaySlotBuffer ds_buffer[C6X_MAXDS][C6X_FILE_REGS_NB];
    DelaySlotBuffer* ds_first[C6X_MAXDS];
} GPRPool;


/* For pce1 delay slot buffer, the ``next'' field is used as the validity field */
#define PCE1_DS_IS_VALID(ds) (((ds)->next) != NULL)
#define PCE1_DS_SET_AS_VALID(ds)    ((ds)->next = (ds))
#define PCE1_DS_SET_AS_INVALID(ds)    ((ds)->next = NULL)
typedef struct Pce1Pool {
    TCGv_i32        r_ptr;

    TCGv_i32        pool[C6X_MAXDS];
    unsigned int    pool_cur;

    DelaySlotBuffer ds_buffer[C6X_MAXDS];
} Pce1Pool;


static TCGv_ptr cpu_env;

/* Current position in the delay slot buffer */
static unsigned int ds_buffer_cur;

/* general purpose registers */
static GPRPool regs[2];

/* control registers */
static TCGv_i32 cregs[C6X_CR_NUM];

/* pce1 */
static Pce1Pool  cr_pce1;

/* Condition flags */
static TCGv_i32 cond_flags[C6X_MAXDS * 8];
static unsigned int cond_flags_cur;

typedef enum C6XOperandType {
    OPTYPE_SINT,    /* 32bits signed */
    OPTYPE_UINT,    /* 32bits unsigned */
    OPTYPE_XSINT,   /* 32bits signed, cross-ref */
    OPTYPE_XUINT,   /* 32bits unsigned, cross-ref */
    OPTYPE_SLONG,   /* 40bits signed */
    OPTYPE_SLLONG,  /* 64bits signed */
    OPTYPE_ULONG,   /* 40bits unsigned */
    OPTYPE_ULLONG,  /* 64bits unsigned */
    OPTYPE_DINT,    /* 64bits signed */
    OPTYPE_DUINT,   /* 64bits unsigned */

    OPTYPE_SCST5,   /* 5 bits immediate, signed */
    OPTYPE_SCST7,   /* 7 bits immediate, signed */
    OPTYPE_SCST12,  /* 12 bits immediate, signed */
    OPTYPE_SCST16,  /* 16 bits immediate, signed */
    OPTYPE_SCST21,  /* 21 bits immediate, signed */
    OPTYPE_UCST3,   /* 3 bits immediate, unsigned */
    OPTYPE_UCST5,   /* 5 bits immediate, unsigned */
    OPTYPE_UCST15,  /* 15 bits immediate, unsigned */

    OPTYPE_CRLO,    /* Control register, lower part */

    OPTYPE_NULL,    /* Operand not used */
} C6XOperandType;

typedef struct C6XOperand {
    uint32_t val;
    C6XOperandType t;
} C6XOperand;

typedef enum C6XLdStMode {
    ADDRMOD_NOFFSET_CST  = 0x0,
    ADDRMOD_POFFSET_CST  = 0x1,
    ADDRMOD_NOFFSET_REG  = 0x4,
    ADDRMOD_POFFSET_REG  = 0x5,
    ADDRMOD_PREDECR_CST  = 0x8,
    ADDRMOD_PREINCR_CST  = 0x9,
    ADDRMOD_POSTDECR_CST = 0xa,
    ADDRMOD_POSTINCR_CST = 0xb,
    ADDRMOD_PREDECR_REG  = 0xc,
    ADDRMOD_PREINCR_REG  = 0xd,
    ADDRMOD_POSTDECR_REG = 0xe,
    ADDRMOD_POSTINCR_REG = 0xf
} C6XLdStMode;

/* internal defines */
typedef struct DisasContext {
    int mmu_idx;

    target_ulong pc, pc_fp, pc_ep;
    target_ulong pc_next_ep;
    int is_jmp;
    int brkpt;

    uint32_t opcode;

    /* Opcode decoding */
    C6XOperand src1, src2, dst;
    uint32_t creg, z, s, x, sc, y, csta, cstb;
    C6XLdStMode ldst_mode;

    /* The label that will be jumped to when the instruction is skipped.  */
    TCGLabel* condlabel;

    /* DS buffers for the insn beeing generated */
    unsigned int num_res;
    CondType ct;
    TCGv_i32 cf;
    DelaySlotBuffer* cur_ds[3];

    struct TranslationBlock *tb;
    int singlestep_enabled;

    /* Set by decode_insn */
    unsigned int elapsed_cycles;

    /* Cycles elapsed since the begining
     * of the tb */
    unsigned int tb_cycles;
} DisasContext;



#include "exec/gen-icount.h"

static const char *aregsname[] = {
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    "a8", "a9", "a10", "a11", "a12", "a13", "a14", "a15",
    "a16", "a17", "a18", "a19", "a20", "a21", "a22", "a23",
    "a24", "a25", "a26", "a27", "a28", "a29", "a30", "a31"
};

static const char *bregsname[] = {
    "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7",
    "b8", "b9", "b10", "b11", "b12", "b13", "b14", "b15",
    "b16", "b17", "b18", "b19", "b20", "b21", "b22", "b23",
    "b24", "b25", "b26", "b27", "b28", "b29", "b30", "b31"
};

static const char *cregnames[] = {
    "amr", "csr", "gfpgfr", "icr", "ier", "ifr", "irp",
    "isr", "istp", "nrp", "dier", "dnum", "ecr",
    "efr", "gplya", "gplyb", "ierr", "ilc", "itsr", "ntsr",
    "rep", "rilc", "ssr", "tsch", "tscl", "tsr"
};

static inline void regs_pool_init(GPRPool *r, size_t offset,
                           const char** reg_names)
{
    int i, j;
#ifdef C6X_DEBUG_DISAS
    char buf[128];
    char *d;
#endif

    for(j = 0; j < C6X_MAXDS; j++) {
        for(i = 0; i < C6X_FILE_REGS_NB; i++) {
#ifdef C6X_DEBUG_DISAS
            sprintf(buf, "%s_%d", reg_names[i], j);
            d = g_malloc(sizeof(char) * (strlen(buf) + 1));
            strcpy(d, buf);
            r->pool[j][i] = tcg_global_mem_new_i32(TCG_AREG0,
                                  offset + (((j*C6X_FILE_REGS_NB) + i) * sizeof(uint32_t)),
                                  d);
#else
            r->pool[j][i] = tcg_global_mem_new_i32(TCG_AREG0,
                                  offset + (((j*C6X_FILE_REGS_NB) + i) * sizeof(uint32_t)),
                                  reg_names[i]);
#endif
        }
        r->ds_first[j] = NULL;
    }

    for(i = 0; i < C6X_FILE_REGS_NB; i++) {
        r->r_ptr[i] = r->pool[0][i];
        r->pool_cur[i] = 0;
    }
}

static inline void cr_pce1_init(void)
{
    int i;
#ifdef C6X_DEBUG_DISAS
    char buf[128];
    char *d;
#endif

    for(i = 0; i < C6X_MAXDS; i++) {
#ifdef C6X_DEBUG_DISAS
        sprintf(buf, "%s_%d", "pce1", i);
        d = g_malloc(sizeof(char) * (strlen(buf) + 1));
        strcpy(d, buf);
        cr_pce1.pool[i] = tcg_global_mem_new_i32(TCG_AREG0,
                                                 offsetof(CPUC6XState, cr_pce1[i]),
                                                 d);
#else
        cr_pce1.pool[i] = tcg_global_mem_new_i32(TCG_AREG0,
                                                 offsetof(CPUC6XState, cr_pce1[i]),
                                                 "pce1");
#endif
        cr_pce1.ds_buffer[i].next = NULL;
    }

    cr_pce1.r_ptr = cr_pce1.pool[0];
    cr_pce1.pool_cur = 0;
}

static inline void cf_init(void)
{
    int i;
#ifdef C6X_DEBUG_DISAS
    char buf[128];
    char *d;
#endif

    for(i = 0; i < C6X_MAXDS * 8; i++) {
#ifdef C6X_DEBUG_DISAS
        sprintf(buf, "%s_%d", "cf", i);
        d = g_malloc(sizeof(char) * (strlen(buf) + 1));
        strcpy(d, buf);
        cond_flags[i] = tcg_global_mem_new_i32(TCG_AREG0,
                                               offsetof(CPUC6XState, cf[i]),
                                               d);
#else
        cond_flags[i] = tcg_global_mem_new_i32(TCG_AREG0,
                                               offsetof(CPUC6XState, cf[i]),
                                               "cf");
#endif
    }

    cond_flags_cur = 0;
}


void c6x_translate_init(void)
{
    int i;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");

    regs_pool_init(&(regs[REGS_FILE_A]), offsetof(CPUC6XState, aregs), aregsname);
    regs_pool_init(&(regs[REGS_FILE_B]), offsetof(CPUC6XState, bregs), bregsname);

    ds_buffer_cur = 0;

    for(i = 0; i < C6X_CR_NUM; i++) {
        cregs[i] = tcg_global_mem_new_i32(TCG_AREG0,
                                  offsetof(CPUC6XState, cregs[i]),
                                  cregnames[i]);
    }

    cr_pce1_init();
    cf_init();
}


/*
 * Return the PC of the first instruction of the next execute packet
 * relatively to dc->pc
 */
static inline target_ulong get_pc_next_exec_packet(DisasContext *dc, CPUC6XState *env)
{
    target_ulong npc = dc->pc + 4;
    uint32_t next_opcode = cpu_ldl_code(env, dc->pc);

    while(next_opcode & 0x1) {
        next_opcode = cpu_ldl_code(env, npc);
        npc += 4;
    }

    return npc;
}


static inline TCGv_i32 get_next_cond_flag(void)
{
    TCGv_i32 cf = cond_flags[cond_flags_cur];
    cond_flags_cur = (cond_flags_cur + 1) % (C6X_MAXDS*8);

    return cf;
}

static inline void gen_set_pc_im_no_ds_canon(DisasContext *dc, uint32_t val)
{
    tcg_gen_movi_i32(cr_pce1.pool[0], val);
}

static inline void gen_set_pc_im_no_ds(DisasContext *dc, uint32_t val)
{
    tcg_gen_movi_i32(cr_pce1.r_ptr, val);
}

static inline void gen_set_pc_r(DisasContext *dc, TCGv_i32 src,
                                unsigned int delay)
{
    TCGv_i32 n_pc;
    unsigned int n_idx;
    DelaySlotBuffer* cur_ds = &(cr_pce1.ds_buffer[DS_IDX_DELAY(delay)]);

    /* Look for another branch pending at the same cycle. If there is one, it
     * should be conditional, or the behavior is undefined
     */
    if(PCE1_DS_IS_VALID(cur_ds)) {
        if(cur_ds->cond_type != CT_COND) {
            TRANSLATE_ABORT("Two branches at the same cycle");
        }

        dc->ct = cur_ds->cond_type = CT_INVCOND;
        n_pc = cur_ds->target;
    } else {
        cr_pce1.pool_cur = (cr_pce1.pool_cur + 1) % C6X_MAXDS;
        n_idx = cr_pce1.pool_cur;

        n_pc = cr_pce1.pool[n_idx];

        cur_ds->target = n_pc;
        cur_ds->cond_type = dc->ct;
        cur_ds->delay = 5;
        cur_ds->reg = reg_pce1;
        cur_ds->branch_type = DISAS_JUMP;
        PCE1_DS_SET_AS_VALID(cur_ds);

    }

    dc->cur_ds[dc->num_res] = cur_ds;
    dc->num_res++;
    tcg_gen_mov_i32(n_pc, src);
}

static inline void gen_set_pc_im(DisasContext *dc, uint32_t val,
                                 unsigned int delay)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    DelaySlotBuffer *e;

    tcg_gen_movi_i32(t0, val);
    gen_set_pc_r(dc, t0, delay);
    tcg_temp_free_i32(t0);

    e = dc->cur_ds[dc->num_res-1];
    e->branch_type = DISAS_TB_JUMP;

    if (e->cond_type == CT_INVCOND) { 
        e->branch_inv_target = val;
    } else {
        e->branch_target = val;
    }
}

static inline void gen_load_reg(TCGv_i32 t, uint32_t reg, uint32_t s)
{
    tcg_gen_mov_i32(t, REG(s, reg));
}

enum ld_64bits_type {
    LD64_LOW,   /* Load only the lower 32 bits */
    LD64_LOW_S, /* Load only the lower 32 bits as signed int */
    LD64_FULL   /* Load the 2*32bits registers pair */
};
static inline void gen_load_64bits_reg(TCGv_i64 t, uint32_t reg, uint32_t s,
                                       enum ld_64bits_type ty)
{
    if(ty == LD64_LOW_S) {
        tcg_gen_ext_i32_i64(t, REG(s, reg));
    } else {
        tcg_gen_extu_i32_i64(t, REG(s, reg));
        if(ty == LD64_FULL) {
            TCGv_i64 t0 = tcg_temp_new_i64();
            tcg_gen_extu_i32_i64(t0, REG(s, reg+1));
            tcg_gen_shli_i64(t0, t0, 32);
            tcg_gen_or_i64(t, t, t0);
            tcg_temp_free_i64(t0);
        }
    }
}

static inline TCGv_i32 reg_pool_next(uint32_t s, uint32_t reg)
{
    unsigned int idx;
    regs[s].pool_cur[reg] = (regs[s].pool_cur[reg] + 1) %
        C6X_MAXDS;

    idx = regs[s].pool_cur[reg];
    return regs[s].pool[idx][reg];
}

static inline DelaySlotBuffer* gen_delayed_store_reg(DisasContext *dc, uint32_t reg,
                                             uint32_t s, unsigned int delay)
{
    DelaySlotBuffer* ds = regs[s].ds_first[DS_IDX_DELAY(delay)];

    /* Look for already allocated TCGv for this register at this
     * cycle. It can mainly happen in case of parallel conditionnal
     * instructions with invert conditions and same destination */
    while(ds != NULL) {
        if(&(regs[s].r_ptr[reg]) == ds->ptr) {
            if(ds->cond_type != CT_COND) {
                TRANSLATE_ABORT("Two instructions write the same reg at the same cycle");
            }

            dc->ct = ds->cond_type = CT_INVCOND;
            break;
        }
        ds = ds->next;
    }

    if(ds == NULL) {
        TCGv_i32 r = reg_pool_next(s, reg);
        ADD_DELAY_SLOTS_BUFFER(s, reg, r, delay);
        ds = &(regs[s].ds_buffer[DS_IDX_DELAY(delay)][reg]);
        ds->cond_type = dc->ct;
    }

    return ds;
}

static inline void gen_store_reg(DisasContext *dc, TCGv_i32 t, uint32_t reg,
                                 uint32_t s, unsigned int delay)
{
    DelaySlotBuffer *ds;

    ds = gen_delayed_store_reg(dc, reg, s, delay);
    tcg_gen_mov_i32(ds->target, t);

    dc->cur_ds[dc->num_res] = ds;
    dc->num_res++;
}


static inline void gen_store_64bits_reg(DisasContext *dc, TCGv_i64 t,
                                        uint32_t reg, uint32_t s,
                                        unsigned int delay)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i64 t1;

    tcg_gen_trunc_i64_i32(t0, t);
    gen_store_reg(dc, t0, reg, s, delay);
    t1 = tcg_temp_new_i64();
    tcg_gen_shri_i64(t1, t, 32);
    tcg_gen_trunc_i64_i32(t0, t1);
    tcg_temp_free_i64(t1);
    gen_store_reg(dc, t0, reg+1, s, delay);
    tcg_temp_free_i32(t0);
}

static inline void gen_exception(int e)
{
    TCGv_i32 te = tcg_temp_new_i32();
    tcg_gen_movi_i32(te, e);
    gen_helper_exception(cpu_env, te);
    tcg_temp_free_i32(te);
}

static const TCGv_i32 * const cond_op_predicate_reg_map[] = {
    NULL,
    &(REG(REGS_FILE_B, 0)), &(REG(REGS_FILE_B, 1)),
    &(REG(REGS_FILE_B, 2)), &(REG(REGS_FILE_A, 1)),
    &(REG(REGS_FILE_A, 2)), &(REG(REGS_FILE_A, 0)),
    NULL
};

static const TCGv_i32 * const cregs_read_map[] = {
    [0x00] = &(cregs[CR_AMR]),
    [0x01] = &(cregs[CR_CSR]),
    [0x02] = &(cregs[CR_IFR]),
    [0x04] = &(cregs[CR_IER]),
    [0x05] = &(cregs[CR_ISTP]),
    [0x06] = &(cregs[CR_IRP]),
    [0x07] = &(cregs[CR_NRP]),
    [0x0a] = &(cregs[CR_TSCL]),
    [0x0b] = &(cregs[CR_TSCH]),
    [0x0d] = &(cregs[CR_ILC]),
    [0x0e] = &(cregs[CR_RILC]),
    [0x0f] = &(cregs[CR_REP]),
    [0x10] = NULL, /* pce1 read, has to be handled separately */
    [0x11] = &(cregs[CR_DNUM]),
    [0x15] = &(cregs[CR_SSR]),
    [0x16] = &(cregs[CR_GPLYA]),
    [0x17] = &(cregs[CR_GPLYB]),
    [0x18] = &(cregs[CR_GFPGFR]),
    [0x19] = &(cregs[CR_DIER]),
    [0x1a] = &(cregs[CR_TSR]),
    [0x1b] = &(cregs[CR_ITSR]),
    [0x1c] = &(cregs[CR_NTSR]),
    [0x1d] = &(cregs[CR_EFR]),
    [0x1f] = &(cregs[CR_IERR]),
};

static const TCGv_i32 * const cregs_write_map[] = {
    [0x00] = &(cregs[CR_AMR]),
    [0x01] = &(cregs[CR_CSR]),
    [0x02] = &(cregs[CR_ISR]),
    [0x03] = &(cregs[CR_ICR]),
    [0x04] = &(cregs[CR_IER]),
    [0x05] = &(cregs[CR_ISTP]),
    [0x06] = &(cregs[CR_IRP]),
    [0x07] = &(cregs[CR_NRP]),
    [0x0d] = &(cregs[CR_ILC]),
    [0x0e] = &(cregs[CR_RILC]),
    [0x0f] = &(cregs[CR_REP]),
    [0x15] = &(cregs[CR_SSR]),
    [0x16] = &(cregs[CR_GPLYA]),
    [0x17] = &(cregs[CR_GPLYB]),
    [0x18] = &(cregs[CR_GFPGFR]),
    [0x19] = &(cregs[CR_DIER]),
    [0x1a] = &(cregs[CR_TSR]),
    [0x1b] = &(cregs[CR_ITSR]),
    [0x1c] = &(cregs[CR_NTSR]),
    [0x1d] = &(cregs[CR_ECR]),
    [0x1f] = &(cregs[CR_IERR]),
};

static inline void pool_reset(DisasContext *dc) {
    int i, j;

    for(j = 0; j < 2; j++) {
        for(i = 0; i < C6X_FILE_REGS_NB; i++) {
            regs[j].pool_cur[i] = 0;
            regs[j].r_ptr[i] = regs[j].pool[0][i];
        }
    }

    cr_pce1.pool_cur = 0;
    cr_pce1.r_ptr = cr_pce1.pool[0];
    cond_flags_cur = 0;
}

static inline int32_t sign_ext_32(int32_t i, unsigned int l)
{
    i <<= 32 - l;
    i >>= 32 - l;
    return i;
}

static inline int64_t sign_ext_64(int64_t i, unsigned int l)
{
    i <<= 64 - l;
    i >>= 64 - l;
    return i;
}

static TCGv_i32 read_operand_32(DisasContext *dc, C6XOperand op)
{
    TCGv_i32 t0 = tcg_temp_new_i32();

    switch(op.t) {
    case OPTYPE_SINT:
    case OPTYPE_UINT:
        gen_load_reg(t0, op.val, dc->s);
        break;
    case OPTYPE_XSINT:
    case OPTYPE_XUINT:
        gen_load_reg(t0, op.val, (dc->s + dc->x) % 2);
        break;
    case OPTYPE_SLONG:
    case OPTYPE_SLLONG:
    case OPTYPE_ULONG:
    case OPTYPE_ULLONG:
    case OPTYPE_DINT:
    case OPTYPE_DUINT:
        TRANSLATE_ABORT("trying to load 64bits registers pair in a 32bits register");
        break;

    case OPTYPE_SCST5:
        op.val = sign_ext_32(op.val, 5);
        tcg_gen_movi_i32(t0, op.val);
        break;
    case OPTYPE_SCST7:
        op.val = sign_ext_32(op.val, 7);
        tcg_gen_movi_i32(t0, op.val);
        break;
    case OPTYPE_SCST12:
        op.val = sign_ext_32(op.val, 12);
        tcg_gen_movi_i32(t0, op.val);
        break;
    case OPTYPE_SCST16:
        op.val = sign_ext_32(op.val, 16);
        tcg_gen_movi_i32(t0, op.val);
        break;
    case OPTYPE_SCST21:
        op.val = sign_ext_32(op.val, 21);
        tcg_gen_movi_i32(t0, op.val);
        break;

    case OPTYPE_UCST3:
    case OPTYPE_UCST5:
    case OPTYPE_UCST15:
        tcg_gen_movi_i32(t0, op.val);
        break;

    case OPTYPE_CRLO:
        if(cregs_read_map[op.val] == NULL) {
            /* pce1 read */
            tcg_gen_movi_i32(t0, dc->pc);
        } else {
            tcg_gen_mov_i32(t0, *cregs_read_map[op.val]);
        }
        break;

    case OPTYPE_NULL:
        TRANSLATE_ABORT("trying to read null operand");
    }

    return t0;
}

static void write_operand_32(DisasContext *dc, TCGv_i32 t,
                                        C6XOperand op, unsigned int delay)
{
    switch(op.t) {
    case OPTYPE_SINT:
    case OPTYPE_UINT:
        gen_store_reg(dc, t, op.val, dc->s, delay);
        break;
    case OPTYPE_SLONG:
    case OPTYPE_SLLONG:
    case OPTYPE_ULONG:
    case OPTYPE_ULLONG:
    case OPTYPE_DINT:
    case OPTYPE_DUINT:
        TRANSLATE_ABORT("trying to store 64bits registers pair un a 32bits register");
        break;
    case OPTYPE_XSINT:
    case OPTYPE_XUINT:
    case OPTYPE_SCST5:
    case OPTYPE_SCST7:
    case OPTYPE_SCST12:
    case OPTYPE_SCST16:
    case OPTYPE_SCST21:
    case OPTYPE_UCST3:
    case OPTYPE_UCST5:
    case OPTYPE_UCST15:
        TRANSLATE_ABORT("bad type for destination register");
        break;

    case OPTYPE_CRLO:
        tcg_gen_mov_i32(*cregs_write_map[op.val], t);
        break;

    case OPTYPE_NULL:
        TRANSLATE_ABORT("trying to write null operand");
    }
}

static TCGv_i64 read_operand_64(DisasContext *dc, C6XOperand op)
{
    TCGv_i64 t0 = tcg_temp_new_i64();

    switch(op.t) {
    case OPTYPE_SINT:
        gen_load_64bits_reg(t0, op.val, dc->s, LD64_LOW_S);
        break;
    case OPTYPE_UINT:
        gen_load_64bits_reg(t0, op.val, dc->s, LD64_LOW);
        break;
    case OPTYPE_XSINT:
        gen_load_64bits_reg(t0, op.val, (dc->s + dc->x) % 2, LD64_LOW_S);
        break;
    case OPTYPE_XUINT:
        gen_load_64bits_reg(t0, op.val, (dc->s + dc->x) % 2, LD64_LOW);
        break;
    case OPTYPE_SLONG:
    case OPTYPE_ULONG:
        gen_load_64bits_reg(t0, op.val, dc->s, LD64_FULL);
        tcg_gen_andi_i64(t0, t0, 0xffffffffffull);
        tcg_gen_shli_i64(t0, t0, 24);
        tcg_gen_shri_i64(t0, t0, 24);
        break;
    case OPTYPE_DINT:
    case OPTYPE_DUINT:
    case OPTYPE_SLLONG:
    case OPTYPE_ULLONG:
        gen_load_64bits_reg(t0, op.val, dc->s, LD64_FULL);
        break;
    case OPTYPE_SCST5:
        op.val = sign_ext_64(op.val, 5);
        tcg_gen_movi_i64(t0, op.val);
        break;
    case OPTYPE_SCST7:
        op.val = sign_ext_64(op.val, 7);
        tcg_gen_movi_i64(t0, op.val);
        break;
    case OPTYPE_SCST12:
        op.val = sign_ext_64(op.val, 12);
        tcg_gen_movi_i64(t0, op.val);
        break;
    case OPTYPE_SCST16:
        op.val = sign_ext_64(op.val, 16);
        tcg_gen_movi_i64(t0, op.val);
        break;
    case OPTYPE_SCST21:
        op.val = sign_ext_64(op.val, 21);
        tcg_gen_movi_i64(t0, op.val);
        break;

    case OPTYPE_UCST3:
    case OPTYPE_UCST5:
    case OPTYPE_UCST15:
        tcg_gen_movi_i64(t0, op.val);
        break;

    case OPTYPE_CRLO:
        TRANSLATE_ABORT("trying to read cr as 64bits reg");
        break;

    case OPTYPE_NULL:
        TRANSLATE_ABORT("trying to read null operand");
    }

    return t0;
}

static void write_operand_64(DisasContext *dc, TCGv_i64 t,
                                        C6XOperand op, unsigned int delay)
{
    switch(op.t) {
    case OPTYPE_SLONG:
    case OPTYPE_ULONG:
        /* XXX decide what to do for this type */
    case OPTYPE_SLLONG:
    case OPTYPE_ULLONG:
    case OPTYPE_DINT:
    case OPTYPE_DUINT:
        gen_store_64bits_reg(dc, t, op.val, dc->s, delay);
        break;
    case OPTYPE_SINT:
    case OPTYPE_UINT:
    case OPTYPE_XSINT:
    case OPTYPE_XUINT:
    case OPTYPE_SCST5:
    case OPTYPE_SCST7:
    case OPTYPE_SCST12:
    case OPTYPE_SCST16:
    case OPTYPE_SCST21:
    case OPTYPE_UCST3:
    case OPTYPE_UCST5:
    case OPTYPE_UCST15:
        TRANSLATE_ABORT("bad type for 64bits destination register");
        break;

    case OPTYPE_CRLO:
        TRANSLATE_ABORT("trying to write cr as 64bits reg");
        break;

    case OPTYPE_NULL:
        TRANSLATE_ABORT("trying to write null operand");
    }
}


static inline void gen_cond_reg_forward(DisasContext *dc, uint32_t reg)
{
    unsigned int next_idx;
    if(dc->creg) {
        next_idx = (regs[dc->s].pool_cur[reg] + 1) % C6X_MAXDS;
        tcg_gen_mov_i32(regs[dc->s].pool[next_idx][reg], regs[dc->s].r_ptr[reg]);
    }
}

static inline void gen_cond_op_begin(DisasContext *dc)
{
    TCGv_i32 t0;

    if(dc->creg) {

        dc->ct = CT_COND;

        dc->condlabel = gen_new_label();
        t0 = tcg_temp_new_i32();

        tcg_gen_mov_i32(t0, *(cond_op_predicate_reg_map[dc->creg]));

        if(dc->z)
            tcg_gen_brcondi_i32(TCG_COND_NE, t0, 0, dc->condlabel);
        else
            tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, dc->condlabel);

        tcg_temp_free_i32(t0);
    }
}

/*
 * BDEC and BPOS can have two conditions
 * (predicate and destination register)
 */
static inline void gen_cond_bdec_bpos_begin(DisasContext *dc)
{
    TCGv_i32 t0, t1;

    dc->ct = CT_COND;

    /* Condition generation
     * Theses branches are executed only if dst>=0
     * They can have another condition based on predicate register */
    t0 = read_operand_32(dc, dc->dst);

    tcg_gen_setcondi_i32(TCG_COND_GE, t0, t0, 0);

    if(dc->creg) {
        t1 = tcg_temp_new_i32();
        tcg_gen_mov_i32(t1, *(cond_op_predicate_reg_map[dc->creg]));

        if(dc->z) {
            tcg_gen_setcondi_i32(TCG_COND_EQ, t1, t1, 0);
        } else {
            tcg_gen_setcondi_i32(TCG_COND_NE, t1, t1, 0);
        }

        tcg_gen_and_i32(t0, t0, t1);
        tcg_temp_free_i32(t1);
    }

    dc->condlabel = gen_new_label();

    tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, dc->condlabel);

    tcg_temp_free_i32(t0);

}



#define REG2HELPERREG(reg)  ((reg).r + (((reg).f) * C6X_FILE_REGS_NB))
#define IS_PCE1(reg)    (((reg).r == reg_pce1.r) && ((reg).f == reg_pce1.f))
static inline void gen_insn_save_prologue(DisasContext* dc, DelaySlotBuffer *ds)
{
    TCGv_i32 d, reg;
    if(dc->tb_cycles <= C6X_MAXDS) {
        if(IS_PCE1(ds->reg)) {
            gen_helper_tb_save_pc_prologue(cpu_env, ds->target);
        } else {
            d = tcg_temp_new_i32();
            tcg_gen_movi_i32(d, ds->delay);
            reg = tcg_temp_new_i32();
            tcg_gen_movi_i32(reg, REG2HELPERREG(ds->reg));
            gen_helper_tb_save_prologue(cpu_env, reg, ds->target, d);
            tcg_temp_free_i32(reg);
            tcg_temp_free_i32(d);
        }
    }
}

static inline void gen_cond_op_end(DisasContext *dc)
{
    unsigned int i, valid_cf = 0;
    TCGLabel *l_end;
    const unsigned int num_res = dc->num_res;

    switch(dc->ct) {
    case CT_NONE:
        for(i = 0; i < num_res; i++) {
            if(dc->cur_ds[i]->delay) {
                gen_insn_save_prologue(dc, dc->cur_ds[i]);
            }
        }
        break;

    case CT_COND:
        if(num_res) {
            l_end = gen_new_label();

            for(i = 0; i < num_res; i++) {
                if(!valid_cf) {
                    dc->cf = get_next_cond_flag();
                    valid_cf = 1;
                }

                dc->cur_ds[i]->cond_flag = dc->cf;
                gen_insn_save_prologue(dc, dc->cur_ds[i]);
            }

            if(valid_cf) {
                tcg_gen_movi_i32(dc->cf, 1);
            }

            tcg_gen_br(l_end);
        }

        gen_set_label(dc->condlabel);

        if(num_res) {
            for(i = 0; i < num_res; i++) {
                if(!(dc->cur_ds[i]->delay)) {
                    tcg_gen_mov_i32(dc->cur_ds[i]->target,
                                    REG(dc->cur_ds[i]->reg.f,
                                        dc->cur_ds[i]->reg.r));

                }
            }

            if(valid_cf) {
                tcg_gen_movi_i32(dc->cf, 0);
            }

            gen_set_label(l_end);
        }
        break;

    case CT_INVCOND:
        for(i = 0; i < num_res; i++) {
            if(dc->cur_ds[i]->delay) {
                gen_insn_save_prologue(dc, dc->cur_ds[i]);
            }
        }
        gen_set_label(dc->condlabel);
        break;

    }
}


static inline TCGv_i32 gen_ld_st_addr(DisasContext *dc, uint32_t offset_shift)
{
    TCGv_i32 addr;
    TCGv_i32 t0 = tcg_temp_new_i32();

    switch(dc->ldst_mode) {
    case ADDRMOD_NOFFSET_CST:
    case ADDRMOD_POFFSET_CST:
    case ADDRMOD_PREDECR_CST:
    case ADDRMOD_PREINCR_CST:
    case ADDRMOD_POSTDECR_CST:
    case ADDRMOD_POSTINCR_CST:
        tcg_gen_movi_i32(t0, dc->src1.val << offset_shift);
        break;

    case ADDRMOD_NOFFSET_REG:
    case ADDRMOD_POFFSET_REG:
    case ADDRMOD_PREDECR_REG:
    case ADDRMOD_PREINCR_REG:
    case ADDRMOD_POSTDECR_REG:
    case ADDRMOD_POSTINCR_REG:
        gen_load_reg(t0, dc->src1.val, dc->y);
        tcg_gen_shli_i32(t0, t0, offset_shift);
        break;
    }

    addr = tcg_temp_new_i32();
    gen_load_reg(addr, dc->src2.val, dc->y);

    switch(dc->ldst_mode) {
    case ADDRMOD_NOFFSET_CST:
    case ADDRMOD_NOFFSET_REG:
        tcg_gen_sub_i32(addr, addr, t0);
        break;

    case ADDRMOD_POFFSET_CST:
    case ADDRMOD_POFFSET_REG:
        tcg_gen_add_i32(addr, addr, t0);
        break;

    case ADDRMOD_PREDECR_CST:
    case ADDRMOD_PREDECR_REG:
        tcg_gen_sub_i32(addr, addr, t0);
        gen_store_reg(dc, addr, dc->src2.val, dc->y, 0);
        break;

    case ADDRMOD_POSTDECR_CST:
    case ADDRMOD_POSTDECR_REG:
        tcg_gen_sub_i32(t0, addr, t0);
        gen_store_reg(dc, t0, dc->src2.val, dc->y, 0);
        break;

    case ADDRMOD_PREINCR_CST:
    case ADDRMOD_PREINCR_REG:
        tcg_gen_add_i32(addr, addr, t0);
        gen_store_reg(dc, addr, dc->src2.val, dc->y, 0);
        break;

    case ADDRMOD_POSTINCR_CST:
    case ADDRMOD_POSTINCR_REG:
        tcg_gen_add_i32(t0, addr, t0);
        gen_store_reg(dc, t0, dc->src2.val, dc->y, 0);
        break;
    }

    tcg_temp_free_i32(t0);

    /* XXX: handle circular addressing */
    return addr;
}


/* XXX fix cond ld/st */
/* Load/Store instructions */
#define GEN_LDST_BEGIN do {                     \
    gen_cond_op_begin(dc);                      \
} while(0)

#define GEN_LDST_END    do {                    \
    gen_cond_op_end(dc);                        \
} while(0)

static inline void gen_ldbu(DisasContext *dc) {
    TCGv_i32 addr, t0;
    GEN_LDST_BEGIN;

    addr = gen_ld_st_addr(dc, 0);
    t0 = tcg_temp_new_i32();
    tcg_gen_qemu_ld8u(t0, addr, dc->mmu_idx);
    tcg_temp_free_i32(addr);
    write_operand_32(dc, t0, dc->dst, 4);
    tcg_temp_free_i32(t0);

    GEN_LDST_END;
}

static inline void gen_ldb(DisasContext *dc) {
    TCGv_i32 addr, t0;
    GEN_LDST_BEGIN;

    addr = gen_ld_st_addr(dc, 0);
    t0 = tcg_temp_new_i32();
    tcg_gen_qemu_ld8s(t0, addr, dc->mmu_idx);
    tcg_temp_free_i32(addr);
    write_operand_32(dc, t0, dc->dst, 4);
    tcg_temp_free_i32(t0);

    GEN_LDST_END;
}

static inline void gen_ldhu(DisasContext *dc) {
    TCGv_i32 addr, t0;
    GEN_LDST_BEGIN;

    addr = gen_ld_st_addr(dc, 1);
    t0 = tcg_temp_new_i32();
    tcg_gen_qemu_ld16u(t0, addr, dc->mmu_idx);
    tcg_temp_free_i32(addr);
    write_operand_32(dc, t0, dc->dst, 4);
    tcg_temp_free_i32(t0);

    GEN_LDST_END;
}

static inline void gen_ldh(DisasContext *dc) {
    TCGv_i32 addr, t0;
    GEN_LDST_BEGIN;

    addr = gen_ld_st_addr(dc, 1);
    t0 = tcg_temp_new_i32();
    tcg_gen_qemu_ld16s(t0, addr, dc->mmu_idx);
    tcg_temp_free_i32(addr);
    write_operand_32(dc, t0, dc->dst, 4);
    tcg_temp_free_i32(t0);

    GEN_LDST_END;
}

static inline void gen_ldw(DisasContext *dc) {
    TCGv_i32 addr, t0;
    GEN_LDST_BEGIN;

    addr = gen_ld_st_addr(dc, 2);
    t0 = tcg_temp_new_i32();
    tcg_gen_qemu_ld32u(t0, addr, dc->mmu_idx);
    tcg_temp_free_i32(addr);
    write_operand_32(dc, t0, dc->dst, 4);
    tcg_temp_free_i32(t0);

    GEN_LDST_END;
}

static inline void gen_lddw(DisasContext *dc) {
    TCGv_i32 addr;
    TCGv_i64 t0;
    GEN_LDST_BEGIN;

    addr = gen_ld_st_addr(dc, 3);
    t0 = tcg_temp_new_i64();
    tcg_gen_qemu_ld64(t0, addr, dc->mmu_idx);
    tcg_temp_free_i32(addr);
    write_operand_64(dc, t0, dc->dst, 4);
    tcg_temp_free_i64(t0);

    GEN_LDST_END;
}

static inline void gen_ldndw(DisasContext *dc) {
    TCGv_i32 addr;
    TCGv_i64 t0;
    GEN_LDST_BEGIN;

    addr = gen_ld_st_addr(dc, (dc->sc) ? 3 : 0);
    t0 = tcg_temp_new_i64();
    tcg_gen_qemu_ld64(t0, addr, dc->mmu_idx);
    tcg_temp_free_i32(addr);
    write_operand_64(dc, t0, dc->dst, 4);
    tcg_temp_free_i64(t0);

    GEN_LDST_END;
}

static inline void gen_stb(DisasContext *dc) {
    TCGv_i32 addr, t0;
    GEN_LDST_BEGIN;

    t0 = read_operand_32(dc, dc->dst);
    addr = gen_ld_st_addr(dc, 0);
    tcg_gen_qemu_st8(t0, addr, dc->mmu_idx);
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(t0);

    GEN_LDST_END;
}

static inline void gen_sth(DisasContext *dc) {
    TCGv_i32 addr, t0;
    GEN_LDST_BEGIN;

    t0 = read_operand_32(dc, dc->dst);
    addr = gen_ld_st_addr(dc, 1);
    tcg_gen_qemu_st16(t0, addr, dc->mmu_idx);
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(t0);

    GEN_LDST_END;
}

static inline void gen_stw(DisasContext *dc) {
    TCGv_i32 addr, t0;
    GEN_LDST_BEGIN;

    t0 = read_operand_32(dc, dc->dst);
    addr = gen_ld_st_addr(dc, 2);
    tcg_gen_qemu_st32(t0, addr, dc->mmu_idx);
    tcg_temp_free_i32(addr);
    tcg_temp_free_i32(t0);

    GEN_LDST_END;
}

static inline void gen_stdw(DisasContext *dc) {
    TCGv_i32 addr;
    TCGv_i64 t0;
    GEN_LDST_BEGIN;

    t0 = read_operand_64(dc, dc->dst);
    addr = gen_ld_st_addr(dc, 3);
    tcg_gen_qemu_st64(t0, addr, dc->mmu_idx);
    tcg_temp_free_i32(addr);
    tcg_temp_free_i64(t0);

    GEN_LDST_END;
}

static inline void gen_stndw(DisasContext *dc) {
    TCGv_i32 addr;
    TCGv_i64 t0;
    GEN_LDST_BEGIN;

    t0 = read_operand_64(dc, dc->dst);
    addr = gen_ld_st_addr(dc, (dc->sc) ? 3 : 0);
    tcg_gen_qemu_st64(t0, addr, dc->mmu_idx);
    tcg_temp_free_i32(addr);
    tcg_temp_free_i64(t0);

    GEN_LDST_END;
}
#undef GEN_LDST_BEGIN
#undef GEN_LDST_END


/* Branch instructions */
#define GEN_BRANCH_BEGIN    do {    \
    gen_cond_op_begin(dc);          \
} while(0)

#define GEN_BRANCH_END      do {    \
    gen_cond_op_end(dc);            \
} while(0)

static inline void gen_b_imm(DisasContext *dc) {
    target_ulong pcf;
    uint32_t cst;

    GEN_BRANCH_BEGIN;
    if(dc->src2.t == OPTYPE_SCST12) {
        cst = sign_ext_32(dc->src2.val, 12);
    } else if(dc->src2.t == OPTYPE_SCST21) {
        cst = sign_ext_32(dc->src2.val, 21);
    } else {
        TRANSLATE_ABORT("Invalid constant type for branch");
    }

    pcf = ((cst << 2) + dc->pc_fp);
    gen_set_pc_im(dc, pcf, 5);
    GEN_BRANCH_END;
}
static inline void gen_b_reg(DisasContext *dc) {
    TCGv_i32 t0;

    GEN_BRANCH_BEGIN;
    t0 = read_operand_32(dc, dc->src2);
    gen_set_pc_r(dc, t0, 5);
    tcg_temp_free_i32(t0);
    GEN_BRANCH_END;
}
static inline void gen_callp(DisasContext *dc) {
    target_ulong pcf, retpc;
    TCGv_i32 t0;

    GEN_BRANCH_BEGIN;
    retpc = dc->pc_next_ep;
    t0 = tcg_temp_new_i32();

    tcg_gen_movi_i32(t0, retpc);

    /* Return addr always stored in a3 or b3 */
    gen_store_reg(dc, t0, 3, dc->s, 0);
    tcg_temp_free_i32(t0);

    dc->src2.val = sign_ext_32(dc->src2.val, 21);
    pcf = ((dc->src2.val << 2) + dc->pc_fp);
    gen_set_pc_im(dc, pcf, 5);
    GEN_BRANCH_END;
}


#define BDECBPOS_BEGIN  do {        \
    gen_cond_bdec_bpos_begin(dc);   \
} while(0)

#define BDECBPOS_END    do {        \
    gen_cond_op_end(dc);            \
} while(0)


static inline void gen_bdec(DisasContext *dc) {
    TCGv_i32 t0;
    target_ulong pcf;
    int32_t src;

    BDECBPOS_BEGIN;
    src = sign_ext_32(dc->src2.val, 10);
    pcf = ((src << 2) + dc->pc_fp);
    t0 = read_operand_32(dc, dc->dst);
    tcg_gen_subi_i32(t0, t0, 1);
    write_operand_32(dc, t0, dc->dst, 0);

    tcg_temp_free_i32(t0);
    gen_set_pc_im(dc, pcf, 5);
    BDECBPOS_END;
}


/* Classical instructions */
#define GEN_OP_BEGIN   do {         \
    gen_cond_op_begin(dc);          \
} while(0)

#define GEN_OP_END     do {         \
    gen_cond_op_end(dc);            \
} while(0)

static inline void gen_add_32(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_add_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_add_40(DisasContext *dc) {
    TCGv_i64 t0, t1;
    GEN_OP_BEGIN;
    t0 = read_operand_64(dc, dc->src1);
    t1 = read_operand_64(dc, dc->src2);

    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_andi_i64(t0, t0, 0xffffffffffull);

    tcg_temp_free_i64(t1);
    write_operand_64(dc, t0, dc->dst, 0);
    tcg_temp_free_i64(t0);
    GEN_OP_END;
}

static inline void gen_sub_32(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;
    /* Convention for sub: dst <- src1 - src2 */

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_sub_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}


static inline void gen_mpy32_32(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_mul_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 3);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_mpy32_64(DisasContext *dc) {
    TCGv_i64 t0, t1;
    GEN_OP_BEGIN;
    t0 = read_operand_64(dc, dc->src1);
    t1 = read_operand_64(dc, dc->src2);

    tcg_gen_mul_i64(t0, t0, t1);

    tcg_temp_free_i64(t1);
    write_operand_64(dc, t0, dc->dst, 3);
    tcg_temp_free_i64(t0);
    GEN_OP_END;
}

static inline void gen_mpyli(DisasContext *dc) {
    TCGv_i64 t0, t1;
    GEN_OP_BEGIN;
    t0 = read_operand_64(dc, dc->src1);
    t1 = read_operand_64(dc, dc->src2);

    tcg_gen_andi_i64(t0, t0, 0x0000ffffull);
    tcg_gen_ext16s_i64(t0, t0);

    tcg_gen_mul_i64(t0, t0, t1);

    tcg_temp_free_i64(t1);
    write_operand_64(dc, t0, dc->dst, 3);
    tcg_temp_free_i64(t0);
    GEN_OP_END;
}

static inline void gen_mpy(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;
    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_ext16s_i32(t0, t0);
    tcg_gen_ext16s_i32(t1, t1);

    tcg_gen_mul_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 1);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_mpyu(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;
    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_andi_i32(t1, t1, 0xffff);

    tcg_gen_mul_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 1);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_mpyhu(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_shri_i32(t0, t0, 16);
    tcg_gen_shri_i32(t1, t1, 16);
    tcg_gen_mul_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 1);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_mpyhl(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_sari_i32(t0, t0, 16);
    tcg_gen_ext16s_i32(t1, t1);
    tcg_gen_mul_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 1);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_mpyhlu(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_shri_i32(t0, t0, 16);
    tcg_gen_andi_i32(t1, t1, 0xffff);
    tcg_gen_mul_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 1);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_mpylh(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_ext16s_i32(t0, t0);
    tcg_gen_sari_i32(t1, t1, 16);
    tcg_gen_mul_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 1);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_mpylhu(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_shri_i32(t1, t1, 16);
    tcg_gen_mul_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 1);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_mpysu(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_ext16s_i32(t0, t0);
    tcg_gen_andi_i32(t1, t1, 0xffff);

    tcg_gen_mul_i32(t0, t0, t1);

    tcg_temp_free(t1);
    write_operand_32(dc, t0, dc->dst, 1);
    tcg_temp_free(t0);
    GEN_OP_END;
}

static inline void gen_mpyus(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_ext16s_i32(t1, t1);

    tcg_gen_mul_i32(t0, t0, t1);

    tcg_temp_free(t1);
    write_operand_32(dc, t0, dc->dst, 1);
    tcg_temp_free(t0);
    GEN_OP_END;
}

static inline void gen_addah(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_shli_i32(t0, t0, 1);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_temp_free_i32(t1);

    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_addaw(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_shli_i32(t0, t0, 2);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_temp_free_i32(t1);

    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_addad(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_shli_i32(t0, t0, 3);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_temp_free_i32(t1);

    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_subah(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_shli_i32(t0, t0, 1);
    tcg_gen_sub_i32(t0, t1, t0);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_subaw(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_shli_i32(t0, t0, 2);
    tcg_gen_sub_i32(t0, t1, t0);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}


static inline void gen_pack2(DisasContext *dc) {
    /*
     * src1: (x1 x0)
     * src2: (y1 y0)
     * dst:  (x0 y0)
     */
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_shli_i32(t0, t0, 16);
    tcg_gen_andi_i32(t1, t1, 0xffff);
    tcg_gen_or_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_packl4(DisasContext *dc) {
    /*
     * src1: (x3 x2 x1 x0)
     * src2: (y3 y2 y1 y0)
     * dst:  (x2 x0 y2 y0)
     */
    TCGv_i32 t0, t1, t2;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t2 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t2, t0, 0x00ff0000);
    tcg_gen_shli_i32(t2, t2, 8);
    tcg_gen_andi_i32(t0, t0, 0xff);
    tcg_gen_shli_i32(t0, t0, 16);
    tcg_gen_or_i32(t0, t0, t2);

    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_andi_i32(t2, t1, 0x00ff0000);
    tcg_gen_shri_i32(t2, t2, 8);
    tcg_gen_andi_i32(t1, t1, 0xff);
    tcg_gen_or_i32(t1, t1, t2);
    tcg_temp_free_i32(t2);

    tcg_gen_or_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_lmbd(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    gen_helper_lmbd(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_subc(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    gen_helper_subc(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_mvk(DisasContext *dc) {
    TCGv_i32 t0;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    write_operand_32(dc, t0, dc->dst, 0);

    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_mvkh(DisasContext *dc) {
    TCGv_i32 t0;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->dst);

    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_ori_i32(t0, t0, (dc->src1.val << 16) & 0xffff0000);
    write_operand_32(dc, t0, dc->dst, 0);

    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_dmv(DisasContext *dc) {
    TCGv_i32 t0;
    C6XOperand low = {
        .val = dc->dst.val,
        .t = OPTYPE_UINT,
    };
    C6XOperand high = {
        .val = dc->dst.val+1,
        .t = OPTYPE_UINT,
    };

    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src2);
    write_operand_32(dc, t0, low, 0);
    tcg_temp_free_i32(t0);

    t0 = read_operand_32(dc, dc->src1);
    write_operand_32(dc, t0, high, 0);
    tcg_temp_free_i32(t0);

    GEN_OP_END;
}

static inline void gen_cmpeq_32(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_setcond_i32(TCG_COND_EQ, t0, t0, t1);
    write_operand_32(dc, t0, dc->dst, 0);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_cmplt_32(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_setcond_i32(TCG_COND_LT, t0, t0, t1);
    write_operand_32(dc, t0, dc->dst, 0);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_cmpltu_32(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_setcond_i32(TCG_COND_LTU, t0, t0, t1);
    write_operand_32(dc, t0, dc->dst, 0);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_cmpgt_32(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_setcond_i32(TCG_COND_GT, t0, t0, t1);
    write_operand_32(dc, t0, dc->dst, 0);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_cmpgtu_32(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_setcond_i32(TCG_COND_GTU, t0, t0, t1);
    write_operand_32(dc, t0, dc->dst, 0);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_addkpc(DisasContext *dc) {
    TCGv_i32 t0;
    GEN_OP_BEGIN;

    t0 = tcg_temp_new_i32();
    tcg_gen_movi_i32(t0, (dc->src1.val << 2) + dc->pc_fp);
    write_operand_32(dc, t0, dc->dst, 0);

    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_and(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_and_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_or(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_or_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_xor(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_xor_i32(t0, t0, t1);

    tcg_temp_free_i32(t1);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_shl_32(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_shl_i32(t0, t1, t0);
    write_operand_32(dc, t0, dc->dst, 0);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    GEN_OP_END;
}


static inline void gen_shr_32(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_sar_i32(t0, t1, t0);
    write_operand_32(dc, t0, dc->dst, 0);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    GEN_OP_END;
}


static inline void gen_shru_32(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_shr_i32(t0, t1, t0);
    write_operand_32(dc, t0, dc->dst, 0);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    GEN_OP_END;
}

static inline void gen_shlmb(DisasContext *dc) {
    TCGv_i32 t0, t1;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);
    tcg_gen_shri_i32(t0, t0, 24);
    tcg_gen_shli_i32(t1, t1, 8);
    tcg_gen_or_i32(t0, t0, t1);
    write_operand_32(dc, t0, dc->dst, 0);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    GEN_OP_END;
}

static inline void gen_ext(DisasContext *dc) {
    TCGv_i32 t0, t1, t2;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    t2 = tcg_temp_new_i32();
    tcg_gen_shri_i32(t2, t0, 5);
    tcg_gen_andi_i32(t2, t2, 0x1f);
    tcg_gen_andi_i32(t0, t0, 0x1f);
    tcg_gen_shl_i32(t1, t1, t2);
    tcg_gen_sar_i32(t1, t1, t0);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t2);
    write_operand_32(dc, t1, dc->dst, 0);
    tcg_temp_free_i32(t1);
    GEN_OP_END;
}

static inline void gen_extu(DisasContext *dc) {
    TCGv_i32 t0, t1, t2;
    GEN_OP_BEGIN;

    t2 = tcg_temp_new_i32();
    t0 = read_operand_32(dc, dc->src1);
    t1 = read_operand_32(dc, dc->src2);

    tcg_gen_shri_i32(t2, t0, 5);
    tcg_gen_andi_i32(t2, t2, 0x1f);
    tcg_gen_andi_i32(t0, t0, 0x1f);
    tcg_gen_shl_i32(t1, t1, t2);
    tcg_gen_shr_i32(t1, t1, t0);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t2);
    write_operand_32(dc, t1, dc->dst, 0);
    tcg_temp_free_i32(t1);
    GEN_OP_END;
}


static inline void gen_mvc(DisasContext *dc) {
    TCGv_i32 t0;
    GEN_OP_BEGIN;

    t0 = read_operand_32(dc, dc->src2);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);

    GEN_OP_END;
}

/* Field operations that use dc->csta and dc->cstb */
static inline void gen_ext_imm(DisasContext *dc) {
    TCGv_i32 t0;

    GEN_OP_BEGIN;
    t0 = read_operand_32(dc, dc->src2);
    tcg_gen_shli_i32(t0, t0, dc->csta);
    tcg_gen_sari_i32(t0, t0, dc->cstb);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_extu_imm(DisasContext *dc) {
    TCGv_i32 t0;

    GEN_OP_BEGIN;
    t0 = read_operand_32(dc, dc->src2);
    tcg_gen_shli_i32(t0, t0, dc->csta);
    tcg_gen_shri_i32(t0, t0, dc->cstb);
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_set_imm(DisasContext *dc) {
    TCGv_i32 t0;

    GEN_OP_BEGIN;
    t0 = read_operand_32(dc, dc->src2);
    if(dc->cstb >= dc->csta) {
        uint32_t field = (1 << (dc->cstb - dc->csta + 1)) - 1;
        field <<= dc->csta;
        tcg_gen_ori_i32(t0, t0, field);
    }
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

static inline void gen_clr_imm(DisasContext *dc) {
    TCGv_i32 t0;

    GEN_OP_BEGIN;
    t0 = read_operand_32(dc, dc->src2);
    if(dc->cstb >= dc->csta) {
        uint32_t field = (1 << (dc->cstb - dc->csta + 1)) - 1;
        field <<= dc->csta;
        field = ~field;
        tcg_gen_andi_i32(t0, t0, field);
    }
    write_operand_32(dc, t0, dc->dst, 0);
    tcg_temp_free_i32(t0);
    GEN_OP_END;
}

#undef GEN_OP_BEGIN
#undef GEN_OP_END
#undef DEF_OP


#define SET_OP_VAL(v1, v2, vd) do { \
    dc->src1.val = v1;              \
    dc->src2.val = v2;              \
    dc->dst.val  = vd;              \
} while(0)

#define SET_OP_TYPE(t1, t2, td) do {    \
    dc->src1.t = t1;                    \
    dc->src2.t = t2;                    \
    dc->dst.t  = td;                    \
} while(0)

#define SWAP_OP(o1, o2) do {    \
    C6XOperand tmp_op__ = (o1);\
    (o1) = (o2);                \
    (o2) = tmp_op__;            \
} while(0)

#define BIT_IS_SET(n, b)    (((n) >> (b)) & 0x1)
static int decode_insn(CPUC6XState *env, DisasContext *dc)
{
    uint32_t op;

    dc->num_res = 0;
    dc->ct = CT_NONE;

    dc->creg = (dc->opcode >> 29) & 0x7;
    dc->z    = (dc->opcode >> 28) & 0x1;
    dc->s    = (dc->opcode >> 1)  & 0x1;
    dc->x    = 0;
    dc->elapsed_cycles = 1;

    if(((dc->opcode >> 2) & 0x1f) == 0x10) {
        if((((dc->opcode >> 10) & 0xff) == 0x1) && dc->s) {
            /* .D Linked word */
            TRANSLATE_ABORT(".D Linked word\n");

        } else  {
            /* .D 1 or 2 sources */
            op   = (dc->opcode >> 7)  & 0x3f;
            SET_OP_VAL((dc->opcode >> 13) & 0x1f,
                       (dc->opcode >> 18) & 0x1f,
                       (dc->opcode >> 23) & 0x1f);
            switch(op) {
            case 0x0:
                /* MVK */
                SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_NULL, OPTYPE_SINT);
                gen_mvk(dc);
                break;

            case 0x10:
                /* ADD */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_SINT, OPTYPE_SINT);
                gen_add_32(dc);
                break;

            case 0x11:
                /* SUB dc->dst.val <- dc->src2.val - dc->src1.val */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_SINT, OPTYPE_SINT);

                /* Swap srcs according to gen_sub_32 convention */
                SWAP_OP(dc->src1, dc->src2);
                gen_sub_32(dc);
                break;

            case 0x12:
                /* ADD imm */
                SET_OP_TYPE(OPTYPE_UCST5, OPTYPE_SINT, OPTYPE_SINT);
                gen_add_32(dc);
                break;

            case 0x13:
                /* SUB imm dc->dst.val <- dc->src2.val - dc->src1.val */
                SET_OP_TYPE(OPTYPE_UCST5, OPTYPE_SINT, OPTYPE_SINT);
                /* Swap srcs according to gen_sub convention */
                SWAP_OP(dc->src1, dc->src2);
                gen_sub_32(dc);
                break;

            case 0x30:
                /* ADDAB */
                TRANSLATE_ABORT("ADDAB\n");
                break;

            case 0x31:
                /* SUBAB */
                TRANSLATE_ABORT("SUBAB\n");
                break;

            case 0x32:
                /* ADDAB imm */
                TRANSLATE_ABORT("ADDAB imm\n");
                break;

            case 0x33:
                /* SUBAB imm */
                TRANSLATE_ABORT("SUBAB imm\n");
                break;

            case 0x34:
                /* ADDAH */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_SINT, OPTYPE_SINT);
                gen_addah(dc);
                break;

            case 0x35:
                /* SUBAH */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_SINT, OPTYPE_SINT);
                gen_subah(dc);
                break;

            case 0x36:
                /* ADDAH imm */
                SET_OP_TYPE(OPTYPE_UCST5, OPTYPE_SINT, OPTYPE_SINT);
                gen_addah(dc);
                break;

            case 0x37:
                /* SUBAH imm */
                SET_OP_TYPE(OPTYPE_UCST5, OPTYPE_SINT, OPTYPE_SINT);
                gen_subah(dc);
                break;

            case 0x38:
                /* ADDAW */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_SINT, OPTYPE_SINT);
                gen_addaw(dc);
                break;

            case 0x39:
                /* SUBAW */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_SINT, OPTYPE_SINT);
                gen_subaw(dc);
                break;

            case 0x3a:
                /* ADDAW imm */
                SET_OP_TYPE(OPTYPE_UCST5, OPTYPE_SINT, OPTYPE_SINT);
                gen_addaw(dc);
                break;

            case 0x3b:
                /* SUBAW imm */
                SET_OP_TYPE(OPTYPE_UCST5, OPTYPE_SINT, OPTYPE_SINT);
                gen_subaw(dc);
                break;

            case 0x3c:
                /* ADDAD */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_SINT, OPTYPE_SINT);
                gen_addad(dc);
                break;

            case 0x3d:
                /* ADDAD imm */
                SET_OP_TYPE(OPTYPE_UCST5, OPTYPE_SINT, OPTYPE_SINT);
                gen_addad(dc);
                break;

            default:
                TRANSLATE_ABORT("unk. .D one or two src\n");
                return 1;

            }
        }

    } else if(((dc->opcode >> 2) & 0xf) == 0xc) {
        /* Extended op */
        op = (dc->opcode >> 10) & 0x3;
        switch(op) {
        case 0x0:
        case 0x1:
            /* Extended .M */
            op    = (dc->opcode >> 6)  & 0x1f;
            dc->x = BIT_IS_SET(dc->opcode, 12);
            SET_OP_VAL((dc->opcode >> 13) & 0x1f,
                       (dc->opcode >> 18) & 0x1f,
                       (dc->opcode >> 23) & 0x1f);

            switch(op) {
            case 0x15:
                /* MPYLI */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_SLLONG);
                gen_mpyli(dc);
                break;

            case 0x18:
                /* MPY32U */
                SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_DUINT);
                gen_mpy32_64(dc);
                break;

            default:
                TRANSLATE_ABORT("unk extended .M, pc:0x%08x, op:0x%x", dc->pc, op);
            }
            break;

        case 0x2:
            /* Extended .D */
            op    = (dc->opcode >> 6)  & 0xf;
            dc->x = BIT_IS_SET(dc->opcode, 12);
            SET_OP_VAL((dc->opcode >> 13) & 0x1f,
                       (dc->opcode >> 18) & 0x1f,
                       (dc->opcode >> 23) & 0x1f);

            switch(op) {
            case 0x2:
                /* OR */
                SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
                gen_or(dc);
                break;

            case 0x3:
                /* OR imm */
                SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XUINT, OPTYPE_UINT);
                gen_or(dc);
                break;

            case 0x6:
                /* AND */
                SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
                gen_and(dc);
                break;

            case 0x7:
                /* AND imm */
                SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XUINT, OPTYPE_UINT);
                gen_and(dc);
                break;

            case 0xa:
                /* ADD */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_SINT);
                gen_add_32(dc);
                break;

            case 0xe:
                /* XOR */
                SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
                gen_xor(dc);
                break;

            case 0xf:
                /* XOR imm */
                SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XUINT, OPTYPE_UINT);
                gen_xor(dc);
                break;

            default:
                TRANSLATE_ABORT("unk extended .D 0x%x pc:0x%08x", op, dc->pc);
            }
            break;

        case 0x3:
            /* Extended .S */
            op   = (dc->opcode >> 6) & 0xf;
            dc->x = BIT_IS_SET(dc->opcode, 12);
            SET_OP_VAL((dc->opcode >> 13) & 0x1f,
                       (dc->opcode >> 18) & 0x1f,
                       (dc->opcode >> 23) & 0x1f);

            switch(op) {
            case 0x5:
                /* SUB */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_SINT);
                SWAP_OP(dc->src1, dc->src2);
                gen_sub_32(dc);
                break;

            case 0x9:
                /* SHLMB */
                SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
                gen_shlmb(dc);
                break;

            case 0xb:
                /* DMV */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_DINT);
                gen_dmv(dc);
                break;

            case 0xf:
                /* PACK2 */
                SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
                gen_pack2(dc);
                break;

            default:
                TRANSLATE_ABORT("unk extended .S 0x%x pc:%08x", op, dc->pc);
            }
            break;
        }

    } else if(((dc->opcode >> 2) & 0x3) == 0x3) {
        op    = (dc->opcode >> 4)  & 0x7;
        dc->y = BIT_IS_SET(dc->opcode, 7);

        /* src2 is always b14 or b15 depending on y value */
        SET_OP_VAL((dc->opcode >> 8) & 0x7fff,
                   (dc->y ? 15 : 14),
                   (dc->opcode >> 23) & 0x1f);

        if(!dc->creg && dc->z) {
            /* .D ADDAB/AH/AW Long immediate */
            /* XXX: c64x+ only */

            /* Non-conditional instructions */
            dc->z = 0;

            if(!dc->s) {
                SET_OP_TYPE(OPTYPE_UCST15, OPTYPE_XUINT, OPTYPE_UINT);
                dc->x = 1;
            } else {
                SET_OP_TYPE(OPTYPE_UCST15, OPTYPE_UINT, OPTYPE_UINT);
            }

            switch(op) {
            case 0x3:
                /* .D ADDAB */
                gen_add_32(dc);
                break;

            case 0x7:
                /* .D ADDAW */
                dc->src1.val <<= 2;
                gen_add_32(dc);
                break;

            default:
                TRANSLATE_ABORT("n/i .D ADDAB... Long immediate op:0x%x pc:0x%08x\n", op, dc->pc);
            }

        } else {
            /* .D ld/st long immediate */
            dc->ldst_mode = ADDRMOD_POFFSET_CST;
            dc->y = 1;
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_UINT);

            switch(op) {
            case 0x2:
                /* LDB */
                gen_ldb(dc);
                break;

            case 0x3:
                /* STB */
                gen_stb(dc);
                break;

            case 0x6:
                /* LDW */
                gen_ldw(dc);
                break;

            case 0x7:
                /* STW */
                gen_stw(dc);
                break;

            default:
                TRANSLATE_ABORT("n/i .D ld/st long immediate op:0x%x pc:0x%08x\n", op, dc->pc);
            }
        }

    } else if(((dc->opcode & 0x0c) >> 2) == 0x1) {
        /* .D ld/st */
        op   = ((dc->opcode >> 4)  & 0x7) | ((dc->opcode >> 5) & 0x8);
        SET_OP_VAL((dc->opcode >> 13) & 0x1f,
                   (dc->opcode >> 18) & 0x1f,
                   (dc->opcode >> 23) & 0x1f);
        dc->ldst_mode = (dc->opcode >> 9)  & 0xf;
        dc->y    = BIT_IS_SET(dc->opcode, 7);

        switch(op) {
        case 0x0:
            /* LDHU */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_UINT);
            gen_ldhu(dc);
            break;

        case 0x1:
            /* LDBU */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_UINT);
            gen_ldbu(dc);
            break;

        case 0x2:
            /* LDB */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_UINT);
            gen_ldb(dc);
            break;

        case 0x3:
            /* STB */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_UINT);
            gen_stb(dc);
            break;

        case 0x4:
            /* LDH */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_UINT);
            gen_ldh(dc);
            break;

        case 0x5:
            /* STH */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_UINT);
            gen_sth(dc);
            break;

        case 0x6:
            /* LDW */
        case 0xb:
            /* LDNW */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_UINT);
            gen_ldw(dc);
            break;

        case 0xc:
            /* STDW */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_SLLONG);
            gen_stdw(dc);
            break;

        case 0x7:
            /* STW */
        case 0xd:
            /* STNW */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_UINT);
            gen_stw(dc);
            break;

        case 0xa:
            /* LDNDW */
            dc->dst.val &= 0x1e;
            dc->sc = BIT_IS_SET(dc->opcode, 23);
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_SLLONG);
            gen_ldndw(dc);
            break;

        case 0xe:
            /* LDDW */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_SLLONG);
            gen_lddw(dc);
            break;

        case 0xf:
            /* STNDW */
            dc->dst.val &= 0x1e;
            dc->sc = BIT_IS_SET(dc->opcode, 23);
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_SLLONG);
            gen_stndw(dc);
            break;

        default:
            TRANSLATE_ABORT("unk. .D ld/st op:0x%x pc:0x%08x\n", op, dc->pc);
            break;
        }

    } else if (((dc->opcode & 0x1c) >> 2) == 0x6) {
        op    = (dc->opcode >> 5) & 0x7f;
        dc->x = BIT_IS_SET(dc->opcode, 12);
        SET_OP_VAL((dc->opcode >> 13) & 0x1f,
                   (dc->opcode >> 18) & 0x1f,
                   (dc->opcode >> 23) & 0x1f);

        if(!dc->creg && dc->z) {
            /* .L 1 or 2 sources (uncond) */
            dc->z = 0;
        }

        switch(op) {
        case 0x00:
            /* PACK2 */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_pack2(dc);
            break;

        case 0x02:
            /* ADD imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XSINT, OPTYPE_SINT);
            gen_add_32(dc);
            break;

        case 0x03:
            /* ADD */
            SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_SINT);
            gen_add_32(dc);
            break;

        case 0x06:
            /* SUB imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XSINT, OPTYPE_SINT);
            gen_sub_32(dc);
            break;

        case 0x07:
            /* SUB */
            SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_SINT);
            gen_sub_32(dc);
            break;

        case 0x17:
            /* SUB */
            SET_OP_TYPE(OPTYPE_XSINT, OPTYPE_SINT, OPTYPE_SINT);
            gen_sub_32(dc);
            break;

        case 0x1a:
            /* MVK */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_NULL, OPTYPE_SINT);
            dc->src1.val = dc->src2.val;
            gen_mvk(dc);
            break;

        case 0x29:
            /* ADDU */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_ULONG, OPTYPE_ULONG);
            gen_add_40(dc);
            break;

        case 0x2b:
            /* ADDU */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_ULONG);
            gen_add_40(dc);
            break;

        case 0x46:
            /* CMPGT imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XSINT, OPTYPE_UINT);
            gen_cmpgt_32(dc);
            break;

        case 0x47:
            /* CMPGT */
            SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_UINT);
            gen_cmpgt_32(dc);
            break;

        case 0x4b:
            /* SUBC */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_subc(dc);
            break;

        case 0x4e:
            /* CMPGTU imm */
            SET_OP_TYPE(OPTYPE_UCST5, OPTYPE_XUINT, OPTYPE_UINT);
            dc->src1.val &= 0xf;    /* Only 4 bits */
            gen_cmpgtu_32(dc);
            break;

        case 0x4f:
            /* CMPGTU */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_cmpgtu_32(dc);
            break;

        case 0x52:
            /* CMPEQ imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XSINT, OPTYPE_UINT);
            gen_cmpeq_32(dc);
            break;

        case 0x53:
            /* CMPEQ */
            SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_UINT);
            gen_cmpeq_32(dc);
            break;

        case 0x56:
            /* CMPLT imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XSINT, OPTYPE_UINT);
            gen_cmplt_32(dc);
            break;

        case 0x57:
            /* CMPLT */
            SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_UINT);
            gen_cmplt_32(dc);
            break;

        case 0x5e:
            /* CMPLTU imm */
            SET_OP_TYPE(OPTYPE_UCST5, OPTYPE_XUINT, OPTYPE_UINT);
            gen_cmpltu_32(dc);
            break;

        case 0x5f:
            /* CMPLTU */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_cmpltu_32(dc);
            break;

        case 0x61:
            /* SHLMB */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_shlmb(dc);
            break;

        case 0x68:
            /* PACKL4 */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_packl4(dc);
            break;

        case 0x6a:
            /* LMBD imm */
            SET_OP_TYPE(OPTYPE_UCST5, OPTYPE_XUINT, OPTYPE_UINT);
            gen_lmbd(dc);
            break;

        case 0x6b:
            /* LMBD */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_lmbd(dc);
            break;

        case 0x6e:
            /* XOR imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XUINT, OPTYPE_UINT);
            gen_xor(dc);
            break;

        case 0x6f:
            /* XOR */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_xor(dc);
            break;

        case 0x7a:
            /* AND imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XUINT, OPTYPE_UINT);
            gen_and(dc);
            break;

        case 0x7b:
            /* AND */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_and(dc);
            break;

        case 0x7e:
            /* OR imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XUINT, OPTYPE_UINT);
            gen_or(dc);
            break;

        case 0x7f:
            /* OR */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_or(dc);
            break;

        default:
            TRANSLATE_ABORT("unimplemented .L 1 or 2 src op: 0x%x, pc:0x%08x", op, dc->pc);
        }

    } else if (((dc->opcode >> 2) & 0x3ff) == 0xd6) {
        /* .L unary */
        TRANSLATE_ABORT(".L unary\n");

    } else if (((dc->opcode >> 2) & 0x1f) == 0x0) {
        op   = (dc->opcode >> 7)  & 0x1f;
        SET_OP_VAL((dc->opcode >> 13) & 0x1f,
                   (dc->opcode >> 18) & 0x1f,
                   (dc->opcode >> 23) & 0x1f);
        dc->x = BIT_IS_SET(dc->opcode, 12);

        if(!op) {
            if(BIT_IS_SET(dc->opcode, 17)) {
                if(!(dc->creg) && !(dc->z)) {
                    /* Loop Buffer, uncond */
                    TRANSLATE_ABORT("Loop Buffer, uncond, pc:0x%08x\n", dc->pc);
                } else {
                    /* Loop Buffer */
                    TRANSLATE_ABORT("Loop Buffer\n");
                }
            } else {
                if(dc->z) {
                    /* DINT, RINT, SWE, SWENR */
                    TRANSLATE_ABORT("DINT, RINT, SWE, SWENR pc:0x%08x, opcode:0x%08x\n", dc->pc, dc->opcode);
                } else {
                    /* IDLE, NOP */
                    dc->elapsed_cycles = ((dc->opcode >> 13) & 0xf) + 1;
                }
            }
        } else {
            switch(op) {
            case 0x07:
                /* .M MPYHU */
                SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
                gen_mpyhu(dc);
                break;

            case 0x09:
                /* .M MPYHL */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_SINT);
                gen_mpyhl(dc);
                break;

            case 0x0f:
                /* .M MPYHLU */
                SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
                gen_mpyhlu(dc);
                break;

            case 0x10:
                /* .M MPY32 */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_SINT);
                gen_mpy32_32(dc);
                break;

            case 0x11:
                /* .M MPYLH */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_SINT);
                gen_mpylh(dc);
                break;

            case 0x14:
                /* .M MPY32 64bits */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_DINT);
                gen_mpy32_64(dc);
                break;

            case 0x17:
                /* .M MPYLHU */
                SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
                gen_mpylhu(dc);
                break;

            case 0x18:
                /* .M MPY imm */
                SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XSINT, OPTYPE_SINT);
                gen_mpy(dc);
                break;

            case 0x19:
                /* .M MPY */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_SINT);
                gen_mpy(dc);
                break;

            case 0x1b:
                /* .M MPYSU */
                SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XUINT, OPTYPE_SINT);
                gen_mpysu(dc);
                break;

            case 0x1d:
                /* .M MPYUS */
                SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XSINT, OPTYPE_UINT);
                gen_mpyus(dc);
                break;

            case 0x1e:
                /* .M MPYSU imm */
                SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XUINT, OPTYPE_SINT);
                gen_mpysu(dc);
                break;

            case 0x1f:
                /* .M MPYU */
                SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
                gen_mpyu(dc);
                break;

            default:
                TRANSLATE_ABORT(".M MPY op:0x%x, pc:0x%08x\n", op, dc->pc);
            }
        }

    } else if (((dc->opcode >> 2) & 0x1f) == 0x14) {
        /* .S ADDK */
        SET_OP_VAL((dc->opcode >> 7)  & 0xffff,
                   (dc->opcode >> 23) & 0x1f,
                   (dc->opcode >> 23) & 0x1f);

        SET_OP_TYPE(OPTYPE_SCST16, OPTYPE_UINT, OPTYPE_UINT);
        gen_add_32(dc);

    } else if (((dc->opcode >> 2) & 0x1f) == 0x4) {
        dc->src2.val = (dc->opcode >> 7) & 0x1fffff;

        SET_OP_TYPE(OPTYPE_NULL, OPTYPE_SCST21, OPTYPE_NULL);

        if(!dc->creg && dc->z) {
            /* .S CALLP */
            gen_callp(dc);
            dc->elapsed_cycles = 6; /* Implied NOP 5 */
        } else {
            /* .S Branch displacement */
            gen_b_imm(dc);
        }

    } else if (((dc->opcode >> 2) & 0xf) == 0x8) {
        /* Most common cases, overwriten if necessary */
        /* XXX Split .S 1 or 2 src from the others */
        SET_OP_VAL((dc->opcode >> 13) & 0x1f,
                   (dc->opcode >> 18) & 0x1f,
                   (dc->opcode >> 23) & 0x1f);
        dc->x = BIT_IS_SET(dc->opcode, 12);

        switch((dc->opcode >> 6) & 0x3f) {
        case 0x05:
            /* .S ADDKPC */
            dc->src1.val = (dc->opcode >> 16) & 0x7f;
            dc->src2.val = (dc->opcode >> 13) & 0x7;

            SET_OP_TYPE(OPTYPE_SCST7, OPTYPE_UCST3, OPTYPE_UINT);
            gen_addkpc(dc);

            /* XXX is that correct? The doc is not precise on this point */
            dc->elapsed_cycles = dc->src2.val + 1;

            break;

        case 0x0d:
            dc->src1.val = (dc->opcode >> 13) & 0x7;

            /* .S Branch register */
            if ((dc->opcode >> 23) & 0x1) {
                /* .S Branch register with NOP */
                dc->elapsed_cycles = dc->src1.val + 1;
            }

            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_XUINT, OPTYPE_NULL);
            gen_b_reg(dc);
            break;

        case 0x02:
            /* .S Branch pointer */
            TRANSLATE_ABORT(".S Branch pointer\n");
            break;

        case 0x00:
            dc->src2.val = (dc->opcode >> 13) & 0x3ff;
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_NULL, OPTYPE_SINT);

            if(BIT_IS_SET(dc->opcode, 12)) {
                /* BDEC */
                gen_bdec(dc);
            } else {
                /* BPOS */
                TRANSLATE_ABORT(".S BPOS pc:0x%08x\n", dc->pc);
            }
            break;

        case 0x04:
            /* .S Branch displacement with NOP */
            dc->src1.val = (dc->opcode >> 13) & 0x7;
            dc->src2.val = (dc->opcode >> 16) & 0xfff;

            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_SCST12, OPTYPE_NULL);
            gen_b_imm(dc);

            dc->elapsed_cycles = dc->src1.val + 1;
            break;

        case 0x06:
            /* .S ADD imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XSINT, OPTYPE_SINT);
            gen_add_32(dc);
            break;

        case 0x07:
            /* .S ADD */
            SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_SINT);
            gen_add_32(dc);
            break;

        case 0x0a:
            /* XOR imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XUINT, OPTYPE_UINT);
            gen_xor(dc);
            break;

        case 0x0b:
            /* XOR */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_xor(dc);
            break;

        case 0x0e:
            /* .S MVC reg->ctrl */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_XUINT, OPTYPE_CRLO);
            gen_mvc(dc);
            break;

        case 0x0f:
            /* .S MVC ctrl->reg */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_CRLO, OPTYPE_UINT);
            gen_mvc(dc);
            break;

        case 0x16:
            /* .S SUB imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XSINT, OPTYPE_SINT);
            gen_sub_32(dc);
            break;

        case 0x17:
            /* .S SUB */
            SET_OP_TYPE(OPTYPE_SINT, OPTYPE_XSINT, OPTYPE_SINT);
            gen_sub_32(dc);
            break;

        case 0x1a:
            /* .S OR imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XUINT, OPTYPE_UINT);
            gen_or(dc);
            break;

        case 0x1b:
            /* .S OR */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_or(dc);
            break;

        case 0x1e:
            /* .S AND imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XUINT, OPTYPE_UINT);
            gen_and(dc);
            break;

        case 0x1f:
            /* .S AND */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_and(dc);
            break;

        case 0x26:
            /* .S SHRU imm */
            SET_OP_TYPE(OPTYPE_SCST5, OPTYPE_XUINT, OPTYPE_UINT);
            gen_shru_32(dc);
            break;

        case 0x27:
            /* .S SHRU */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_shru_32(dc);
            break;

        case 0x2b:
            /* .S EXTU */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XUINT, OPTYPE_UINT);
            gen_extu(dc);
            break;

        case 0x2f:
            /* .S EXT */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XSINT, OPTYPE_SINT);
            gen_ext(dc);
            break;

        case 0x32:
            /* .S SHL imm */
            SET_OP_TYPE(OPTYPE_UCST5, OPTYPE_XSINT, OPTYPE_SINT);
            gen_shl_32(dc);
            break;

        case 0x33:
            /* .S SHL */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XSINT, OPTYPE_SINT);
            gen_shl_32(dc);
            break;

        case 0x36:
            /* .S SHR imm */
            SET_OP_TYPE(OPTYPE_UCST5, OPTYPE_XSINT, OPTYPE_SINT);
            gen_shr_32(dc);
            break;

        case 0x37:
            /* .S SHR */
            SET_OP_TYPE(OPTYPE_UINT, OPTYPE_XSINT, OPTYPE_SINT);
            gen_shr_32(dc);
            break;

        case 0x3c:
            /* .S Unary */
            TRANSLATE_ABORT(".S Unary\n");
            break;

        default:
            TRANSLATE_ABORT(".S 1 or 2 sources pc:0x%08x op:0x%x\n", dc->pc, (dc->opcode >> 6) & 0x3f);
            return 1;
        }

    } else if (((dc->opcode >> 2) & 0xf) == 0xa) {
        /* .S Move constant */
        dc->src1.val = (dc->opcode >> 7)  & 0xffff;
        dc->dst.val  = (dc->opcode >> 23) & 0x1f;

        SET_OP_TYPE(OPTYPE_SCST16, OPTYPE_NULL, OPTYPE_SINT);
        if(BIT_IS_SET(dc->opcode, 6)) {
            gen_mvkh(dc);
        } else {
            gen_mvk(dc);
        }

    } else if (((dc->opcode >> 2) & 0xf) == 0x2) {
        /* .S Field operations */
        op   = (dc->opcode >> 6)  & 0x3;
        dc->dst.val  = (dc->opcode >> 23) & 0x1f;
        dc->src2.val = (dc->opcode >> 18) & 0x1f;
        dc->csta = (dc->opcode >> 13) & 0x1f;
        dc->cstb = (dc->opcode >> 8)  & 0x1f;

        switch(op) {
        case 0x0:
            /* EXTU imm */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_UINT, OPTYPE_UINT);
            gen_extu_imm(dc);
            break;

        case 0x1:
            /* EXT imm */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_SINT, OPTYPE_SINT);
            gen_ext_imm(dc);
            break;

        case 0x2:
            /* SET imm */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_UINT, OPTYPE_UINT);
            gen_set_imm(dc);
            break;

        case 0x3:
            /* CLR imm */
            SET_OP_TYPE(OPTYPE_NULL, OPTYPE_UINT, OPTYPE_UINT);
            gen_clr_imm(dc);
            break;

        default:
            TRANSLATE_ABORT("unknown .S Field op %x pc:0x%08x", op, dc->pc);
        }

    } else
        TRANSLATE_ABORT("unk insn.\n");

    return 0;
}
#undef BIT_IS_SET

/* Move every registers into the canonical ones (num 0 of the pool)
 * to have unformized registers usage between TBs */
static inline void gen_c6x_tb_end(void)
{
    int i, j;

    for(i = 0; i < 2; i++) {
        for(j = 0; j < C6X_FILE_REGS_NB; j++) {
            if(!TCGV_EQUAL_I32(REG(i, j), regs[i].pool[0][j])) {
                tcg_gen_mov_i32(regs[i].pool[0][j], REG(i, j));
            }
        }
    }

    tcg_gen_mov_i32(cr_pce1.pool[0], cr_pce1.r_ptr);
}


/* Go through the ds_buffer and write the modification
 * to apply dynamically in the next tb */
static inline void gen_save_context(void)
{
    int i, j, cur_d = -1;
    DelaySlotBuffer *modif;
    TCGv_i32 t0, d, one;

    t0  = tcg_temp_new_i32();
    d   = tcg_temp_new_i32();
    one = tcg_temp_new_i32();

    tcg_gen_movi_i32(one, 1);

    for(i = 0; i < C6X_MAXDS; i++) {
        for(j = 0; j < 2; j++) {
            modif = regs[j].ds_first[DS_IDX_DELAY(i)];
            while(modif != NULL) {
                if(cur_d != i) {
                    tcg_gen_movi_i32(d,  i);
                    cur_d = i;
                }
                tcg_gen_movi_i32(t0, (modif->ptr - regs[j].r_ptr) + j * C6X_FILE_REGS_NB);

                if(modif->cond_type == CT_COND) {
                    gen_helper_tb_save_context_cond(cpu_env, t0, modif->target, modif->cond_flag, d);
                } else {
                    gen_helper_tb_save_context(cpu_env, t0, modif->target, d);
                }
                modif = modif->next;
            }
        }

        modif = &(cr_pce1.ds_buffer[DS_IDX_DELAY(i)]);
        if(PCE1_DS_IS_VALID(modif)) {
            if(cur_d != i) {
                tcg_gen_movi_i32(d,  i);
                cur_d = i;
            }
            if(modif->cond_type == CT_COND) {
                gen_helper_tb_save_context_pc(cpu_env, modif->target,
                                              modif->cond_flag, d);
            } else {
                gen_helper_tb_save_context_pc(cpu_env, modif->target, one, d);
            }
        }
    }

    tcg_temp_free_i32(one);
    tcg_temp_free_i32(d);
    tcg_temp_free_i32(t0);
}

static inline void flush_ds_buffer(void)
{
    int i, j;

    for(i = 0; i < 2; i++) {
        for(j = 0; j < C6X_MAXDS; j++) {
            regs[i].ds_first[j] = NULL;
        }
    }

    for(i = 0; i < C6X_MAXDS; i++) {
        PCE1_DS_SET_AS_INVALID(&(cr_pce1.ds_buffer[i]));
    }
}

static inline void gen_goto_tb(DisasContext *dc, int n, uint32_t dest)
{
    TranslationBlock *tb;

    tb = dc->tb;
    if (!singlestep && (tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)) {
        gen_save_context();
        gen_c6x_tb_end();

        tcg_gen_goto_tb(n);
        gen_set_pc_im_no_ds_canon(dc, dest);

        tcg_gen_exit_tb((long)tb + n);
    } else {
        gen_set_pc_im_no_ds(dc, dest);

        gen_save_context();
        gen_c6x_tb_end();

        tcg_gen_exit_tb(0);
    }
}

static inline int gen_cond_branch_runtime(DisasContext *dc, DelaySlotBuffer* branch, int elapsed_cycles)
{
    TCGLabel *cond_was_false = gen_new_label();
    int branch_type;

    tcg_gen_brcondi_i32(TCG_COND_EQ, branch->cond_flag, 0, cond_was_false);

    if ((branch->branch_type == DISAS_TB_JUMP) && (!dc->is_jmp)) {
        gen_goto_tb(dc, 0, branch->branch_target);
    } else {
        gen_save_context();
        gen_c6x_tb_end();
        tcg_gen_exit_tb(0);
    }

    gen_set_label(cond_was_false);

    if ((branch->branch_type == DISAS_TB_JUMP) && (!dc->is_jmp) && (!elapsed_cycles)) {
        gen_goto_tb(dc, 1, dc->pc);
        branch_type = DISAS_TB_JUMP;
    } else {
        gen_set_pc_im_no_ds(dc, dc->pc);
        branch_type = DISAS_JUMP;
    }

    return branch_type;
}

static inline int gen_inv_cond_branch_runtime(DisasContext *dc, DelaySlotBuffer *branch)
{
    TCGLabel *cond_was_false = gen_new_label();
    int branch_type = DISAS_JUMP;

    if ((branch->branch_type == DISAS_TB_JUMP) && (dc->is_jmp)) {
        tcg_gen_brcondi_i32(TCG_COND_EQ, branch->cond_flag, 0, cond_was_false);
        gen_goto_tb(dc, 0, branch->branch_target);
        
        gen_set_label(cond_was_false);
        gen_goto_tb(dc, 1, branch->branch_inv_target);

        branch_type = DISAS_TB_JUMP;
    }

    return branch_type;
}

static inline void gen_cond_insn_runtime(DelaySlotBuffer* ds)
{
    TCGLabel *cond_was_true = gen_new_label();

    tcg_gen_brcondi_i32(TCG_COND_NE, ds->cond_flag, 0, cond_was_true);
    tcg_gen_mov_i32(ds->target, REG(ds->reg.f, ds->reg.r));
    gen_set_label(cond_was_true);
}

static inline void update_ds_buffer(DisasContext *dc,
                                    unsigned int elapsed_cycles)
{
    int i;
    DelaySlotBuffer* modif;

    C6X_DS_DEBUG("Cycles: %u. Maj:", elapsed_cycles);
    assert(elapsed_cycles);

    while(elapsed_cycles--) {
        ds_buffer_cur = (ds_buffer_cur + 1) % C6X_MAXDS;
        dc->tb_cycles++;

        for(i = 0; i < 2; i++) {
            while(regs[i].ds_first[ds_buffer_cur] != NULL) {
                modif = regs[i].ds_first[ds_buffer_cur];

                if((modif->cond_type == CT_COND) && modif->delay) {
                    gen_cond_insn_runtime(modif);
                }

                if(dc->tb_cycles <= C6X_MAXDS) {
                    tcg_gen_mov_i32(*(modif->ptr), modif->target);
                } else {
                    *(modif->ptr) = modif->target;
                }
                regs[i].ds_first[ds_buffer_cur] = modif->next;
                C6X_DS_DEBUG("%c%u->%d ", i?'b':'a', (unsigned int) (modif->ptr - regs[i].r_ptr), GET_TCGV_I32(modif->target));
            }

            regs[i].ds_first[ds_buffer_cur] = NULL;
        }

        if(dc->tb_cycles <= C6X_MAXDS) {
            gen_helper_tb_restore_context(cpu_env);
        }


        modif = &(cr_pce1.ds_buffer[ds_buffer_cur]);
        if(PCE1_DS_IS_VALID(modif)) {
            cr_pce1.r_ptr = modif->target;
            PCE1_DS_SET_AS_INVALID(modif);

            if(modif->cond_type == CT_COND) {
                dc->is_jmp = gen_cond_branch_runtime(dc, modif, elapsed_cycles);
            } else if (modif->cond_type == CT_INVCOND) {
                dc->is_jmp = gen_inv_cond_branch_runtime(dc, modif);
            } else if ((modif->branch_type == DISAS_TB_JUMP) && (!dc->is_jmp)) {
                gen_goto_tb(dc, 0, modif->branch_target);
                dc->is_jmp = DISAS_TB_JUMP;
            } else {
                dc->is_jmp = DISAS_JUMP;
            }

            if(modif->cond_type != CT_COND) {
                /* If the conditional branch is false, we must consume the remaining cycles.
                 * Otherwise, the branch cancels the nops */
                break;
            }
        }

    }
    C6X_DS_DEBUG("\n");
}

#define BUNDLE_ADDR_MASK 0xffffffe0

/* generate intermediate code in gen_opc_buf and gen_opparam_buf for
   basic block 'tb'. If search_pc is true, also generate PC
   information for each intermediate instruction. */
static inline void gen_intermediate_code_internal(C6XCPU *cpu,
                                           TranslationBlock *tb,
                                           bool search_pc)
{
    CPUState *cs = CPU(cpu);
    CPUC6XState *env = &cpu->env;
    DisasContext ctx, *dc = &ctx;
    int j, lj;
    target_ulong pc_start;
    uint32_t next_page_start;
    int num_insns;
    int max_insns;
    int parallel = 0;
    unsigned int ep_cycles = 0;
    CPUBreakpoint* bp;

    /* generate intermediate code */
    pc_start = tb->pc;

    dc->tb = tb;

    dc->is_jmp = DISAS_NEXT;
    dc->pc = pc_start;
    dc->singlestep_enabled = cs->singlestep_enabled;
    dc->tb_cycles = 0;
    dc->brkpt = 0;
    dc->mmu_idx = cpu_mmu_index(env);

    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    lj = -1;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0)
        max_insns = CF_COUNT_MASK;


    gen_tb_start(tb);

    do {
        dc->pc_ep = dc->pc;
        dc->pc_next_ep = get_pc_next_exec_packet(dc, env);
        do {

            /* Breakpoints handling */
            if (unlikely(!QTAILQ_EMPTY(&cs->breakpoints))) {
                QTAILQ_FOREACH(bp, &cs->breakpoints, entry) {
                    if (bp->pc == dc->pc) {
                        gen_set_pc_im_no_ds(dc, dc->pc);
                        /* Advance PC so that clearing the breakpoint will
                           invalidate this TB.  */
                        gen_save_context();
                        flush_ds_buffer();
                        gen_c6x_tb_end();
                        gen_exception(EXCP_DEBUG);
                        dc->pc += 4;
                        goto done_generating;
                        break;
                    }
                }
            }

            if (search_pc) {
                j = tcg_op_buf_count();
                if (lj < j) {
                    lj++;
                    while (lj < j)
                        tcg_ctx.gen_opc_instr_start[lj++] = 0;
                }
                tcg_ctx.gen_opc_pc[lj] = dc->pc;
                tcg_ctx.gen_opc_instr_start[lj] = 1;
                tcg_ctx.gen_opc_icount[lj] = num_insns;
            }

            if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO))
                gen_io_start();

            dc->pc_fp = dc->pc & BUNDLE_ADDR_MASK;

            dc->opcode = cpu_ldl_code(env, dc->pc);
            parallel = dc->opcode & 0x1;

            if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP))) {
                tcg_gen_debug_insn_start(dc->pc);
            }

            decode_insn(env, dc);
            ep_cycles = MAX(ep_cycles, dc->elapsed_cycles);

            dc->pc += 4;
            num_insns ++;
        } while (!dc->is_jmp
             && !tcg_op_buf_full()
             && dc->pc < next_page_start
             && num_insns < max_insns
             && parallel);

        /* If we are in the middle of a execute packet, it means that we reach
         * one of the QEMU limits (virtual page boundary, gen_opc_ptr full, ...
         * So we do not statically update the ds buffer, they will be done
         * dynamically in the next tb. */
        if(!parallel) {
            update_ds_buffer(dc, ep_cycles);
            ep_cycles = 0;
        }

        /* Translation stops when a conditional branch is encountered.
         * Otherwise the subsequent code could get translated several times.
         * Also stop translation when a page boundary is reached.  This
         * ensures prefetch aborts occur at the right place.  */
    } while (!dc->is_jmp 
         && !tcg_op_buf_full()
         && !cs->singlestep_enabled
         && !singlestep
         && dc->pc < next_page_start
         && num_insns < max_insns);

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

    /* At this stage dc->condjmp will only be set when the skipped
       instruction was a conditional branch or trap, and the PC has
       already been written.  */
    if (unlikely(cs->singlestep_enabled)) {
        gen_set_pc_im_no_ds(dc, dc->pc);
        /* Advance PC so that clearing the breakpoint will
           invalidate this TB.  */
        gen_save_context();
        flush_ds_buffer();
        gen_c6x_tb_end();
        gen_exception(EXCP_DEBUG);
    } else {
        /* While branches must always occur at the end of an IT block,
           there are a few other things that can cause us to terminate
           the TB in the middel of an IT block:
            - Exception generating instructions (bkpt, swi, undefined).
            - Page boundaries.
            - Hardware watchpoints.
           Hardware breakpoints have already been handled and skip this code.
         */


        switch(dc->is_jmp) {
        case DISAS_NEXT:
            gen_goto_tb(dc, 1, dc->pc);
            break;
        default:
        case DISAS_JUMP:
        case DISAS_UPDATE:
            gen_save_context();
            gen_c6x_tb_end();
            /* indicate that the hash table must be used to find the next TB */
            tcg_gen_exit_tb(0);
            break;

        case DISAS_TB_JUMP:
            /* nothing more to generate */
            break;
        }
    }


done_generating:
    flush_ds_buffer();
    pool_reset(dc);
    gen_tb_end(tb, num_insns);

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("----------------\n");
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(cs, pc_start, dc->pc - pc_start, 0);
        qemu_log("\n");
    }
#endif
    if (search_pc) {
        j = tcg_op_buf_count();
        lj++;
        while (lj <= j)
            tcg_ctx.gen_opc_instr_start[lj++] = 0;
    } else {
        tb->size = dc->pc - pc_start;
        tb->icount = num_insns;
    }

    if (tcg_check_temp_count()) {
        fprintf(stderr, "TCG temporary leak before %08x\n", dc->pc);
    }
}

void gen_intermediate_code(CPUC6XState *env, TranslationBlock *tb)
{
    gen_intermediate_code_internal(c6x_env_get_cpu(env), tb, false);
}

void gen_intermediate_code_pc(CPUC6XState *env, TranslationBlock *tb)
{
    gen_intermediate_code_internal(c6x_env_get_cpu(env), tb, true);
}

#define DUMP_GPR_REGS(rf, env_regs, names, pad, mod) \
do { \
    for(i = 0; i < C6X_FILE_REGS_NB; i++) { \
        cpu_fprintf(f, "%" #pad "s=0x%08x", (names)[i], (env_regs)[0][i]); \
        if((i%mod) == (mod - 1)) { \
            cpu_fprintf(f, "\n"); \
        } else { \
            cpu_fprintf(f, " "); \
        } \
    } \
    cpu_fprintf(f, "\n"); \
} while(0)

#define DUMP_CREGS(regs, num, names, pad, mod) \
do { \
    for(i = 0; i < (num); i++) { \
        cpu_fprintf(f, "%" #pad "s=0x%08x", (names)[i], (regs)[i]); \
        if((i%mod) == (mod - 1)) { \
            cpu_fprintf(f, "\n"); \
        } else { \
            cpu_fprintf(f, " "); \
        } \
    } \
    cpu_fprintf(f, "\n"); \
} while(0)



void c6x_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                    int flags)
{
    C6XCPU *cpu = C6X_CPU(cs);
    CPUC6XState *env = &cpu->env;
    int i;

    DUMP_GPR_REGS(REGS_FILE_A, env->aregs, aregsname, 3, 4);
    DUMP_GPR_REGS(REGS_FILE_B, env->bregs, bregsname, 3, 4);

    if(env->cpuid == CSR_CPUID_C64X) {
        DUMP_CREGS(env->cregs, CR_DIER, cregnames, 6, 2);
    } else {
        DUMP_CREGS(env->cregs, C6X_CR_NUM, cregnames, 6, 2);
    }

    cpu_fprintf(f, "pce1=0x%08x\n\n", env->cr_pce1[env->cr_pce1_cur]);
}

void restore_state_to_opc(CPUC6XState *env, TranslationBlock *tb, int pc_pos)
{
    env->cr_pce1[env->cr_pce1_cur] = tcg_ctx.gen_opc_pc[pc_pos];
}
