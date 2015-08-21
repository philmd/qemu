#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "exec/gdbstub.h"
#include "qemu-common.h"
#include "qemu/host-utils.h"

void c6x_cpu_do_interrupt(CPUState *cs)
{
    cpu_abort(cs, "do interrupt n/i\n");
}

/* Return a physical address associated to a virtual one
 * for debugging purposes */
hwaddr c6x_cpu_get_phys_page_debug(CPUState *env, vaddr addr)
{
    return addr;
}

C6XCPU *cpu_c6x_init(const char *cpu_model)
{
    return C6X_CPU(cpu_generic_init(TYPE_C6X_CPU, cpu_model));
}
