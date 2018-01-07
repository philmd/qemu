/*
 * QEMU PIIX South Bridge Emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/range.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/southbridge/i82371_piix.h"
#include "hw/dma/i8257.h"
#include "hw/audio/pcspk.h"
#include "hw/timer/i8254.h"
#include "hw/isa/superio.h"
#include "hw/xen/xen.h"

#define PIIX_NUM_PIC_IRQS       16      /* i8259 * 2 */
#define XEN_PIIX_NUM_PIRQS      128ULL
#define PIIX_PIRQC              0x60

typedef struct PIIXState {
    /*< private >*/
    PCIDevice dev;
    /*< public >*/

    ISABus *isa_bus;

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

    qemu_irq cpu_intr;
    qemu_irq *pic;

    /* This member isn't used. Just for save/load compatibility */
    int32_t pci_irq_levels_vmstate[PIIX_NUM_PIRQS];

    /* Reset Control Register contents */
    uint8_t rcr;
    /* IO memory region for Reset Control Register (RCR_IOPORT) */
    MemoryRegion rcr_mem;
} PIIXState;

static void piix_rcr_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned int len)
{
    PIIXState *s = opaque;

    if (val & 4) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return;
    }
    s->rcr = val & 2; /* keep System Reset type only */
}

static uint64_t piix_rcr_read(void *opaque, hwaddr addr, unsigned int len)
{
    PIIXState *s = opaque;
    return s->rcr;
}

