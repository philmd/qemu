/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2019 Michael Rolnik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "tcg/tcg.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg-op.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"
#include "exec/translator.h"
#include "exec/gen-icount.h"

/*
 *  Define if you want a BREAK instruction translated to a breakpoint
 *  Active debugging connection is assumed
 *  This is for
 *  https://github.com/seharris/qemu-avr-tests/tree/master/instruction-tests
 *  tests
 */
#undef BREAKPOINT_ON_BREAK

static TCGv cpu_pc;

static TCGv cpu_Cf;
static TCGv cpu_Zf;
static TCGv cpu_Nf;
static TCGv cpu_Vf;
static TCGv cpu_Sf;
static TCGv cpu_Hf;
static TCGv cpu_Tf;
static TCGv cpu_If;

static TCGv cpu_rampD;
static TCGv cpu_rampX;
static TCGv cpu_rampY;
static TCGv cpu_rampZ;

static TCGv cpu_r[NUMBER_OF_CPU_REGISTERS];
static TCGv cpu_eind;
static TCGv cpu_sp;

static TCGv cpu_skip;

static const char reg_names[NUMBER_OF_CPU_REGISTERS][8] = {
    "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
};
#define REG(x) (cpu_r[x])

enum {
    DISAS_EXIT   = DISAS_TARGET_0,  /* We want return to the cpu main loop.  */
    DISAS_LOOKUP = DISAS_TARGET_1,  /* We have a variable condition exit.  */
    DISAS_CHAIN  = DISAS_TARGET_2,  /* We have a single condition exit.  */
};

typedef struct DisasContext DisasContext;

/* This is the state at translation time. */
struct DisasContext {
    TranslationBlock *tb;

    CPUAVRState *env;
    CPUState *cs;

    target_long npc;
    uint32_t opcode;

    /* Routine used to access memory */
    int memidx;
    int bstate;
    int singlestep;

    TCGv skip_var0;
    TCGv skip_var1;
    TCGCond skip_cond;
    bool free_skip_var0;
};

static int to_regs_16_31_by_one(DisasContext *ctx, int indx)
{
    return 16 + (indx % 16);
}

static int to_regs_16_23_by_one(DisasContext *ctx, int indx)
{
    return 16 + (indx % 8);
}
static int to_regs_24_30_by_two(DisasContext *ctx, int indx)
{
    return 24 + (indx % 4) * 2;
}
static int to_regs_00_30_by_two(DisasContext *ctx, int indx)
{
    return (indx % 16) * 2;
}

static uint16_t next_word(DisasContext *ctx)
{
    return cpu_lduw_code(ctx->env, ctx->npc++ * 2);
}

static int append_16(DisasContext *ctx, int x)
{
    return x << 16 | next_word(ctx);
}


static bool avr_have_feature(DisasContext *ctx, int feature)
{
    if (!avr_feature(ctx->env, feature)) {
        gen_helper_unsupported(cpu_env);
        ctx->bstate = DISAS_NORETURN;
        return false;
    }
    return true;
}

static bool decode_insn(DisasContext *ctx, uint16_t insn);
#include "decode_insn.inc.c"

/*
 * Arithmetic Instructions
 */

static void gen_add_CHf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();
    TCGv t3 = tcg_temp_new_i32();

    tcg_gen_and_tl(t1, Rd, Rr); /* t1 = Rd & Rr */
    tcg_gen_andc_tl(t2, Rd, R); /* t2 = Rd & ~R */
    tcg_gen_andc_tl(t3, Rr, R); /* t3 = Rr & ~R */
    tcg_gen_or_tl(t1, t1, t2); /* t1 = t1 | t2 | t3 */
    tcg_gen_or_tl(t1, t1, t3);
    tcg_gen_shri_tl(cpu_Cf, t1, 7); /* Cf = t1(7) */
    tcg_gen_shri_tl(cpu_Hf, t1, 3); /* Hf = t1(3) */
    tcg_gen_andi_tl(cpu_Hf, cpu_Hf, 1);

    tcg_temp_free_i32(t3);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}


static void gen_add_Vf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();

    /* t1 = Rd & Rr & ~R | ~Rd & ~Rr & R */
    /*    = (Rd ^ R) & ~(Rd ^ Rr) */
    tcg_gen_xor_tl(t1, Rd, R);
    tcg_gen_xor_tl(t2, Rd, Rr);
    tcg_gen_andc_tl(t1, t1, t2);
    tcg_gen_shri_tl(cpu_Vf, t1, 7); /* Vf = t1(7) */

    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}


