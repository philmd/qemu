/*
 * QEMU C6X CPU
 *
 * Copyright (c) 2012 Luc Michel
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */
#ifndef QEMU_C6X_CPU_QOM_H
#define QEMU_C6X_CPU_QOM_H

#include "qom/cpu.h"

#define TYPE_C6X_CPU "c6x-cpu"

#define C6X_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(C6XCPUClass, (klass), TYPE_C6X_CPU)
#define C6X_CPU(obj) \
    OBJECT_CHECK(C6XCPU, (obj), TYPE_C6X_CPU)
#define C6X_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(C6XCPUClass, (obj), TYPE_C6X_CPU)

/**
 * C6XCPUClass:
 * @parent_reset: The parent class' reset handler.
 *
 * An C6X CPU model.
 */
typedef struct C6XCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} C6XCPUClass;

typedef struct C6XCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUC6XState env;
} C6XCPU;

static inline C6XCPU *c6x_env_get_cpu(CPUC6XState *env)
{
    return C6X_CPU(container_of(env, C6XCPU, env));
}

#define ENV_GET_CPU(e) CPU(c6x_env_get_cpu(e))

#define ENV_OFFSET offsetof(C6XCPU, env)

void c6x_cpu_do_interrupt(CPUState *);
void c6x_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                        int flags);
hwaddr c6x_cpu_get_phys_page_debug(CPUState *env, vaddr addr);


#endif
