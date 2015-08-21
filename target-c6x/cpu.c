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

#include "cpu.h"
#include "qemu-common.h"

static void c6x_cpu_set_pc(CPUState *cs, vaddr value)
{
    C6XCPU *cpu = C6X_CPU(cs);

    cpu->env.cr_pce1[cpu->env.cr_pce1_cur] = value;
}

static ObjectClass *c6x_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    if (cpu_model == NULL) {
        return NULL;
    }

    typename = g_strdup_printf("%s-" TYPE_C6X_CPU, cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    if (oc != NULL && (!object_class_dynamic_cast(oc, TYPE_C6X_CPU) ||
                       object_class_is_abstract(oc))) {
        oc = NULL;
    }
    return oc;
}

/* Sort alphabetically by type name. */
static gint c6x_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a, *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    return strcmp(name_a, name_b);
}

static void c6x_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CPUListState *s = user_data;
    const char *typename;
    char *name;

    typename = object_class_get_name(oc);
    name = g_strndup(typename, strlen(typename) - strlen("-" TYPE_C6X_CPU));
    (*s->cpu_fprintf)(s->file, "  %s\n",
                      name);
    g_free(name);
}

void c6x_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    CPUListState s = {
        .file = f,
        .cpu_fprintf = cpu_fprintf,
    };
    GSList *list;

    list = object_class_get_list(TYPE_C6X_CPU, false);
    list = g_slist_sort(list, c6x_cpu_list_compare);
    (*cpu_fprintf)(f, "Available CPUs:\n");
    g_slist_foreach(list, c6x_cpu_list_entry, &s);
    g_slist_free(list);
}

C6XCPU *c6x_cpu_init(const char *cpu_model)
{
    C6XCPU *cpu;
    ObjectClass *oc;

    oc = c6x_cpu_class_by_name(cpu_model);
    if(!oc) {
        return NULL;
    }

    cpu = C6X_CPU(object_new(object_class_get_name(oc)));

    object_property_set_bool(OBJECT(cpu), true, "realized", NULL);

    return cpu;
}

static bool c6x_cpu_has_work(CPUState *cs)
{
    return (cs->interrupt_request & 
        (CPU_INTERRUPT_HARD | CPU_INTERRUPT_EXITTB));
}

/* CPUClass::reset() */
static void c6x_cpu_reset(CPUState *s)
{
    C6XCPU *cpu = C6X_CPU(s);
    C6XCPUClass *mcc = C6X_CPU_GET_CLASS(cpu);
    CPUC6XState *env = &cpu->env;

    mcc->parent_reset(s);

    memset(env->cregs, 0, sizeof(env->cregs));

    env->cregs[CR_CSR]    = env->cpuid | CSR_M_EN;
    env->cregs[CR_GFPGFR] = 0x0700001d;
    
    /* FIXME: CR_ISTB is device specific */
}

static void c64x_initfn(Object *obj)
{
    C6XCPU *cpu = C6X_CPU(obj);
    CPUC6XState *env = &cpu->env;

    env->cpuid = CSR_CPUID_C64X;
}

static void c64x_plus_initfn(Object *obj)
{
    C6XCPU *cpu = C6X_CPU(obj);
    CPUC6XState *env = &cpu->env;

    env->cpuid = CSR_CPUID_C64X_PLUS;
}

static void c64x_plus_sim_initfn(Object *obj)
{
    C6XCPU *cpu = C6X_CPU(obj);
    CPUC6XState *env = &cpu->env;

    env->cpuid = CSR_CPUID_C64X_PLUS_SIM;
}

static void c6x_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    C6XCPUClass *ccc = C6X_CPU_GET_CLASS(dev);

    cpu_reset(cs);

    qemu_init_vcpu(cs);

    ccc->parent_realize(dev, errp);
}

static void c6x_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    C6XCPU *cpu = C6X_CPU(obj);
    CPUC6XState *env = &cpu->env;
    static bool inited;

    cs->env_ptr = env;
    cpu_exec_init(cs, &error_abort);

    if(tcg_enabled() && !inited) {
        inited = true;
        c6x_translate_init();
        env->cr_pce1_cur = 0;
    }

}

typedef struct C6XCPUInfo {
    const char *name;
    void (*initfn)(Object *obj);
    void (*class_init)(ObjectClass *oc, void *data);
} C6XCPUInfo;

static const C6XCPUInfo c6x_cpus[] = {
    { .name = "tms320c64x", .initfn = c64x_initfn },
    { .name = "tms320c64x+", .initfn = c64x_plus_initfn },
    { .name = "tms320c64x+ (sim)", .initfn = c64x_plus_sim_initfn },
};

static void c6x_cpu_class_init(ObjectClass *c, void *data)
{
    C6XCPUClass *mcc = C6X_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);

    mcc->parent_realize = dc->realize;
    dc->realize = c6x_cpu_realizefn;

    mcc->parent_reset = cc->reset;
    cc->reset = c6x_cpu_reset;

    cc->class_by_name = c6x_cpu_class_by_name;
    cc->has_work = c6x_cpu_has_work;
    cc->do_interrupt = c6x_cpu_do_interrupt;
    cc->dump_state = c6x_cpu_dump_state;
    cc->set_pc = c6x_cpu_set_pc;
    cc->get_phys_page_debug = c6x_cpu_get_phys_page_debug;
}

static void cpu_register(const C6XCPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_C6X_CPU,
        .instance_size = sizeof(C6XCPU),
        .instance_init = info->initfn,
        .class_size = sizeof(C6XCPUClass),
        .class_init = info->class_init,
    };

    type_info.name = g_strdup_printf("%s-" TYPE_C6X_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo c6x_cpu_type_info = {
    .name = TYPE_C6X_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(C6XCPU),
    .instance_init = c6x_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(C6XCPUClass),
    .class_init = c6x_cpu_class_init,
};

static void c6x_cpu_register_types(void)
{
    int i;

    type_register_static(&c6x_cpu_type_info);
    for(i = 0; i < ARRAY_SIZE(c6x_cpus); i++) {
        cpu_register(&c6x_cpus[i]);
    }
}

type_init(c6x_cpu_register_types)

