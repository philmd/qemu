/*
 * QEMU ATmega MCU
 *
 * Copyright (c) 2019 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/boards.h" /* FIXME memory_region_allocate_system_memory for sram */
#include "hw/misc/unimp.h"
#include "atmega.h"

typedef struct {
    uint16_t addr;
    uint16_t prr_addr;
    uint8_t prr_bit;
    /* timer specific */
    uint16_t intmask_addr;
    uint16_t intflag_addr;
    bool is_timer16;
} peripheral_cfg;

typedef struct AtmegaMcuClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/
    const char *uc_name;
    const char *cpu_type;
    size_t flash_size;
    size_t eeprom_size;
    size_t sram_size;
    size_t io_size;
    size_t gpio_count;
    size_t adc_count;
    const uint8_t *irq;
    const peripheral_cfg *dev;
} AtmegaMcuClass;

#define ATMEGA_MCU_CLASS(klass) \
    OBJECT_CLASS_CHECK(AtmegaMcuClass, (klass), TYPE_ATMEGA_MCU)
#define ATMEGA_MCU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(AtmegaMcuClass, (obj), TYPE_ATMEGA_MCU)

enum AtmegaIrq {
    USART0_RXC_IRQ, USART0_DRE_IRQ, USART0_TXC_IRQ,
    USART1_RXC_IRQ, USART1_DRE_IRQ, USART1_TXC_IRQ,
    USART2_RXC_IRQ, USART2_DRE_IRQ, USART2_TXC_IRQ,
    USART3_RXC_IRQ, USART3_DRE_IRQ, USART3_TXC_IRQ,
    TIMER0_CAPT_IRQ, TIMER0_COMPA_IRQ, TIMER0_COMPB_IRQ,
        TIMER0_COMPC_IRQ, TIMER0_OVF_IRQ,
    TIMER1_CAPT_IRQ, TIMER1_COMPA_IRQ, TIMER1_COMPB_IRQ,
        TIMER1_COMPC_IRQ, TIMER1_OVF_IRQ,
    TIMER2_CAPT_IRQ, TIMER2_COMPA_IRQ, TIMER2_COMPB_IRQ,
        TIMER2_COMPC_IRQ, TIMER2_OVF_IRQ,
    TIMER3_CAPT_IRQ, TIMER3_COMPA_IRQ, TIMER3_COMPB_IRQ,
        TIMER3_COMPC_IRQ, TIMER3_OVF_IRQ,
    TIMER4_CAPT_IRQ, TIMER4_COMPA_IRQ, TIMER4_COMPB_IRQ,
        TIMER4_COMPC_IRQ, TIMER4_OVF_IRQ,
    TIMER5_CAPT_IRQ, TIMER5_COMPA_IRQ, TIMER5_COMPB_IRQ,
        TIMER5_COMPC_IRQ, TIMER5_OVF_IRQ,
    IRQ_COUNT
};

#define USART_IRQ_COUNT     3
#define USART_RXC_IRQ(n)    (n * USART_IRQ_COUNT + USART0_RXC_IRQ)
#define USART_DRE_IRQ(n)    (n * USART_IRQ_COUNT + USART0_DRE_IRQ)
#define USART_TXC_IRQ(n)    (n * USART_IRQ_COUNT + USART0_TXC_IRQ)
#define TIMER_IRQ_COUNT     5
#define TIMER_CAPT_IRQ(n)   (n * TIMER_IRQ_COUNT + TIMER0_CAPT_IRQ)
#define TIMER_COMPA_IRQ(n)  (n * TIMER_IRQ_COUNT + TIMER0_COMPA_IRQ)
#define TIMER_COMPB_IRQ(n)  (n * TIMER_IRQ_COUNT + TIMER0_COMPB_IRQ)
#define TIMER_COMPC_IRQ(n)  (n * TIMER_IRQ_COUNT + TIMER0_COMPC_IRQ)
#define TIMER_OVF_IRQ(n)    (n * TIMER_IRQ_COUNT + TIMER0_OVF_IRQ)