static const MemoryRegionOps rcr_ops = {
    .read = piix_rcr_read,
    .write = piix_rcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void piix_rcr_init(PIIXState *s, MemoryRegion *io, const char *regname)
{
    memory_region_init_io(&s->rcr_mem, OBJECT(s), &rcr_ops, s, regname, 1);
    memory_region_add_subregion_overlap(io, RCR_IOPORT, &s->rcr_mem, 1);
}

/* return the global irq number corresponding to a given device irq
   pin. We could also use the bus number to have a more precise
   mapping. */
static int pci_slot_get_pirq(PCIDevice *pci_dev, int pci_intx)
{
    int slot_addend;
    slot_addend = (pci_dev->devfn >> 3) - 1;
    return (pci_intx + slot_addend) & 3;
}

static void piix_realize(PCIDevice *pci_dev, PIIXState *s, Error **errp)
{
    MemoryRegion *pci_io = pci_address_space_io(pci_dev);

    s->isa_bus = isa_bus_new(DEVICE(s), pci_address_space(pci_dev),
                             pci_io, errp);
    if (!s->isa_bus) {
        if (!errp) {
            error_setg(errp, "can not create ISA bus");
        }
        return;
    }

    piix_rcr_init(s, pci_io, "reset-control");
}

typedef struct PIIXState PIIX3State;

#define TYPE_PIIX3_PCI_DEVICE "pci-piix3"
#define PIIX3_PCI_DEVICE(obj) \
    OBJECT_CHECK(PIIX3State, (obj), TYPE_PIIX3_PCI_DEVICE)

/* PIIX3 PCI to ISA bridge */
static void piix3_set_irq_pic(PIIX3State *piix3, int pic_irq)
{
    qemu_set_irq(piix3->pic[pic_irq],
                 !!(piix3->pic_levels &
                    (((1ULL << PIIX_NUM_PIRQS) - 1) <<
                     (pic_irq * PIIX_NUM_PIRQS))));
}

static void piix3_set_irq_level_internal(PIIX3State *piix3, int pirq, int level)
{
    int pic_irq;
    uint64_t mask;

    pic_irq = piix3->dev.config[PIIX_PIRQC + pirq];
    if (pic_irq >= PIIX_NUM_PIC_IRQS) {
        return;
    }

    mask = 1ULL << ((pic_irq * PIIX_NUM_PIRQS) + pirq);
    piix3->pic_levels &= ~mask;
    piix3->pic_levels |= mask * !!level;
}

static void piix3_set_irq_level(PIIX3State *piix3, int pirq, int level)
{
    int pic_irq;

    pic_irq = piix3->dev.config[PIIX_PIRQC + pirq];
    if (pic_irq >= PIIX_NUM_PIC_IRQS) {
        return;
    }

    piix3_set_irq_level_internal(piix3, pirq, level);

    piix3_set_irq_pic(piix3, pic_irq);
}

static void piix3_set_irq(void *opaque, int pirq, int level)
{
    PIIX3State *piix3 = opaque;
    piix3_set_irq_level(piix3, pirq, level);
}

static PCIINTxRoute piix3_route_intx_pin_to_irq(void *opaque, int pin)
{
    PIIX3State *piix3 = opaque;
    int irq = piix3->dev.config[PIIX_PIRQC + pin];
    PCIINTxRoute route;

    if (irq < PIIX_NUM_PIC_IRQS) {
        route.mode = PCI_INTX_ENABLED;
        route.irq = irq;
    } else {
        route.mode = PCI_INTX_DISABLED;
        route.irq = -1;
    }
    return route;
}

PCIDevice *piix3_init(PCIBus *bus, ISABus **isa_bus,
                      qemu_irq *pic, int *piix3_devfn)
{
    PCIDevice *pci_dev;
    PIIX3State *piix3;

    /* Xen supports additional interrupt routes from the PCI devices to
     * the IOAPIC: the four pins of each PCI device on the bus are also
     * connected to the IOAPIC directly.
     * These additional routes can be discovered through ACPI. */
    if (xen_enabled()) {
        pci_dev = pci_create_simple_multifunction(bus,
                             -1, true, "PIIX3-xen");
        piix3 = PIIX3_PCI_DEVICE(pci_dev);
        pci_bus_irqs(bus, xen_piix3_set_irq, xen_pci_slot_get_pirq,
                piix3, XEN_PIIX_NUM_PIRQS);
    } else {
        pci_dev = pci_create_simple_multifunction(bus,
                             -1, true, "PIIX3");
        piix3 = PIIX3_PCI_DEVICE(pci_dev);
        pci_bus_irqs(bus, piix3_set_irq, pci_slot_get_pirq, piix3,
                PIIX_NUM_PIRQS);
        pci_bus_set_route_irq_fn(bus, piix3_route_intx_pin_to_irq);
    }
    piix3->pic = pic;
    *isa_bus = ISA_BUS(qdev_get_child_bus(DEVICE(piix3), "isa.0"));

    *piix3_devfn = piix3->dev.devfn;

    return pci_dev;
}

/* irq routing is changed. so rebuild bitmap */
static void piix3_update_irq_levels(PIIX3State *piix3)
{
    int pirq;

    piix3->pic_levels = 0;
    for (pirq = 0; pirq < PIIX_NUM_PIRQS; pirq++) {
        piix3_set_irq_level(piix3, pirq,
                            pci_bus_get_irq_level(piix3->dev.bus, pirq));
    }
}

static void piix3_write_config(PCIDevice *dev,
                               uint32_t address, uint32_t val, int len)
{
    pci_default_write_config(dev, address, val, len);
    if (ranges_overlap(address, len, PIIX_PIRQC, 4)) {
        PIIX3State *piix3 = PIIX3_PCI_DEVICE(dev);
        int pic_irq;

        pci_bus_fire_intx_routing_notifier(piix3->dev.bus);
        piix3_update_irq_levels(piix3);
        for (pic_irq = 0; pic_irq < PIIX_NUM_PIC_IRQS; pic_irq++) {
            piix3_set_irq_pic(piix3, pic_irq);
        }
    }
}

static void piix3_write_config_xen(PCIDevice *dev,
                               uint32_t address, uint32_t val, int len)
{
    xen_piix_pci_write_config_client(address, val, len);
    piix3_write_config(dev, address, val, len);
}

static void piix3_reset(DeviceState *dev)
{
    PIIX3State *s = PIIX3_PCI_DEVICE(dev);
    uint8_t *pci_conf = s->dev.config;

    pci_conf[0x04] = 0x07; /* master, memory and I/O */
    pci_conf[0x05] = 0x00;
    pci_conf[0x06] = 0x00;
    pci_conf[0x07] = 0x02; /* PCI_status_devsel_medium */
    pci_conf[0x4c] = 0x4d;
    pci_conf[0x4e] = 0x03;
    pci_conf[0x4f] = 0x00;
    pci_conf[0x60] = 0x80;
    pci_conf[0x61] = 0x80;
    pci_conf[0x62] = 0x80;
    pci_conf[0x63] = 0x80;
    pci_conf[0x69] = 0x02;
    pci_conf[0x70] = 0x80;
    pci_conf[0x76] = 0x0c;
    pci_conf[0x77] = 0x0c;
    pci_conf[0x78] = 0x02;
    pci_conf[0x79] = 0x00;
    pci_conf[0x80] = 0x00;
    pci_conf[0x82] = 0x00;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa2] = 0x00;
    pci_conf[0xa3] = 0x00;
    pci_conf[0xa4] = 0x00;
    pci_conf[0xa5] = 0x00;
    pci_conf[0xa6] = 0x00;
    pci_conf[0xa7] = 0x00;
    pci_conf[0xa8] = 0x0f;
    pci_conf[0xaa] = 0x00;
    pci_conf[0xab] = 0x00;
    pci_conf[0xac] = 0x00;
    pci_conf[0xae] = 0x00;

    s->pic_levels = 0;
    s->rcr = 0;
}

