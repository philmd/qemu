/*
 *  C6X helper routines
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
#include "exec/helper-proto.h"

#ifndef CONFIG_USER_ONLY
#include "sysemu/sysemu.h"
#endif

#define C6X_DEBUGHELPER

#if !defined(CONFIG_USER_ONLY)

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
void tlb_fill (CPUState *cs, target_ulong addr, int is_write, int mmu_idx,
               uintptr_t retaddr)
{
    addr &= ~(uint32_t)(TARGET_PAGE_SIZE - 1);

    tlb_set_page(cs, addr, addr, PAGE_READ | PAGE_WRITE | PAGE_EXEC,
                 mmu_idx, TARGET_PAGE_SIZE);
}
#endif


void HELPER(tb_save_context)(CPUC6XState *env, uint32_t reg, uint32_t val, uint32_t delay)
{
    unsigned int d = (env->tb_context.cur_cycle + delay) % C6X_MAXDS;

    if(!(env->tb_context.mod[d][reg].valid)) {
#ifdef C6X_DEBUGHELPER
        qemu_log_mask(CPU_LOG_EXEC, "save_context: pc:0x%08x, reg:%c%u, "
                      "val:0x%08x, delay:%u\n", env->cr_pce1[0],
                      reg >= C6X_FILE_REGS_NB ? 'b' : 'a',
                      reg >= C6X_FILE_REGS_NB ? reg - C6X_FILE_REGS_NB : reg,
                      val, delay);
#endif

        env->tb_context.mod[d][reg].val = val;
        env->tb_context.mod[d][reg].valid = 1;
        env->tb_context.mod[d][reg].next = env->tb_context.first[d];
        env->tb_context.first[d] = &(env->tb_context.mod[d][reg]);
    }
}


void HELPER(tb_save_context_cond)(CPUC6XState *env, uint32_t reg, uint32_t val, uint32_t cf, uint32_t delay)
{
    unsigned int d = (env->tb_context.cur_cycle + delay) % C6X_MAXDS;


    if((cf) && !(env->tb_context.mod[d][reg].valid)) {
#ifdef C6X_DEBUGHELPER
        qemu_log_mask(CPU_LOG_EXEC, "save_context_cond: pc:0x%08x, "
                      "reg:%c%u, val:0x%08x, delay:%u\n", env->cr_pce1[0],
                      reg >= C6X_FILE_REGS_NB ? 'b' : 'a',
                      reg >= C6X_FILE_REGS_NB ? reg - C6X_FILE_REGS_NB : reg,
                      val, delay);
#endif
        env->tb_context.mod[d][reg].val = val;
        env->tb_context.mod[d][reg].valid = 1;
        env->tb_context.mod[d][reg].next = env->tb_context.first[d];
        env->tb_context.first[d] = &(env->tb_context.mod[d][reg]);
    }
}


void HELPER(tb_save_context_pc)(CPUC6XState *env, uint32_t val, uint32_t cf, uint32_t delay)
{
    unsigned int d = (env->tb_context.cur_cycle + delay) % C6X_MAXDS;

    if(cf && !(env->tb_context.cr_pce1_mod[d].valid)) {
#ifdef C6X_DEBUGHELPER
        qemu_log_mask(CPU_LOG_EXEC, "save_context_pc: pc:0x%08x, "
                      "val:0x%08x, delay:%u\n", env->cr_pce1[0],
                      val, delay);
#endif
        env->tb_context.cr_pce1_mod[d].val = val;
        env->tb_context.cr_pce1_mod[d].valid = 1;
        env->tb_context.cr_pce1_mod[d].cf  = cf;
        env->tb_context.cr_pce1_mod[d].next = env->tb_context.first[d];
        env->tb_context.first[d] = &(env->tb_context.cr_pce1_mod[d]);
        env->tb_context.pending_b++;
    }

}

void HELPER(tb_save_prologue)(CPUC6XState *env, uint32_t reg, uint32_t val, uint32_t delay)
{
    unsigned int d = (env->tb_context.cur_cycle + delay) % C6X_MAXDS;

    if(env->tb_context.pending_b) {
#ifdef C6X_DEBUGHELPER
        qemu_log_mask(CPU_LOG_EXEC, "save_prologue: pc:0x%08x, reg:%c%u, "
                      "val:0x%08x, delay:%u\n", env->cr_pce1[0],
                      reg >= C6X_FILE_REGS_NB ? 'b' : 'a', 
                      reg >= C6X_FILE_REGS_NB ? reg - C6X_FILE_REGS_NB : reg,
                      val, delay);
#endif
        env->tb_context.mod[d][reg].val = val;
        env->tb_context.mod[d][reg].valid = 1;
        env->tb_context.mod[d][reg].next = env->tb_context.first[d];
        env->tb_context.first[d] = &(env->tb_context.mod[d][reg]);
    }
}


void HELPER(tb_save_pc_prologue)(CPUC6XState *env, uint32_t val)
{
    unsigned int d = (env->tb_context.cur_cycle + 5) % C6X_MAXDS;


    if(env->tb_context.pending_b) {
#ifdef C6X_DEBUGHELPER
        qemu_log_mask(CPU_LOG_EXEC, "save_pc_prologue: pc:0x%08x, "
                      "val:0x%08x\n", env->cr_pce1[0], val);
#endif
        env->tb_context.cr_pce1_mod[d].val = val;
        env->tb_context.cr_pce1_mod[d].valid = 1;
        env->tb_context.cr_pce1_mod[d].cf  = 1;
        env->tb_context.cr_pce1_mod[d].next = env->tb_context.first[d];
        env->tb_context.first[d] = &(env->tb_context.cr_pce1_mod[d]);
        env->tb_context.pending_b++;
    }
}


void HELPER(tb_restore_context)(CPUC6XState *env)
{
    CPUState *cs = CPU(c6x_env_get_cpu(env));
    unsigned int c = env->tb_context.cur_cycle;
    C6XTBModif *t, *m = env->tb_context.first[c];
    unsigned int reg, exit_tb = 0;
    
    while(m != NULL) {
        reg = (unsigned int)(m - env->tb_context.mod[c]);
        if(reg >= 2*C6X_FILE_REGS_NB) {
            /* pce1 modification */