static void gen_sub_CHf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();
    TCGv t3 = tcg_temp_new_i32();

    tcg_gen_not_tl(t1, Rd); /* t1 = ~Rd */
    tcg_gen_and_tl(t2, t1, Rr); /* t2 = ~Rd & Rr */
    tcg_gen_or_tl(t3, t1, Rr); /* t3 = (~Rd | Rr) & R */
    tcg_gen_and_tl(t3, t3, R);
    tcg_gen_or_tl(t2, t2, t3); /* t2 = ~Rd & Rr | ~Rd & R | R & Rr */
    tcg_gen_shri_tl(cpu_Cf, t2, 7); /* Cf = t2(7) */
    tcg_gen_shri_tl(cpu_Hf, t2, 3); /* Hf = t2(3) */
    tcg_gen_andi_tl(cpu_Hf, cpu_Hf, 1);

    tcg_temp_free_i32(t3);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}


static void gen_sub_Vf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();

    /* t1 = Rd & ~Rr & ~R | ~Rd & Rr & R */
    /*    = (Rd ^ R) & (Rd ^ R) */
    tcg_gen_xor_tl(t1, Rd, R);
    tcg_gen_xor_tl(t2, Rd, Rr);
    tcg_gen_and_tl(t1, t1, t2);
    tcg_gen_shri_tl(cpu_Vf, t1, 7); /* Vf = t1(7) */

    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}


static void gen_NSf(TCGv R)
{
    tcg_gen_shri_tl(cpu_Nf, R, 7); /* Nf = R(7) */
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf); /* Sf = Nf ^ Vf */
}


static void gen_ZNSf(TCGv R)
{
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */
    tcg_gen_shri_tl(cpu_Nf, R, 7); /* Nf = R(7) */
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf); /* Sf = Nf ^ Vf */
}

/*
 *  Adds two registers without the C Flag and places the result in the
 *  destination register Rd.
 */
static bool trans_ADD(DisasContext *ctx, arg_ADD *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_add_tl(R, Rd, Rr); /* Rd = Rd + Rr */
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */
    gen_add_CHf(R, Rd, Rr);
    gen_add_Vf(R, Rd, Rr);
    gen_ZNSf(R);
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Adds two registers and the contents of the C Flag and places the result in
 *  the destination register Rd.
 */
static bool trans_ADC(DisasContext *ctx, arg_ADC *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_add_tl(R, Rd, Rr); /* R = Rd + Rr + Cf */
    tcg_gen_add_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */
    gen_add_CHf(R, Rd, Rr);
    gen_add_Vf(R, Rd, Rr);
    gen_ZNSf(R);
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Adds an immediate value (0 - 63) to a register pair and places the result
 *  in the register pair. This instruction operates on the upper four register
 *  pairs, and is well suited for operations on the pointer registers.  This
 *  instruction is not available in all devices. Refer to the device specific
 *  instruction set summary.
 */
static bool trans_ADIW(DisasContext *ctx, arg_ADIW *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_ADIW_SBIW)) {
        return true;
    }

    TCGv RdL = cpu_r[a->rd];
    TCGv RdH = cpu_r[a->rd + 1];
    int Imm = (a->imm);
    TCGv R = tcg_temp_new_i32();
    TCGv Rd = tcg_temp_new_i32();

    tcg_gen_deposit_tl(Rd, RdL, RdH, 8, 8); /* Rd = RdH:RdL */
    tcg_gen_addi_tl(R, Rd, Imm); /* R = Rd + Imm */
    tcg_gen_andi_tl(R, R, 0xffff); /* make it 16 bits */

    tcg_gen_andc_tl(cpu_Cf, Rd, R); /* Cf = Rd & ~R */
    tcg_gen_shri_tl(cpu_Cf, cpu_Cf, 15);
    tcg_gen_andc_tl(cpu_Vf, R, Rd); /* Vf = R & ~Rd */
    tcg_gen_shri_tl(cpu_Vf, cpu_Vf, 15);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */
    tcg_gen_shri_tl(cpu_Nf, R, 15); /* Nf = R(15) */
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf);/* Sf = Nf ^ Vf */
    tcg_gen_andi_tl(RdL, R, 0xff);
    tcg_gen_shri_tl(RdH, R, 8);

    tcg_temp_free_i32(Rd);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Subtracts two registers and places the result in the destination
 *  register Rd.
 */
static bool trans_SUB(DisasContext *ctx, arg_SUB *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, Rd, Rr); /* R = Rd - Rr */
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Subtracts a register and a constant and places the result in the
 *  destination register Rd. This instruction is working on Register R16 to R31
 *  and is very well suited for operations on the X, Y, and Z-pointers.
 */
static bool trans_SUBI(DisasContext *ctx, arg_SUBI *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = tcg_const_i32(a->imm);
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, Rd, Rr); /* R = Rd - Imm */
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);
    tcg_temp_free_i32(Rr);

    return true;
}

