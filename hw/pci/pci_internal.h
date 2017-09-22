/*
 * QEMU PCI internal
 *
 * Copyright (c) 2005 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_HW_PCI_INTERNAL_H
#define QEMU_HW_PCI_INTERNAL_H

#include "hw/pci/pci_bus.h"

PCIBus *pci_get_bus_devfn(int *devfnp, PCIBus *root, const char *devaddr);

#endif