static const uint8_t irq168_328[IRQ_COUNT] = {
    [TIMER2_COMPA_IRQ]      = 8,
    [TIMER2_COMPB_IRQ]      = 9,
    [TIMER2_OVF_IRQ]        = 10,
    [TIMER1_CAPT_IRQ]       = 11,
    [TIMER1_COMPA_IRQ]      = 12,
    [TIMER1_COMPB_IRQ]      = 13,
    [TIMER1_OVF_IRQ]        = 14,
    [TIMER0_COMPA_IRQ]      = 15,
    [TIMER0_COMPB_IRQ]      = 16,
    [TIMER0_OVF_IRQ]        = 17,
    [USART0_RXC_IRQ]        = 19,
    [USART0_DRE_IRQ]        = 20,
    [USART0_TXC_IRQ]        = 21,
}, irq1280_2560[IRQ_COUNT] = {
    [TIMER2_COMPA_IRQ]      = 14,
    [TIMER2_COMPB_IRQ]      = 15,
    [TIMER2_OVF_IRQ]        = 16,
    [TIMER1_CAPT_IRQ]       = 17,
    [TIMER1_COMPA_IRQ]      = 18,
    [TIMER1_COMPB_IRQ]      = 19,
    [TIMER1_COMPC_IRQ]      = 20,
    [TIMER1_OVF_IRQ]        = 21,
    [TIMER0_COMPA_IRQ]      = 22,
    [TIMER0_COMPB_IRQ]      = 23,
    [TIMER0_OVF_IRQ]        = 24,
    [USART0_RXC_IRQ]        = 26,
    [USART0_DRE_IRQ]        = 27,
    [USART0_TXC_IRQ]        = 28,
    [TIMER3_CAPT_IRQ]       = 32,
    [TIMER3_COMPA_IRQ]      = 33,
    [TIMER3_COMPB_IRQ]      = 34,
    [TIMER3_COMPC_IRQ]      = 35,
    [TIMER3_OVF_IRQ]        = 36,
    [USART1_RXC_IRQ]        = 37,
    [USART1_DRE_IRQ]        = 38,
    [USART1_TXC_IRQ]        = 39,
    [TIMER4_CAPT_IRQ]       = 42,
    [TIMER4_COMPA_IRQ]      = 43,
    [TIMER4_COMPB_IRQ]      = 44,
    [TIMER4_COMPC_IRQ]      = 45,
    [TIMER4_OVF_IRQ]        = 46,
    [TIMER5_CAPT_IRQ]       = 47,
    [TIMER5_COMPA_IRQ]      = 48,
    [TIMER5_COMPB_IRQ]      = 49,
    [TIMER5_COMPC_IRQ]      = 50,
    [TIMER5_OVF_IRQ]        = 51,
    [USART2_RXC_IRQ]        = 52,
    [USART2_DRE_IRQ]        = 53,
    [USART2_TXC_IRQ]        = 54,
    [USART3_RXC_IRQ]        = 55,
    [USART3_DRE_IRQ]        = 56,
    [USART3_TXC_IRQ]        = 57,
};

enum AtmegaPeripheralAddress {
    POWER0, POWER1,
    GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF,
    GPIOG, GPIOH, GPIOI, GPIOJ, GPIOK, GPIOL,
    USART0, USART1, USART2, USART3,
    TIMER0, TIMER1, TIMER2, TIMER3, TIMER4, TIMER5,
    PERIFMAX
};

#define GPIO_ADDR(n)        (n + GPIOA)
#define USART_ADDR(n)       (n + USART0)
#define TIMER_ADDR(n)       (n + TIMER0)
#define POWER_ADDR(n)       (n + POWER0)

