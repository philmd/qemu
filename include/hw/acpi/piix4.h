/*
 * PIIX4 ACPI implementation
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_ACPI_PIIX4_H
#define HW_ACPI_PIIX4_H

#include "qom/object.h"
#include "hw/irq.h"

I2CBus *piix4_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
                      qemu_irq sci_irq, qemu_irq smi_irq,
                      int smm_enabled, DeviceState **piix4_pm);

Object *piix4_pm_find(void);

#endif
