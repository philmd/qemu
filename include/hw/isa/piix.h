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

#include "hw/hw.h"
#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/i2c/smbus.h"

#define PIIX_NUM_PIC_IRQS       16      /* i8259 * 2 */
#define PIIX_NUM_PIRQS          4ULL    /* PIRQ[A-D] */
#define XEN_PIIX_NUM_PIRQS      128ULL
#define PIIX_PIRQC              0x60

typedef struct PIIX3State {
    PCIDevice dev;

    /*
     * bitmap to track pic levels.
     * The pic level is the logical OR of all the PCI irqs mapped to it
     * So one PIC level is tracked by PIIX_NUM_PIRQS bits.
     *
     * PIRQ is mapped to PIC pins, we track it by
     * PIIX_NUM_PIRQS * PIIX_NUM_PIC_IRQS = 64 bits with
     * pic_irq * PIIX_NUM_PIRQS + pirq
     */
#if PIIX_NUM_PIC_IRQS * PIIX_NUM_PIRQS > 64
#error "unable to encode pic state in 64bit in pic_levels."
#endif
    uint64_t pic_levels;

    qemu_irq *pic;

    /* This member isn't used. Just for save/load compatibility */
    int32_t pci_irq_levels_vmstate[PIIX_NUM_PIRQS];

    /* Reset Control Register contents */
    uint8_t rcr;

    /* IO memory region for Reset Control Register (RCR_IOPORT) */
    MemoryRegion rcr_mem;
} PIIX3State;

#define TYPE_PIIX3_PCI_DEVICE "pci-piix3"
#define PIIX3_PCI_DEVICE(obj) \
    OBJECT_CHECK(PIIX3State, (obj), TYPE_PIIX3_PCI_DEVICE)

#define TYPE_PIIX3_DEVICE "PIIX3"
#define TYPE_PIIX3_XEN_DEVICE "PIIX3-xen"

/*
 * Reset Control Register: PCI-accessible ISA-Compatible Register at address
 * 0xcf9, provided by the PCI/ISA bridge (PIIX3 PCI function 0, 8086:7000).
 */
#define RCR_IOPORT 0xcf9

void piix3_set_irq(void *opaque, int pirq, int level);
PCIINTxRoute piix3_route_intx_pin_to_irq(void *opaque, int pci_intx);
void piix3_write_config_xen(PCIDevice *dev,
                            uint32_t address, uint32_t val, int len);

extern PCIDevice *piix4_dev;

DeviceState *piix4_create(PCIBus *pci_bus, ISABus **isa_bus, I2CBus **smbus,
                          size_t ide_buses);

#endif
