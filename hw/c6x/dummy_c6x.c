/*
 * Dummy C6X board
 * Inspired from QEMU/MIPS pseudo-board
 *
 */
#include "hw/hw.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"

#define MAX_TEST_NAME   16

enum {
    R_SHUTDOWN = 0,
    R_PUTC,
    R_TESTRES,
    R_TESTNAME,
    R_TESTREAD,
    R_PUT_INT,
    R_MAX
};

typedef struct c6x_dummy_state {
    char test_name [MAX_TEST_NAME];
} c6x_dummy_state;

static c6x_dummy_state state;
static void regs_write (void *opaque, hwaddr addr,
			      uint64_t val, unsigned size)
{
    switch(addr >> 2) {
    case R_SHUTDOWN:
        qemu_system_shutdown_request();
        break;
    case R_PUTC:
        fprintf(stderr, "%c", (char) val);
        break;
    case R_TESTRES:
        fprintf(stderr, "Test %-16s %s\n", state.test_name, (val) ? "OK" : "FAILED");
        break;
    case R_TESTNAME:
        cpu_physical_memory_read(val, (uint8_t*)state.test_name, MAX_TEST_NAME);
        state.test_name[MAX_TEST_NAME-1] = '\0';
        break;
    case R_PUT_INT:
        fprintf(stderr, "%d", (uint32_t)val);
        break;

    case R_TESTREAD:
    default:
        error_report("dummy_c6x: write access to unknown register 0x"
                     TARGET_FMT_plx, addr);
    }
}

static uint64_t regs_read (void *opaque, hwaddr addr,
                                unsigned size)
{
    switch(addr >> 2) {
    case R_TESTREAD:
        return 0xdeadbeef;
        break;

    default:
        return 0;
    }
}

static const MemoryRegionOps regs_ops = {
    .write = regs_write,
    .read = regs_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


typedef struct ResetData {
    CPUC6XState *env;
    uint64_t vector;
} ResetData;


#if 0
static void main_cpu_reset(void *opaque)
{
    ResetData *s = (ResetData *)opaque;
    CPUC6XState *env = s->env;

    cpu_state_reset(env);
}
#endif

static void dummy_c6x_init (MachineState *machine)
{
    C6XCPU *cpu;
    CPUC6XState *env;
    //ResetData *reset_info;
    int size;
    uint64_t elf_entry;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *regs = g_new(MemoryRegion, 1);

    /* init CPUs */
    if (machine->cpu_model == NULL) {
        machine->cpu_model = "tms320c64x+ (sim)";
    }
    cpu = cpu_c6x_init(machine->cpu_model);
    if (!cpu) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }

    env = &cpu->env;

    /* RAM at address zero */
    memory_region_allocate_system_memory(ram, NULL, "dummy_c6x.ram", machine->ram_size);
    memory_region_add_subregion(address_space_mem, 0, ram);

    /* Virtual registers */
    memory_region_init_io(regs, NULL, &regs_ops, NULL, "dummy_c6x.regs", R_MAX * 4);
    memory_region_add_subregion(address_space_mem, 0x1fbf0000, regs);

    size = load_elf(machine->kernel_filename, NULL, NULL, &elf_entry, NULL, NULL, 0,
                    ELF_MACHINE, 1);

    env->cr_pce1[env->cr_pce1_cur] = (uint32_t) elf_entry;

    if(size < 0) {
        fprintf(stderr, "Unable to load %s\n", machine->kernel_filename);
        exit(1);
    }
}

static QEMUMachine dummy_c6x_machine = {
    .name = "dummy_c6x",
    .desc = "dummy c6x platform",
    .init = dummy_c6x_init,
    .is_default = 1,
};

static void dummy_c6x_machine_init(void)
{
    qemu_register_machine(&dummy_c6x_machine);
}

machine_init(dummy_c6x_machine_init);
