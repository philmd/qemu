/*
 * Generic ISA Super I/O
 *
 * Copyright (c) 2018 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef HW_ISA_SUPERIO_H
#define HW_ISA_SUPERIO_H

#include "hw/isa/isa.h"

ISADevice *isa_superio_init(ISABus *isa_bus, int serial_count,
                            int parallel_count, int drive_count);

#endif
