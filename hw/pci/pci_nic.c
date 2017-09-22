/*
 * QEMU PCI network interface
 *
 * Copyright (c) 2004 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/net/pci.h"
#include "net/net.h"
#include "pci_internal.h"

static const char *const pci_nic_models[] = {
    "ne2k_pci",
    "i82551",
    "i82557b",
    "i82559er",
    "rtl8139",
    "e1000",
    "pcnet",
    "virtio",
    "sungem",
    NULL
};

static const char *const pci_nic_names[] = {
    "ne2k_pci",
    "i82551",
    "i82557b",
    "i82559er",
    "rtl8139",
    "e1000",
    "pcnet",
    "virtio-net-pci",
    "sungem",
    NULL
};

/* Initialize a PCI NIC.  */
PCIDevice *pci_nic_init_nofail(NICInfo *nd, PCIBus *rootbus,
                               const char *default_model,
                               const char *default_devaddr)
{
    const char *devaddr = nd->devaddr ? nd->devaddr : default_devaddr;
    PCIBus *bus;
    PCIDevice *pci_dev;
    DeviceState *dev;
    int devfn;
    int i;

    if (qemu_show_nic_models(nd->model, pci_nic_models)) {
        exit(0);
    }

    i = qemu_find_nic_model(nd, pci_nic_models, default_model);
    if (i < 0) {
        exit(1);
    }

    bus = pci_get_bus_devfn(&devfn, rootbus, devaddr);
    if (!bus) {
        error_report("Invalid PCI device address %s for device %s",
                     devaddr, pci_nic_names[i]);
        exit(1);
    }

    pci_dev = pci_create(bus, devfn, pci_nic_names[i]);
    dev = &pci_dev->qdev;
    qdev_set_nic_properties(dev, nd);
    qdev_init_nofail(dev);

    return pci_dev;
}