/*
 *  Subtracts two registers and subtracts with the C Flag and places the
 *  result in the destination register Rd.
 */
static bool trans_SBC(DisasContext *ctx, arg_SBC *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv zero = tcg_const_i32(0);

    tcg_gen_sub_tl(R, Rd, Rr); /* R = Rd - Rr - Cf */
    tcg_gen_sub_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_NSf(R);

    /*
     * Previous value remains unchanged when the result is zero;
     * cleared otherwise.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, cpu_Zf, R, zero, cpu_Zf, zero);
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(zero);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  SBCI -- Subtract Immediate with Carry
 */
static bool trans_SBCI(DisasContext *ctx, arg_SBCI *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = tcg_const_i32(a->imm);
    TCGv R = tcg_temp_new_i32();
    TCGv zero = tcg_const_i32(0);

    tcg_gen_sub_tl(R, Rd, Rr); /* R = Rd - Rr - Cf */
    tcg_gen_sub_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_NSf(R);

    /*
     * Previous value remains unchanged when the result is zero;
     * cleared otherwise.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, cpu_Zf, R, zero, cpu_Zf, zero);
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(zero);
    tcg_temp_free_i32(R);
    tcg_temp_free_i32(Rr);

    return true;
}

/*
 *  Subtracts an immediate value (0-63) from a register pair and places the
 *  result in the register pair. This instruction operates on the upper four
 *  register pairs, and is well suited for operations on the Pointer Registers.
 *  This instruction is not available in all devices. Refer to the device
 *  specific instruction set summary.
 */
static bool trans_SBIW(DisasContext *ctx, arg_SBIW *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_ADIW_SBIW)) {
        return true;
    }

    TCGv RdL = cpu_r[a->rd];
    TCGv RdH = cpu_r[a->rd + 1];
    int Imm = (a->imm);
    TCGv R = tcg_temp_new_i32();
    TCGv Rd = tcg_temp_new_i32();

    tcg_gen_deposit_tl(Rd, RdL, RdH, 8, 8); /* Rd = RdH:RdL */
    tcg_gen_subi_tl(R, Rd, Imm); /* R = Rd - Imm */
    tcg_gen_andi_tl(R, R, 0xffff); /* make it 16 bits */
    tcg_gen_andc_tl(cpu_Cf, R, Rd);
    tcg_gen_shri_tl(cpu_Cf, cpu_Cf, 15); /* Cf = R & ~Rd */
    tcg_gen_andc_tl(cpu_Vf, Rd, R);
    tcg_gen_shri_tl(cpu_Vf, cpu_Vf, 15); /* Vf = Rd & ~R */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */
    tcg_gen_shri_tl(cpu_Nf, R, 15); /* Nf = R(15) */
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf); /* Sf = Nf ^ Vf */
    tcg_gen_andi_tl(RdL, R, 0xff);
    tcg_gen_shri_tl(RdH, R, 8);

    tcg_temp_free_i32(Rd);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Performs the logical AND between the contents of register Rd and register
 *  Rr and places the result in the destination register Rd.
 */