static int piix3_post_load(void *opaque, int version_id)
{
    PIIX3State *piix3 = opaque;
    int pirq;

    /* Because the i8259 has not been deserialized yet, qemu_irq_raise
     * might bring the system to a different state than the saved one;
     * for example, the interrupt could be masked but the i8259 would
     * not know that yet and would trigger an interrupt in the CPU.
     *
     * Here, we update irq levels without raising the interrupt.
     * Interrupt state will be deserialized separately through the i8259.
     */
    piix3->pic_levels = 0;
    for (pirq = 0; pirq < PIIX_NUM_PIRQS; pirq++) {
        piix3_set_irq_level_internal(piix3, pirq,
                            pci_bus_get_irq_level(piix3->dev.bus, pirq));
    }
    return 0;
}

static int piix3_pre_save(void *opaque)
{
    int i;
    PIIX3State *piix3 = opaque;

    for (i = 0; i < ARRAY_SIZE(piix3->pci_irq_levels_vmstate); i++) {
        piix3->pci_irq_levels_vmstate[i] =
            pci_bus_get_irq_level(piix3->dev.bus, i);
    }

    return 0;
}

static bool piix3_rcr_needed(void *opaque)
{
    PIIX3State *piix3 = opaque;

    return (piix3->rcr != 0);
}

static const VMStateDescription vmstate_piix3_rcr = {
    .name = "PIIX3/rcr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = piix3_rcr_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(rcr, PIIX3State),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_piix3 = {
    .name = "PIIX3",
    .version_id = 3,
    .minimum_version_id = 2,
    .post_load = piix3_post_load,
    .pre_save = piix3_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PIIX3State),
        VMSTATE_INT32_ARRAY_V(pci_irq_levels_vmstate, PIIX3State,
                              PIIX_NUM_PIRQS, 3),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_piix3_rcr,
        NULL
    }
};

static void piix3_realize(PCIDevice *pci_dev, Error **errp)
{
    piix_realize(pci_dev, PIIX3_PCI_DEVICE(pci_dev), errp);
}

static void pci_piix3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    dc->desc        = "ISA bridge";
    dc->vmsd        = &vmstate_piix3;
    dc->hotpluggable   = false;
    dc->reset       = piix3_reset;
    k->realize      = piix3_realize;
    k->vendor_id    = PCI_VENDOR_ID_INTEL;
    /* 82371SB PIIX3 PCI-to-ISA bridge (Step A1) */
    k->device_id    = PCI_DEVICE_ID_INTEL_82371SB_0;
    k->class_id     = PCI_CLASS_BRIDGE_ISA;
    /*
     * Reason: part of PIIX3 southbridge, needs to be wired up by
     * pc_piix.c's pc_init1()
     */
    dc->user_creatable = false;
}

static const TypeInfo piix3_pci_type_info = {
    .name = TYPE_PIIX3_PCI_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PIIX3State),
    .abstract = true,
    .class_init = pci_piix3_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void piix3_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_write = piix3_write_config;
}

static const TypeInfo piix3_info = {
    .name          = "PIIX3",
    .parent        = TYPE_PIIX3_PCI_DEVICE,
    .class_init    = piix3_class_init,
};

static void piix3_xen_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_write = piix3_write_config_xen;
};

static const TypeInfo piix3_xen_info = {
    .name          = "PIIX3-xen",
    .parent        = TYPE_PIIX3_PCI_DEVICE,
    .class_init    = piix3_xen_class_init,
};

typedef struct PIIXState PIIX4State;

#define TYPE_PIIX4_PCI_DEVICE "PIIX4"
#define PIIX4_PCI_DEVICE(obj) \
    OBJECT_CHECK(PIIX4State, (obj), TYPE_PIIX4_PCI_DEVICE)

