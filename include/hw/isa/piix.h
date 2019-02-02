/*
 * QEMU PIIX South Bridge Emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2018 Hervé Poussineau
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_ISA_PIIX_H
#define HW_ISA_PIIX_H

#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/i2c/smbus.h"

extern PCIDevice *piix4_dev;

DeviceState *piix4_create(PCIBus *pci_bus, ISABus **isa_bus, I2CBus **smbus,
                          size_t ide_buses);

#endif