#ifdef C6X_DEBUGHELPER
            qemu_log_mask(CPU_LOG_EXEC, "restore_context pc:0x%08x ",
                          env->cr_pce1[0]);
            qemu_log_mask(CPU_LOG_EXEC, "(pce1, val:0x%08x, cf:%u)\n",
                          m->val, m->cf);

            assert(m->valid);
#endif
            if(m->cf) {
                env->cr_pce1[0] = m->val;
                exit_tb = 1;
            }
            env->tb_context.pending_b--;
            m->valid = 0;
            
        } else if(reg >= C6X_FILE_REGS_NB) {
            /* Bank B modification */
            reg -= C6X_FILE_REGS_NB;
            env->bregs[0][reg] = m->val;
#ifdef C6X_DEBUGHELPER
            qemu_log_mask(CPU_LOG_EXEC, "restore_context pc:0x%08x ",
                          env->cr_pce1[0]);
            qemu_log_mask(CPU_LOG_EXEC, "(reg:b%u, val:0x%08x)\n",
                          reg, m->val);
            assert(m->valid);
#endif
            m->valid = 0;
        } else {
            /* Bank A modification */
            env->aregs[0][reg] = m->val;
#ifdef C6X_DEBUGHELPER
            qemu_log_mask(CPU_LOG_EXEC, "restore_context pc:0x%08x ",
                          env->cr_pce1[0]);
            qemu_log_mask(CPU_LOG_EXEC, "(reg:a%u, val:0x%08x)\n",
                          reg, m->val);
            assert(m->valid);
#endif
            m->valid = 0;
        }
        
        t = m;
        m = m->next;
        t->next = NULL;
    }

    env->tb_context.first[c] = NULL;
    env->tb_context.cur_cycle = (env->tb_context.cur_cycle + 1) % C6X_MAXDS;

    if(exit_tb) {
#ifdef C6X_DEBUGHELPER
        qemu_log_mask(CPU_LOG_EXEC, "--> exit\n");
#endif
        cpu_loop_exit(cs);
    }
}

void HELPER(exception)(CPUC6XState* env, uint32_t e)
{
    CPUState *cs = CPU(c6x_env_get_cpu(env));
    cs->exception_index = e;
    cpu_loop_exit(cs);
}


uint32_t HELPER(lmbd)(uint32_t src1, uint32_t src2)
{
    uint32_t ret = 0;

    if(!src1) {
        src1 = 0x80000000u;
    } else {
        src1 = 0;
    }

    while(((src2 & 0x80000000u) == src1) && ret < 32) {
          src2 <<= 1;
          ret++;
    }

    return ret;
}

uint32_t HELPER(subc)(uint32_t src1, uint32_t src2)
{
    int32_t sub = src1 - src2;
    uint32_t ret;

    if(sub >= 0) {
        ret = ((uint32_t)sub << 1) + 1;
    } else {
        ret = src1 << 1;
    }

    return ret;
}