static const peripheral_cfg dev168_328[PERIFMAX] = {
    [USART0]        = {  0xc0, 0x64, 1 },
    [TIMER2]        = {  0xb0, 0x64, 6, 0x70, 0x37, false },
    [TIMER1]        = {  0x80, 0x64, 3, 0x6f, 0x36, true },
    [POWER0]        = {  0x64 },
    [TIMER0]        = {  0x44, 0x64, 5, 0x6e, 0x35, false },
    [GPIOD]         = {  0x29 },
    [GPIOC]         = {  0x26 },
    [GPIOB]         = {  0x23 },
}, dev1280_2560[PERIFMAX] = {
    [USART3]        = { 0x130, 0x65, 2 },
    [TIMER5]        = { 0x120, 0x65, 5, 0x73, 0x3a, true },
    [GPIOL]         = { 0x109 },
    [GPIOK]         = { 0x106 },
    [GPIOJ]         = { 0x103 },
    [GPIOH]         = { 0x100 },
    [USART2]        = {  0xd0, 0x65, 1 },
    [USART1]        = {  0xc8, 0x65, 0 },
    [USART0]        = {  0xc0, 0x64, 1 },
    [TIMER2]        = {  0xb0, 0x64, 6, 0x70, 0x37, false }, /* TODO async */
    [TIMER4]        = {  0xa0, 0x65, 4, 0x72, 0x39, true },
    [TIMER3]        = {  0x90, 0x65, 3, 0x71, 0x38, true },
    [TIMER1]        = {  0x80, 0x64, 3, 0x6f, 0x36, true },
    [POWER1]        = {  0x65 },
    [POWER0]        = {  0x64 },
    [TIMER0]        = {  0x44, 0x64, 5, 0x6e, 0x35, false },
    [GPIOG]         = {  0x32 },
    [GPIOF]         = {  0x2f },
    [GPIOE]         = {  0x2c },
    [GPIOD]         = {  0x29 },
    [GPIOC]         = {  0x26 },
    [GPIOB]         = {  0x23 },
    [GPIOA]         = {  0x20 },
};

static void connect_peripheral_irq(const AtmegaMcuClass *mc,
                                   SysBusDevice *sbd,
                                   DeviceState *dev, int n,
                                   unsigned periph_irq)
{
    int irq = mc->irq[periph_irq];

    if (!irq) {
        return;
    }
    /* FIXME move that to avr_cpu_set_int() once 'sample' board is removed */
    assert(irq >= 2);
    irq -= 2;

    sysbus_connect_irq(sbd, n, qdev_get_gpio_in(dev, irq));
}

static void connect_power_reduction_gpio(AtmegaMcuState *s,
                                         const AtmegaMcuClass *mc,
                                         DeviceState *dev, int index)
{
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pwr[mc->dev[index].prr_addr & 1]),
                       mc->dev[index].prr_bit, qdev_get_gpio_in(dev, 0));
}