static bool trans_AND(DisasContext *ctx, arg_AND *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_and_tl(R, Rd, Rr); /* Rd = Rd and Rr */
    tcg_gen_movi_tl(cpu_Vf, 0); /* Vf = 0 */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */
    gen_ZNSf(R);
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Performs the logical AND between the contents of register Rd and a constant
 *  and places the result in the destination register Rd.
 */
static bool trans_ANDI(DisasContext *ctx, arg_ANDI *a)
{
    TCGv Rd = cpu_r[a->rd];
    int Imm = (a->imm);

    tcg_gen_andi_tl(Rd, Rd, Imm); /* Rd = Rd & Imm */
    tcg_gen_movi_tl(cpu_Vf, 0x00); /* Vf = 0 */
    gen_ZNSf(Rd);

    return true;
}

/*
 *  Performs the logical OR between the contents of register Rd and register
 *  Rr and places the result in the destination register Rd.
 */
static bool trans_OR(DisasContext *ctx, arg_OR *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_or_tl(R, Rd, Rr);
    tcg_gen_movi_tl(cpu_Vf, 0);
    gen_ZNSf(R);
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Performs the logical OR between the contents of register Rd and a
 *  constant and places the result in the destination register Rd.
 */
static bool trans_ORI(DisasContext *ctx, arg_ORI *a)
{
    TCGv Rd = cpu_r[a->rd];
    int Imm = (a->imm);

    tcg_gen_ori_tl(Rd, Rd, Imm); /* Rd = Rd | Imm */
    tcg_gen_movi_tl(cpu_Vf, 0x00); /* Vf = 0 */
    gen_ZNSf(Rd);

    return true;
}

/*
 *  Performs the logical EOR between the contents of register Rd and
 *  register Rr and places the result in the destination register Rd.
 */
static bool trans_EOR(DisasContext *ctx, arg_EOR *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];

    tcg_gen_xor_tl(Rd, Rd, Rr);
    tcg_gen_movi_tl(cpu_Vf, 0);
    gen_ZNSf(Rd);

    return true;
}

/*
 *  Clears the specified bits in register Rd. Performs the logical AND
 *  between the contents of register Rd and the complement of the constant mask
 *  K. The result will be placed in register Rd.
 */
static bool trans_COM(DisasContext *ctx, arg_COM *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_xori_tl(Rd, Rd, 0xff);
    tcg_gen_movi_tl(cpu_Cf, 1); /* Cf = 1 */
    tcg_gen_movi_tl(cpu_Vf, 0); /* Vf = 0 */
    gen_ZNSf(Rd);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Replaces the contents of register Rd with its two's complement; the
 *  value $80 is left unchanged.
 */
static bool trans_NEG(DisasContext *ctx, arg_NEG *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv t0 = tcg_const_i32(0);
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, t0, Rd); /* R = 0 - Rd */
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */
    gen_sub_CHf(R, t0, Rd);
    gen_sub_Vf(R, t0, Rd);
    gen_ZNSf(R);
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Adds one -1- to the contents of register Rd and places the result in the
 *  destination register Rd.  The C Flag in SREG is not affected by the
 *  operation, thus allowing the INC instruction to be used on a loop counter in
 *  multiple-precision computations.  When operating on unsigned numbers, only
 *  BREQ and BRNE branches can be expected to perform consistently. When
 *  operating on two's complement values, all signed branches are available.
 */
static bool trans_INC(DisasContext *ctx, arg_INC *a)
{
    TCGv Rd = cpu_r[a->rd];

    tcg_gen_addi_tl(Rd, Rd, 1);
    tcg_gen_andi_tl(Rd, Rd, 0xff);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Vf, Rd, 0x80); /* Vf = Rd == 0x80 */
    gen_ZNSf(Rd);

    return true;
}

/*
 *  Subtracts one -1- from the contents of register Rd and places the result
 *  in the destination register Rd.  The C Flag in SREG is not affected by the
 *  operation, thus allowing the DEC instruction to be used on a loop counter in
 *  multiple-precision computations.  When operating on unsigned values, only
 *  BREQ and BRNE branches can be expected to perform consistently.  When
 *  operating on two's complement values, all signed branches are available.
 */
static bool trans_DEC(DisasContext *ctx, arg_DEC *a)
{
    TCGv Rd = cpu_r[a->rd];

    tcg_gen_subi_tl(Rd, Rd, 1); /* Rd = Rd - 1 */
    tcg_gen_andi_tl(Rd, Rd, 0xff); /* make it 8 bits */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Vf, Rd, 0x7f); /* Vf = Rd == 0x7f */
    gen_ZNSf(Rd);

    return true;
}

/*
 *  This instruction performs 8-bit x 8-bit -> 16-bit unsigned multiplication.
 */
static bool trans_MUL(DisasContext *ctx, arg_MUL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_mul_tl(R, Rd, Rr); /* R = Rd * Rr */
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);
    tcg_gen_shri_tl(cpu_Cf, R, 15); /* Cf = R(15) */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs 8-bit x 8-bit -> 16-bit signed multiplication.
 */
static bool trans_MULS(DisasContext *ctx, arg_MULS *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd); /* make Rd full 32 bit signed */
    tcg_gen_ext8s_tl(t1, Rr); /* make Rr full 32 bit signed */
    tcg_gen_mul_tl(R, t0, t1); /* R = Rd * Rr */
    tcg_gen_andi_tl(R, R, 0xffff); /* make it 16 bits */
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);
    tcg_gen_shri_tl(cpu_Cf, R, 15); /* Cf = R(15) */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs 8-bit x 8-bit -> 16-bit multiplication of a
 *  signed and an unsigned number.
 */
static bool trans_MULSU(DisasContext *ctx, arg_MULSU *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd); /* make Rd full 32 bit signed */
    tcg_gen_mul_tl(R, t0, Rr); /* R = Rd * Rr */
    tcg_gen_andi_tl(R, R, 0xffff); /* make R 16 bits */
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);
    tcg_gen_shri_tl(cpu_Cf, R, 15); /* Cf = R(15) */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs 8-bit x 8-bit -> 16-bit unsigned
 *  multiplication and shifts the result one bit left.
 */
