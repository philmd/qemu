/*
 * MIPS gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2013 SUSE LINUX Products GmbH
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "cpu.h"
#include "internal.h"
#include "exec/gdbstub.h"
#include "gdbstub/helpers.h"
#include "fpu_helper.h"

static int mips_gdb_get_regl(const CPUMIPSState *env,
                             GByteArray *buf, uint64_t val)
{
    return mips_env_is_64bit(env) ? gdb_get_reg64(buf, val)
                                  : gdb_get_reg32(buf, val);
}

static bool mips_gdb_read_value(CPUMIPSState *env, int n, uint64_t *pval)
{
    uint64_t val;

    if (n < 32) {
        *pval = env->active_tc.gpr[n];
        return true;
    }
    if (env->CP0_Config1 & (1 << CP0C1_FP) && n >= 38 && n < 72) {
        switch (n) {
        case 70:
            val = (int32_t)env->active_fpu.fcr31;
            break;
        case 71:
            val = (int32_t)env->active_fpu.fcr0;
            break;
        default:
            if (env->CP0_Status & (1 << CP0St_FR)) {
                val = env->active_fpu.fpr[n - 38].d;
            } else {
                val = env->active_fpu.fpr[n - 38].w[FP_ENDIAN_IDX];
            }
            break;
        }
    } else {
        switch (n) {
        case 32:
            val = (int32_t)env->CP0_Status;
            break;
        case 33:
            val = env->active_tc.LO[0];
            break;
        case 34:
            val = env->active_tc.HI[0];
            break;
        case 35:
            val = env->CP0_BadVAddr;
            break;
        case 36:
            val = (int32_t)env->CP0_Cause;
            break;
        case 37:
            val = env->active_tc.PC | !!(env->hflags & MIPS_HFLAG_M16);
            break;
        case 72:
            val = 0; /* fp */
            break;
        case 89:
            val = (int32_t)env->CP0_PRid;
            break;
        default:
            if (n > 89) {
                return false;
            }
            /* 16 embedded regs.  */
            val = 0;
            break;
        }
    }
    *pval = val;
    return true;
}

int mips_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    CPUMIPSState *env = cpu_env(cs);
    uint64_t val;

    if (!mips_gdb_read_value(env, n, &val)) {
        return 0;
    }
    return mips_gdb_get_regl(env, mem_buf, val);
}

int mips_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    const unsigned regsz = target_long_bits() / 8;
    CPUMIPSState *env = cpu_env(cs);
    uint64_t tmp;

    tmp = ldn_p(mem_buf, regsz);

    if (n < 32) {
        env->active_tc.gpr[n] = tmp;
        return regsz;
    }
    if (env->CP0_Config1 & (1 << CP0C1_FP) && n >= 38 && n < 72) {
        switch (n) {
        case 70:
            env->active_fpu.fcr31 = (tmp & env->active_fpu.fcr31_rw_bitmask) |
                  (env->active_fpu.fcr31 & ~(env->active_fpu.fcr31_rw_bitmask));
            restore_fp_status(env);
            break;
        case 71:
            /* FIR is read-only.  Ignore writes.  */
            break;
        default:
            if (env->CP0_Status & (1 << CP0St_FR)) {
                env->active_fpu.fpr[n - 38].d = tmp;
            } else {
                env->active_fpu.fpr[n - 38].w[FP_ENDIAN_IDX] = tmp;
            }
            break;
        }
        return regsz;
    }
    switch (n) {
    case 32:
#ifndef CONFIG_USER_ONLY
        cpu_mips_store_status(env, tmp);
#endif
        break;
    case 33:
        env->active_tc.LO[0] = tmp;
        break;
    case 34:
        env->active_tc.HI[0] = tmp;
        break;
    case 35:
        env->CP0_BadVAddr = tmp;
        break;
    case 36:
#ifndef CONFIG_USER_ONLY
        cpu_mips_store_cause(env, tmp);
#endif
        break;
    case 37:
        env->active_tc.PC = deposit64(tmp, 63, 1, 0);
        if (tmp & 1) {
            env->hflags |= MIPS_HFLAG_M16;
        } else {
            env->hflags &= ~(MIPS_HFLAG_M16);
        }
        break;
    case 72: /* fp, ignored */
        break;
    default:
        if (n > 89) {
            return 0;
        }
        /* Other registers are readonly.  Ignore writes.  */
        break;
    }

    return regsz;
}

