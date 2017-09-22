/*
 * QEMU network devices
 *
 * Copyright (c) 2005 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_HW_NET_PCI_H
#define QEMU_HW_NET_PCI_H

#include "net/net.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"

#define TYPE_PCI_E1000      "e1000"

PCIDevice *pci_nic_init_nofail(NICInfo *nd, PCIBus *rootbus,
                               const char *default_model,
                               const char *default_devaddr);

#endif