static bool trans_FMUL(DisasContext *ctx, arg_FMUL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_mul_tl(R, Rd, Rr); /* R = Rd * Rr */
    tcg_gen_shri_tl(cpu_Cf, R, 15); /* Cf = R(15) */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */
    tcg_gen_shli_tl(R, R, 1);
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);
    tcg_gen_andi_tl(R1, R1, 0xff);


    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs 8-bit x 8-bit -> 16-bit signed multiplication
 *  and shifts the result one bit left.
 */
static bool trans_FMULS(DisasContext *ctx, arg_FMULS *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd); /* make Rd full 32 bit signed */
    tcg_gen_ext8s_tl(t1, Rr); /* make Rr full 32 bit signed */
    tcg_gen_mul_tl(R, t0, t1); /* R = Rd * Rr */
    tcg_gen_andi_tl(R, R, 0xffff); /* make it 16 bits */
    tcg_gen_shri_tl(cpu_Cf, R, 15); /* Cf = R(15) */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */
    tcg_gen_shli_tl(R, R, 1);
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);
    tcg_gen_andi_tl(R1, R1, 0xff);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs 8-bit x 8-bit -> 16-bit signed multiplication
 *  and shifts the result one bit left.
 */
static bool trans_FMULSU(DisasContext *ctx, arg_FMULSU *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd); /* make Rd full 32 bit signed */
    tcg_gen_mul_tl(R, t0, Rr); /* R = Rd * Rr */
    tcg_gen_andi_tl(R, R, 0xffff); /* make it 16 bits */
    tcg_gen_shri_tl(cpu_Cf, R, 15); /* Cf = R(15) */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */
    tcg_gen_shli_tl(R, R, 1);
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);
    tcg_gen_andi_tl(R1, R1, 0xff);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  The module is an instruction set extension to the AVR CPU, performing
 *  DES iterations. The 64-bit data block (plaintext or ciphertext) is placed in
 *  the CPU register file, registers R0-R7, where LSB of data is placed in LSB
 *  of R0 and MSB of data is placed in MSB of R7. The full 64-bit key (including
 *  parity bits) is placed in registers R8- R15, organized in the register file
 *  with LSB of key in LSB of R8 and MSB of key in MSB of R15. Executing one DES
 *  instruction performs one round in the DES algorithm. Sixteen rounds must be
 *  executed in increasing order to form the correct DES ciphertext or
 *  plaintext. Intermediate results are stored in the register file (R0-R15)
 *  after each DES instruction. The instruction's operand (K) determines which
 *  round is executed, and the half carry flag (H) determines whether encryption
 *  or decryption is performed.  The DES algorithm is described in
 *  "Specifications for the Data Encryption Standard" (Federal Information
 *  Processing Standards Publication 46). Intermediate results in this
 *  implementation differ from the standard because the initial permutation and
 *  the inverse initial permutation are performed each iteration. This does not
 *  affect the result in the final ciphertext or plaintext, but reduces
 *  execution time.
 */
static bool trans_DES(DisasContext *ctx, arg_DES *a)
{
    /* TODO */
    if (!avr_have_feature(ctx, AVR_FEATURE_DES)) {
        return true;
    }

    return true;
}

/*
 * Branch Instructions
 */
static void gen_jmp_ez(DisasContext *ctx)
{
    tcg_gen_deposit_tl(cpu_pc, cpu_r[30], cpu_r[31], 8, 8);
    tcg_gen_or_tl(cpu_pc, cpu_pc, cpu_eind);
    ctx->bstate = DISAS_LOOKUP;
}

static void gen_jmp_z(DisasContext *ctx)
{
    tcg_gen_deposit_tl(cpu_pc, cpu_r[30], cpu_r[31], 8, 8);
    ctx->bstate = DISAS_LOOKUP;
}

static void gen_push_ret(DisasContext *ctx, int ret)
{
    if (avr_feature(ctx->env, AVR_FEATURE_1_BYTE_PC)) {

        TCGv t0 = tcg_const_i32((ret & 0x0000ff));

        tcg_gen_qemu_st_tl(t0, cpu_sp, MMU_DATA_IDX, MO_UB);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);

        tcg_temp_free_i32(t0);
    } else if (avr_feature(ctx->env, AVR_FEATURE_2_BYTE_PC)) {

        TCGv t0 = tcg_const_i32((ret & 0x00ffff));

        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_st_tl(t0, cpu_sp, MMU_DATA_IDX, MO_BEUW);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);

        tcg_temp_free_i32(t0);

    } else if (avr_feature(ctx->env, AVR_FEATURE_3_BYTE_PC)) {

        TCGv lo = tcg_const_i32((ret & 0x0000ff));
        TCGv hi = tcg_const_i32((ret & 0xffff00) >> 8);

        tcg_gen_qemu_st_tl(lo, cpu_sp, MMU_DATA_IDX, MO_UB);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 2);
        tcg_gen_qemu_st_tl(hi, cpu_sp, MMU_DATA_IDX, MO_BEUW);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);

        tcg_temp_free_i32(lo);
        tcg_temp_free_i32(hi);
    }
}