static const VMStateDescription vmstate_piix4 = {
    .name = "PIIX4",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PIIX4State),
        VMSTATE_END_OF_LIST()
    }
};

static void piix4_request_i8259_irq(void *opaque, int irq, int level)
{
    PIIX4State *s = opaque;
    qemu_set_irq(s->cpu_intr, level);
}

static void piix4_set_i8259_irq(void *opaque, int irq, int level)
{
    PIIX4State *s = opaque;
    qemu_set_irq(s->pic[irq], level);
}

static void piix4_reset(DeviceState *dev)
{
    PIIX4State *s = PIIX4_PCI_DEVICE(dev);
    uint8_t *pci_conf = s->dev.config;

    pci_conf[0x04] = 0x07; /* master, memory and I/O */
    pci_conf[0x05] = 0x00;
    pci_conf[0x06] = 0x00;
    pci_conf[0x07] = 0x02; /* PCI_status_devsel_medium */
    pci_conf[0x4c] = 0x4d;
    pci_conf[0x4e] = 0x03;
    pci_conf[0x4f] = 0x00;
    pci_conf[0x60] = 0x0a; /* PCI A -> IRQ 10 */
    pci_conf[0x61] = 0x0a; /* PCI B -> IRQ 10 */
    pci_conf[0x62] = 0x0b; /* PCI C -> IRQ 11 */
    pci_conf[0x63] = 0x0b; /* PCI D -> IRQ 11 */
    pci_conf[0x69] = 0x02;
    pci_conf[0x70] = 0x80;
    pci_conf[0x76] = 0x0c;
    pci_conf[0x77] = 0x0c;
    pci_conf[0x78] = 0x02;
    pci_conf[0x79] = 0x00;
    pci_conf[0x80] = 0x00;
    pci_conf[0x82] = 0x00;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa2] = 0x00;
    pci_conf[0xa3] = 0x00;
    pci_conf[0xa4] = 0x00;
    pci_conf[0xa5] = 0x00;
    pci_conf[0xa6] = 0x00;
    pci_conf[0xa7] = 0x00;
    pci_conf[0xa8] = 0x0f;
    pci_conf[0xaa] = 0x00;
    pci_conf[0xab] = 0x00;
    pci_conf[0xac] = 0x00;
    pci_conf[0xae] = 0x00;
}

PCIDevice *piix4_dev;

static void piix4_realize(PCIDevice *pci_dev, Error **errp)
{
    DeviceState *dev = DEVICE(pci_dev);
    PIIX4State *s = DO_UPCAST(PIIX4State, dev, pci_dev);
    ISADevice *pit;
    qemu_irq *i8259_out_irq;

    piix_realize(pci_dev, s, errp);
    if (errp) {
        return;
    }

    qdev_init_gpio_in_named(dev, piix4_set_i8259_irq, "isa", ISA_NUM_IRQS);
    qdev_init_gpio_out_named(dev, &s->cpu_intr, "intr", 1);

    /* initialize i8259 pic */
    i8259_out_irq = qemu_allocate_irqs(piix4_request_i8259_irq, s, 1);
    s->pic = i8259_init(s->isa_bus, *i8259_out_irq);

    /* initialize ISA irqs */
    isa_bus_irqs(s->isa_bus, s->pic);

    /* initialize pit */
    pit = i8254_pit_init(s->isa_bus, 0x40, 0, NULL);

    /* speaker */
    pcspk_init(s->isa_bus, pit);

    /* DMA */
    i8257_dma_init(s->isa_bus, 0);

    isa_superio_init(s->isa_bus, 2, 1, 1);

    piix4_dev = pci_dev;
}

static void piix4_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = piix4_realize;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82371AB_0;
    k->class_id = PCI_CLASS_BRIDGE_ISA;
    dc->reset = piix4_reset;
    dc->desc = "ISA bridge";
    dc->vmsd = &vmstate_piix4;
    /*
     * Reason: part of PIIX4 southbridge, needs to be wired up,
     * e.g. by mips_malta_init()
     */
    dc->user_creatable = false;
    dc->hotpluggable = false;
}

static const TypeInfo piix4_info = {
    .name          = TYPE_PIIX4_PCI_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PIIX4State),
    .class_init    = piix4_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void piix_register_types(void)
{
    type_register_static(&piix3_pci_type_info);
    type_register_static(&piix3_info);
    type_register_static(&piix3_xen_info);
    type_register_static(&piix4_info);
}

type_init(piix_register_types)
