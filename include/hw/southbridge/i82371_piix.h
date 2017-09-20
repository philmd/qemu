/*
 * Intel 82371 PIIX South Bridge Emulation
 *
 * Copyright (c) 2018 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef HW_ISA_PIIX_H
#define HW_ISA_PIIX_H

#include "hw/pci/pci.h"
#include "hw/isa/isa.h"

#define TYPE_PIIX4_PCI_DEVICE "PIIX4"

#define PIIX_NUM_PIRQS          4ULL    /* PIRQ[A-D] */

/*
 * Reset Control Register: PCI-accessible ISA-Compatible Register at address
 * 0xcf9, provided by the PCI/ISA bridge (PIIX3 PCI function 0, 8086:7000).
 */
#define RCR_IOPORT 0xcf9

/* piix4.c */
extern PCIDevice *piix4_dev;

/* acpi_piix.c */
Object *piix4_pm_find(void);
I2CBus *piix4_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
                      qemu_irq sci_irq, qemu_irq smi_irq,
                      int smm_enabled, DeviceState **piix4_pm);

#endif /* HW_ISA_PIIX_H */
