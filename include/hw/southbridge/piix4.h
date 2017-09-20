#ifndef HW_SOUTHBRIDGE_PIIX4_H
#define HW_SOUTHBRIDGE_PIIX4_H

#include "hw/irq.h"
#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/i2c/i2c.h"

#define TYPE_PIIX4_PCI_DEVICE "PIIX4"

extern PCIDevice *piix4_dev;

/* piix4.c */
int piix4_init(PCIBus *bus, ISABus **isa_bus, int devfn);

/* acpi_piix.c */
I2CBus *piix4_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
                      qemu_irq sci_irq, qemu_irq smi_irq,
                      int smm_enabled, DeviceState **piix4_pm);

Object *piix4_pm_find(void);

#endif