static void gen_pop_ret(DisasContext *ctx, TCGv ret)
{
    if (avr_feature(ctx->env, AVR_FEATURE_1_BYTE_PC)) {
        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_ld_tl(ret, cpu_sp, MMU_DATA_IDX, MO_UB);
    } else if (avr_feature(ctx->env, AVR_FEATURE_2_BYTE_PC)) {
        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_ld_tl(ret, cpu_sp, MMU_DATA_IDX, MO_BEUW);
        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
    } else if (avr_feature(ctx->env, AVR_FEATURE_3_BYTE_PC)) {
        TCGv lo = tcg_temp_new_i32();
        TCGv hi = tcg_temp_new_i32();

        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_ld_tl(hi, cpu_sp, MMU_DATA_IDX, MO_BEUW);

        tcg_gen_addi_tl(cpu_sp, cpu_sp, 2);
        tcg_gen_qemu_ld_tl(lo, cpu_sp, MMU_DATA_IDX, MO_UB);

        tcg_gen_deposit_tl(ret, lo, hi, 8, 16);

        tcg_temp_free_i32(lo);
        tcg_temp_free_i32(hi);
    }
}

static void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    TranslationBlock *tb = ctx->tb;

    if (ctx->singlestep == 0) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb(tb, n);
    } else {
        tcg_gen_movi_i32(cpu_pc, dest);
        gen_helper_debug(cpu_env);
        tcg_gen_exit_tb(NULL, 0);
    }
    ctx->bstate = DISAS_NORETURN;
}

/*
 *  Relative jump to an address within PC - 2K +1 and PC + 2K (words). For
 *  AVR microcontrollers with Program memory not exceeding 4K words (8KB) this
 *  instruction can address the entire memory from every address location. See
 *  also JMP.
 */
static bool trans_RJMP(DisasContext *ctx, arg_RJMP *a)
{
    int dst = ctx->npc + a->imm;

    gen_goto_tb(ctx, 0, dst);

    return true;
}

/*
 *  Indirect jump to the address pointed to by the Z (16 bits) Pointer
 *  Register in the Register File. The Z-pointer Register is 16 bits wide and
 *  allows jump within the lowest 64K words (128KB) section of Program memory.
 *  This instruction is not available in all devices. Refer to the device
 *  specific instruction set summary.
 */
static bool trans_IJMP(DisasContext *ctx, arg_IJMP *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_IJMP_ICALL)) {
        return true;
    }

    gen_jmp_z(ctx);

    return true;
}

/*
 *  Indirect jump to the address pointed to by the Z (16 bits) Pointer
 *  Register in the Register File and the EIND Register in the I/O space. This
 *  instruction allows for indirect jumps to the entire 4M (words) Program
 *  memory space. See also IJMP.  This instruction is not available in all
 *  devices. Refer to the device specific instruction set summary.
 */
static bool trans_EIJMP(DisasContext *ctx, arg_EIJMP *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_EIJMP_EICALL)) {
        return true;
    }

    gen_jmp_ez(ctx);
    return true;
}

/*
 *  Jump to an address within the entire 4M (words) Program memory. See also
 *  RJMP.  This instruction is not available in all devices. Refer to the device
 *  specific instruction set summary.0
 */
static bool trans_JMP(DisasContext *ctx, arg_JMP *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_JMP_CALL)) {
        return true;
    }

    gen_goto_tb(ctx, 0, a->imm);

    return true;
}

/*
 *  Relative call to an address within PC - 2K + 1 and PC + 2K (words). The
 *  return address (the instruction after the RCALL) is stored onto the Stack.
 *  See also CALL. For AVR microcontrollers with Program memory not exceeding 4K
 *  words (8KB) this instruction can address the entire memory from every
 *  address location. The Stack Pointer uses a post-decrement scheme during
 *  RCALL.
 */
static bool trans_RCALL(DisasContext *ctx, arg_RCALL *a)
{
    int ret = ctx->npc;
    int dst = ctx->npc + a->imm;

    gen_push_ret(ctx, ret);
    gen_goto_tb(ctx, 0, dst);

    return true;
}

/*
 *  Calls to a subroutine within the entire 4M (words) Program memory. The
 *  return address (to the instruction after the CALL) will be stored onto the
 *  Stack. See also RCALL. The Stack Pointer uses a post-decrement scheme during
 *  CALL.  This instruction is not available in all devices. Refer to the device
 *  specific instruction set summary.
 */
