/*
 * Generic ISA Super I/O
 *
 * Copyright (c) 2018 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/blockdev.h"
#include "hw/isa/superio.h"
#include "hw/char/serial.h"
#include "hw/char/parallel.h"
#include "hw/block/fdc.h"
#include "hw/input/i8042.h"

ISADevice *isa_superio_init(ISABus *isa_bus, int serial_count,
                            int parallel_count, int drive_count)
{
    serial_hds_isa_init(isa_bus, 0, serial_count);

    parallel_hds_isa_init(isa_bus, parallel_count);

    if (drive_count) {
        DriveInfo **fd;
        int i;

        if (drive_count > MAX_FD) {
            warn_report("superio: ignoring %d floppy controllers",
                        drive_count - MAX_FD);
            drive_count = MAX_FD;
        }
        fd = g_new(DriveInfo *, drive_count);

        for (i = 0; i < drive_count; i++) {
            fd[i] = drive_get(IF_FLOPPY, 0, i);
        }
        fdctrl_init_isa(isa_bus, fd);

        g_free(fd); /* FIXME */
    }

    return isa_create_simple(isa_bus, TYPE_I8042);
}