static void atmega_realize(DeviceState *dev, Error **errp)
{
    AtmegaMcuState *s = ATMEGA_MCU(dev);
    const AtmegaMcuClass *mc = ATMEGA_MCU_GET_CLASS(dev);
    DeviceState *cpudev;
    SysBusDevice *sbd;
    Error *err = NULL;
    char *devname;
    size_t i;

    assert(mc->io_size <= 0x200);

    if (!s->xtal_freq_hz) {
        error_setg(errp, "\"xtal-frequency-hz\" property must be provided.");
        return;
    }

    /* CPU */
    object_initialize_child(OBJECT(dev), "cpu", &s->cpu, sizeof(s->cpu),
                            mc->cpu_type, &err, NULL);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    object_property_set_bool(OBJECT(&s->cpu), true, "realized", &error_abort);
    cpudev = DEVICE(&s->cpu);

    /* SRAM */
    memory_region_init_ram(&s->sram, OBJECT(dev), "sram", mc->sram_size,
                           &error_abort);
    memory_region_add_subregion(get_system_memory(),
                                OFFSET_DATA + 0x200, &s->sram);

    /* Flash */
    memory_region_init_rom(&s->flash, OBJECT(dev),
                           "flash", mc->flash_size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), OFFSET_CODE, &s->flash);

    /* I/O */
    s->io = qdev_create(NULL, TYPE_UNIMPLEMENTED_DEVICE);
    qdev_prop_set_string(s->io, "name", "I/O");
    qdev_prop_set_uint64(s->io, "size", mc->io_size);
    qdev_init_nofail(s->io);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(s->io), 0, OFFSET_DATA, -1234);

    /* Power Reduction */
    for (i = 0; i < POWER_MAX; i++) {
        int idx = POWER_ADDR(i);
        if (!mc->dev[idx].addr) {
            continue;
        }
        devname = g_strdup_printf("pwr%zu", i);
        object_initialize_child(OBJECT(dev), devname,
                                &s->pwr[i], sizeof(s->pwr[i]),
                                TYPE_AVR_MASK, &error_abort, NULL);
        object_property_set_bool(OBJECT(&s->pwr[i]), true, "realized",
                                 &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->pwr[i]), 0,
                        OFFSET_DATA + mc->dev[idx].addr);
        g_free(devname);
    }

    /* GPIO */
    for (i = 0; i < GPIO_MAX; i++) {
        int idx = GPIO_ADDR(i);
        if (!mc->dev[idx].addr) {
            continue;
        }
        devname = g_strdup_printf("avr-gpio-%c", 'a' + (char)i);
        create_unimplemented_device(devname,
                                    OFFSET_DATA + mc->dev[idx].addr, 3);
        g_free(devname);
    }

    /* USART */
    for (i = 0; i < USART_MAX; i++) {
        int idx = USART_ADDR(i);
        if (!mc->dev[idx].addr) {
            continue;
        }
        devname = g_strdup_printf("usart%zu", i);
        object_initialize_child(OBJECT(dev), devname,
                                &s->usart[i], sizeof(s->usart[i]),
                                TYPE_AVR_USART, &error_abort, NULL);
        qdev_prop_set_chr(DEVICE(&s->usart[i]), "chardev", serial_hd(i));
        object_property_set_bool(OBJECT(&s->usart[i]), true, "realized",
                                 &error_abort);
        sbd = SYS_BUS_DEVICE(&s->usart[i]);
        sysbus_mmio_map(sbd, 0, OFFSET_DATA + mc->dev[USART_ADDR(i)].addr);
        connect_peripheral_irq(mc, sbd, cpudev, 0, USART_RXC_IRQ(i));
        connect_peripheral_irq(mc, sbd, cpudev, 1, USART_DRE_IRQ(i));
        connect_peripheral_irq(mc, sbd, cpudev, 2, USART_TXC_IRQ(i));
        connect_power_reduction_gpio(s, mc, DEVICE(&s->usart[i]), idx);
        g_free(devname);
    }

    /* Timer */
    for (i = 0; i < TIMER_MAX; i++) {
        int idx = TIMER_ADDR(i);
        if (!mc->dev[idx].addr) {
            continue;
        }
        if (!mc->dev[idx].is_timer16) {
            create_unimplemented_device("avr-timer8",
                                        OFFSET_DATA + mc->dev[idx].addr, 5);
            create_unimplemented_device("avr-timer8-intmask",
                                        OFFSET_DATA
                                        + mc->dev[idx].intmask_addr, 1);
            create_unimplemented_device("avr-timer8-intflag",
                                        OFFSET_DATA
                                        + mc->dev[idx].intflag_addr, 1);
            continue;
        }
        devname = g_strdup_printf("timer%zu", i);
        object_initialize_child(OBJECT(dev), devname,
                                &s->timer[i], sizeof(s->timer[i]),
                                TYPE_AVR_TIMER16, &error_abort, NULL);
        object_property_set_uint(OBJECT(&s->timer[i]), s->xtal_freq_hz,
                                 "cpu-frequency-hz", &error_abort);
        object_property_set_bool(OBJECT(&s->timer[i]), true, "realized",
                                 &error_abort);
        sbd = SYS_BUS_DEVICE(&s->timer[i]);
        sysbus_mmio_map(sbd, 0, OFFSET_DATA + mc->dev[idx].addr);
        sysbus_mmio_map(sbd, 1, OFFSET_DATA + mc->dev[idx].intmask_addr);
        sysbus_mmio_map(sbd, 2, OFFSET_DATA + mc->dev[idx].intflag_addr);
        connect_peripheral_irq(mc, sbd, cpudev, 0, TIMER_CAPT_IRQ(i));
        connect_peripheral_irq(mc, sbd, cpudev, 1, TIMER_COMPA_IRQ(i));
        connect_peripheral_irq(mc, sbd, cpudev, 2, TIMER_COMPB_IRQ(i));
        connect_peripheral_irq(mc, sbd, cpudev, 3, TIMER_COMPC_IRQ(i));
        connect_peripheral_irq(mc, sbd, cpudev, 4, TIMER_OVF_IRQ(i));
        connect_power_reduction_gpio(s, mc, DEVICE(&s->timer[i]), idx);
        g_free(devname);
    }

    create_unimplemented_device("avr-twi",          OFFSET_DATA + 0x0b8, 6);
    create_unimplemented_device("avr-adc",          OFFSET_DATA + 0x078, 8);
    create_unimplemented_device("avr-ext-mem-ctrl", OFFSET_DATA + 0x074, 2);
    create_unimplemented_device("avr-watchdog",     OFFSET_DATA + 0x060, 1);
    create_unimplemented_device("avr-spi",          OFFSET_DATA + 0x04c, 3);
    create_unimplemented_device("avr-eeprom",       OFFSET_DATA + 0x03f, 3);
}