static bool trans_ICALL(DisasContext *ctx, arg_ICALL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_IJMP_ICALL)) {
        return true;
    }

    int ret = ctx->npc;

    gen_push_ret(ctx, ret);
    gen_jmp_z(ctx);

    return true;
}

/*
 *  Indirect call of a subroutine pointed to by the Z (16 bits) Pointer
 *  Register in the Register File and the EIND Register in the I/O space. This
 *  instruction allows for indirect calls to the entire 4M (words) Program
 *  memory space. See also ICALL. The Stack Pointer uses a post-decrement scheme
 *  during EICALL.  This instruction is not available in all devices. Refer to
 *  the device specific instruction set summary.
 */
static bool trans_EICALL(DisasContext *ctx, arg_EICALL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_EIJMP_EICALL)) {
        return true;
    }

    int ret = ctx->npc;

    gen_push_ret(ctx, ret);
    gen_jmp_ez(ctx);
    return true;
}

/*
 *  Calls to a subroutine within the entire Program memory. The return
 *  address (to the instruction after the CALL) will be stored onto the Stack.
 *  (See also RCALL). The Stack Pointer uses a post-decrement scheme during
 *  CALL.  This instruction is not available in all devices. Refer to the device
 *  specific instruction set summary.
 */
static bool trans_CALL(DisasContext *ctx, arg_CALL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_JMP_CALL)) {
        return true;
    }

    int Imm = a->imm;
    int ret = ctx->npc;

    gen_push_ret(ctx, ret);
    gen_goto_tb(ctx, 0, Imm);

    return true;
}

/*
 *  Returns from subroutine. The return address is loaded from the STACK.
 *  The Stack Pointer uses a preincrement scheme during RET.
 */
static bool trans_RET(DisasContext *ctx, arg_RET *a)
{
    gen_pop_ret(ctx, cpu_pc);

    ctx->bstate = DISAS_LOOKUP;
    return true;
}

/*
 *  Returns from interrupt. The return address is loaded from the STACK and
 *  the Global Interrupt Flag is set.  Note that the Status Register is not
 *  automatically stored when entering an interrupt routine, and it is not
 *  restored when returning from an interrupt routine. This must be handled by
 *  the application program. The Stack Pointer uses a pre-increment scheme
 *  during RETI.
 */
static bool trans_RETI(DisasContext *ctx, arg_RETI *a)
{
    gen_pop_ret(ctx, cpu_pc);
    tcg_gen_movi_tl(cpu_If, 1);

    /* Need to return to main loop to re-evaluate interrupts.  */
    ctx->bstate = DISAS_EXIT;
    return true;
}

/*
 *  This instruction performs a compare between two registers Rd and Rr, and
 *  skips the next instruction if Rd = Rr.
 */
static bool trans_CPSE(DisasContext *ctx, arg_CPSE *a)
{
    ctx->skip_cond = TCG_COND_EQ;
    ctx->skip_var0 = cpu_r[a->rd];
    ctx->skip_var1 = cpu_r[a->rr];
    return true;
}

/*
 *  This instruction performs a compare between two registers Rd and Rr.
 *  None of the registers are changed. All conditional branches can be used
 *  after this instruction.
 */
static bool trans_CP(DisasContext *ctx, arg_CP *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, Rd, Rr); /* R = Rd - Rr */
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */

    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs a compare between two registers Rd and Rr and
 *  also takes into account the previous carry. None of the registers are
 *  changed. All conditional branches can be used after this instruction.
 */
static bool trans_CPC(DisasContext *ctx, arg_CPC *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv zero = tcg_const_i32(0);

    tcg_gen_sub_tl(R, Rd, Rr); /* R = Rd - Rr - Cf */
    tcg_gen_sub_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_NSf(R);

    /*
     * Previous value remains unchanged when the result is zero;
     * cleared otherwise.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, cpu_Zf, R, zero, cpu_Zf, zero);

    tcg_temp_free_i32(zero);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs a compare between register Rd and a constant.
 *  The register is not changed. All conditional branches can be used after this
 *  instruction.
 */
static bool trans_CPI(DisasContext *ctx, arg_CPI *a)
{
    TCGv Rd = cpu_r[a->rd];
    int Imm = a->imm;
    TCGv Rr = tcg_const_i32(Imm);
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, Rd, Rr); /* R = Rd - Rr */
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    tcg_temp_free_i32(R);
    tcg_temp_free_i32(Rr);

    return true;
}

/*
 *  This instruction tests a single bit in a register and skips the next
 *  instruction if the bit is cleared.
 */