static int mips_gdb_cpu_legacy_regnum(int n)
{
    switch (n) {
    case 32:
        return 33; /* lo */
    case 33:
        return 34; /* hi */
    case 34:
        return 37; /* pc */
    default:
        return n;
    }
}

/* org.gnu.gdb.mips.cp0: status, badvaddr, cause */
static const int mips_gdb_cp0_legacy_regnum[] = { 32, 35, 36 };

/* org.gnu.gdb.mips.fpu: f0..f31, fcsr, fir */
#define MIPS_GDB_FPU_LEGACY_BASE  38

static int mips_gdb_read_legacy(CPUState *cs, GByteArray *buf, int n)
{
    uint64_t val;

    if (!mips_gdb_read_value(cpu_env(cs), n, &val)) {
        return 0;
    }
    return target_long_bits() == 64 ? gdb_get_reg64(buf, val)
                                    : gdb_get_reg32(buf, val);
}

static int mips_gdb_read_cpu(CPUState *cs, GByteArray *buf, int n)
{
    return mips_gdb_read_legacy(cs, buf, mips_gdb_cpu_legacy_regnum(n));
}

static int mips_gdb_write_cpu(CPUState *cs, uint8_t *buf, int n)
{
    return mips_cpu_gdb_write_register(cs, buf,
                                       mips_gdb_cpu_legacy_regnum(n));
}

static int mips_gdb_read_cp0(CPUState *cs, GByteArray *buf, int n)
{
    return mips_gdb_read_legacy(cs, buf, mips_gdb_cp0_legacy_regnum[n]);
}

static int mips_gdb_write_cp0(CPUState *cs, uint8_t *buf, int n)
{
    return mips_cpu_gdb_write_register(cs, buf,
                                       mips_gdb_cp0_legacy_regnum[n]);
}

static int mips_gdb_read_fpu(CPUState *cs, GByteArray *buf, int n)
{
    return mips_gdb_read_legacy(cs, buf, MIPS_GDB_FPU_LEGACY_BASE + n);
}

static int mips_gdb_write_fpu(CPUState *cs, uint8_t *buf, int n)
{
    return mips_cpu_gdb_write_register(cs, buf,
                                       MIPS_GDB_FPU_LEGACY_BASE + n);
}

void mips_cpu_gdb_class_init_xml(CPUClass *cc)
{
    cc->gdb_core_xml_file = target_long_bits() == 64 ? "mips64-cpu.xml"
                                                     : "mips32-cpu.xml";
    cc->gdb_num_core_regs = 0;
    cc->gdb_read_register = mips_gdb_read_cpu;
    cc->gdb_write_register = mips_gdb_write_cpu;
}

void mips_cpu_register_gdb_regs(CPUState *cs)
{
    const bool is_64bit = target_long_bits() == 64;
    const char *cp0_xml = is_64bit ? "mips64-cp0.xml" : "mips32-cp0.xml";
    const char *fpu_xml = is_64bit ? "mips64-fpu.xml" : "mips32-fpu.xml";

    gdb_register_coprocessor(cs, mips_gdb_read_cp0, mips_gdb_write_cp0,
                             gdb_find_static_feature(cp0_xml));
    gdb_register_coprocessor(cs, mips_gdb_read_fpu, mips_gdb_write_fpu,
                             gdb_find_static_feature(fpu_xml));
}