static Property atmega_props[] = {
    DEFINE_PROP_UINT64("xtal-frequency-hz", AtmegaMcuState,
                       xtal_freq_hz, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void atmega_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = atmega_realize;
    dc->props = atmega_props;
    /* Reason: Mapped at fixed location on the system bus */
    dc->user_creatable = false;
}

static void atmega168_class_init(ObjectClass *oc, void *data)
{
    AtmegaMcuClass *amc = ATMEGA_MCU_CLASS(oc);

    amc->cpu_type = AVR_CPU_TYPE_NAME("avr5");
    amc->flash_size = 16 * KiB;
    amc->eeprom_size = 512;
    amc->sram_size = 1 * KiB;
    amc->io_size = 256;
    amc->gpio_count = 23;
    amc->adc_count = 6;
    amc->irq = irq168_328;
    amc->dev = dev168_328;
};

static void atmega328_class_init(ObjectClass *oc, void *data)
{
    AtmegaMcuClass *amc = ATMEGA_MCU_CLASS(oc);

    amc->cpu_type = AVR_CPU_TYPE_NAME("avr5");
    amc->flash_size = 32 * KiB;
    amc->eeprom_size = 1 * KiB;
    amc->sram_size = 2 * KiB;
    amc->io_size = 256;
    amc->gpio_count = 23;
    amc->adc_count = 6;
    amc->irq = irq168_328;
    amc->dev = dev168_328;
};

static void atmega1280_class_init(ObjectClass *oc, void *data)
{
    AtmegaMcuClass *amc = ATMEGA_MCU_CLASS(oc);

    amc->cpu_type = AVR_CPU_TYPE_NAME("avr6");
    amc->flash_size = 128 * KiB;
    amc->eeprom_size = 4 * KiB;
    amc->sram_size = 8 * KiB;
    amc->io_size = 512;
    amc->gpio_count = 86;
    amc->adc_count = 16;
    amc->irq = irq1280_2560;
    amc->dev = dev1280_2560;
};

static void atmega2560_class_init(ObjectClass *oc, void *data)
{
    AtmegaMcuClass *amc = ATMEGA_MCU_CLASS(oc);

    amc->cpu_type = AVR_CPU_TYPE_NAME("avr6");
    amc->flash_size = 256 * KiB;
    amc->eeprom_size = 4 * KiB;
    amc->sram_size = 8 * KiB;
    amc->io_size = 512;
    amc->gpio_count = 54;
    amc->adc_count = 16;
    amc->irq = irq1280_2560;
    amc->dev = dev1280_2560;
};

static const TypeInfo atmega_mcu_types[] = {
    {
        .name           = TYPE_ATMEGA168_MCU,
        .parent         = TYPE_ATMEGA_MCU,
        .class_init     = atmega168_class_init,
    }, {
        .name           = TYPE_ATMEGA328_MCU,
        .parent         = TYPE_ATMEGA_MCU,
        .class_init     = atmega328_class_init,
    }, {
        .name           = TYPE_ATMEGA1280_MCU,
        .parent         = TYPE_ATMEGA_MCU,
        .class_init     = atmega1280_class_init,
    }, {
        .name           = TYPE_ATMEGA2560_MCU,
        .parent         = TYPE_ATMEGA_MCU,
        .class_init     = atmega2560_class_init,
    }, {
        .name           = TYPE_ATMEGA_MCU,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(AtmegaMcuState),
        .class_size     = sizeof(AtmegaMcuClass),
        .class_init     = atmega_class_init,
        .abstract       = true,
    }
};

DEFINE_TYPES(atmega_mcu_types)