static bool trans_SBRC(DisasContext *ctx, arg_SBRC *a)
{
    TCGv Rr = cpu_r[a->rr];

    ctx->skip_cond = TCG_COND_EQ;
    ctx->skip_var0 = tcg_temp_new();
    ctx->free_skip_var0 = true;

    tcg_gen_andi_tl(ctx->skip_var0, Rr, 1 << a->bit);
    return true;
}

/*
 *  This instruction tests a single bit in a register and skips the next
 *  instruction if the bit is set.
 */
static bool trans_SBRS(DisasContext *ctx, arg_SBRS *a)
{
    TCGv Rr = cpu_r[a->rr];

    ctx->skip_cond = TCG_COND_NE;
    ctx->skip_var0 = tcg_temp_new();
    ctx->free_skip_var0 = true;

    tcg_gen_andi_tl(ctx->skip_var0, Rr, 1 << a->bit);
    return true;
}

/*
 *  This instruction tests a single bit in an I/O Register and skips the
 *  next instruction if the bit is cleared. This instruction operates on the
 *  lower 32 I/O Registers -- addresses 0-31.
 */
static bool trans_SBIC(DisasContext *ctx, arg_SBIC *a)
{
    TCGv temp = tcg_const_i32(a->reg);

    gen_helper_inb(temp, cpu_env, temp);
    tcg_gen_andi_tl(temp, temp, 1 << a->bit);
    ctx->skip_cond = TCG_COND_EQ;
    ctx->skip_var0 = temp;
    ctx->free_skip_var0 = true;

    return true;
}

/*
 *  This instruction tests a single bit in an I/O Register and skips the
 *  next instruction if the bit is set. This instruction operates on the lower
 *  32 I/O Registers -- addresses 0-31.
 */
static bool trans_SBIS(DisasContext *ctx, arg_SBIS *a)
{
    TCGv temp = tcg_const_i32(a->reg);

    gen_helper_inb(temp, cpu_env, temp);
    tcg_gen_andi_tl(temp, temp, 1 << a->bit);
    ctx->skip_cond = TCG_COND_NE;
    ctx->skip_var0 = temp;
    ctx->free_skip_var0 = true;

    return true;
}

/*
 *  Conditional relative branch. Tests a single bit in SREG and branches
 *  relatively to PC if the bit is cleared. This instruction branches relatively
 *  to PC in either direction (PC - 63 < = destination <= PC + 64). The
 *  parameter k is the offset from PC and is represented in two's complement
 *  form.
 */
static bool trans_BRBC(DisasContext *ctx, arg_BRBC *a)
{
    TCGLabel *not_taken = gen_new_label();

    TCGv var;

    switch (a->bit) {
    case 0x00:
        var = cpu_Cf;
        break;
    case 0x01:
        var = cpu_Zf;
        break;
    case 0x02:
        var = cpu_Nf;
        break;
    case 0x03:
        var = cpu_Vf;
        break;
    case 0x04:
        var = cpu_Sf;
        break;
    case 0x05:
        var = cpu_Hf;
        break;
    case 0x06:
        var = cpu_Tf;
        break;
    case 0x07:
        var = cpu_If;
        break;
    default:
        g_assert_not_reached();
    }

    tcg_gen_brcondi_i32(TCG_COND_NE, var, 0, not_taken);
    gen_goto_tb(ctx, 0, ctx->npc + a->imm);
    gen_set_label(not_taken);

    ctx->bstate = DISAS_CHAIN;
    return true;
}

/*
 *  Conditional relative branch. Tests a single bit in SREG and branches
 *  relatively to PC if the bit is set. This instruction branches relatively to
 *  PC in either direction (PC - 63 < = destination <= PC + 64). The parameter k
 *  is the offset from PC and is represented in two's complement form.
 */
static bool trans_BRBS(DisasContext *ctx, arg_BRBS *a)
{
    TCGLabel *not_taken = gen_new_label();

    TCGv var;

    switch (a->bit) {
    case 0x00:
        var = cpu_Cf;
        break;
    case 0x01:
        var = cpu_Zf;
        break;
    case 0x02:
        var = cpu_Nf;
        break;
    case 0x03:
        var = cpu_Vf;
        break;
    case 0x04:
        var = cpu_Sf;
        break;
    case 0x05:
        var = cpu_Hf;
        break;
    case 0x06:
        var = cpu_Tf;
        break;
    case 0x07:
        var = cpu_If;
        break;
    default:
        g_assert_not_reached();
    }

    tcg_gen_brcondi_i32(TCG_COND_EQ, var, 0, not_taken);
    gen_goto_tb(ctx, 0, ctx->npc + a->imm);
    gen_set_label(not_taken);

    ctx->bstate = DISAS_CHAIN;
    return true;
}

