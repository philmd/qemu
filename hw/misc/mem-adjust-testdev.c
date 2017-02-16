/*
 * QEMU test device to support memory::access_with_adjusted_size() unit-tests
 *
 * Copyright (c) 2017 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/qdev.h"
#include "exec/address-spaces.h"

#define IMINBIT_MAX 4
#define IMAXBIT_MAX 4

#define IOMEM_COUNT (2 * IMINBIT_MAX * IMAXBIT_MAX)
#define IOMEM_SIZE 0x00000020

typedef union {
    uint32_t addr;
    struct {
#ifdef HOST_WORDS_BIGENDIAN
        uint8_t :4, endianness:4, vmin:4, vmax:4, imin:4, imax:4, data:8;
#else
        uint8_t data:8, imax:4, imin:4, vmax:4, vmin:4, endianness:4, :4;
#endif
    } access_size;
} iobase_t;

typedef struct MmioTestState {
    /*< private >*/
    DeviceState parent;

    /*< public >*/
    MemoryRegion iomem[IOMEM_COUNT];
    int iocnt;

    union {
        char data[IOMEM_SIZE];
        uint8_t   u8[IOMEM_SIZE / sizeof(uint8_t)];
        uint16_t u16[IOMEM_SIZE / sizeof(uint16_t)];
        uint32_t u32[IOMEM_SIZE / sizeof(uint32_t)];
        uint64_t u64[IOMEM_SIZE / sizeof(uint64_t)];
    } buf;
} MmioTestState;


#define TYPE_MMIO_TEST_DEV "mem-adjust-testdev"
#define MMIO_TEST_DEV(obj) \
        OBJECT_CHECK(MmioTestState, (obj), TYPE_MMIO_TEST_DEV)

static uint64_t memadj_testdev_read(void *opaque, hwaddr offset, unsigned size)
{
    MmioTestState *s = MMIO_TEST_DEV(opaque);
    uint64_t value;

    offset /= size;
    switch (size) {
    case 1:
        value = s->buf.u8[offset];
        break;
    case 2:
        value = s->buf.u16[offset];
        break;
    case 4:
        value = s->buf.u32[offset];
        break;
    case 8:
        value = s->buf.u64[offset];
        break;
    }

    return value;
}

static void memadj_testdev_write(void *opaque, hwaddr offset, uint64_t value,
                unsigned size)
{
    MmioTestState *s = MMIO_TEST_DEV(opaque);

    offset /= size;
    switch (size) {
    case 1:
        s->buf.u8[offset] = value;
        break;
    case 2:
        s->buf.u16[offset] = value;
        break;
    case 4:
        s->buf.u32[offset] = value;
        break;
    case 8:
        s->buf.u64[offset] = value;
        break;
    }
}

static void memadj_testdev_add_region(MmioTestState *s, MemoryRegionOps *ops)
{
    static const char endian_ch[] = {
        [DEVICE_LITTLE_ENDIAN] = 'l',
        [DEVICE_BIG_ENDIAN] = 'b',
        [DEVICE_NATIVE_ENDIAN] = 'n',
    };
    char *name;
    iobase_t base = { .addr = 0 };
    int idx = s->iocnt;

    assert(s->iocnt < ARRAY_SIZE(s->iomem));
    s->iocnt++;

    base.access_size.vmin = ops->valid.min_access_size;
    base.access_size.vmax = ops->valid.max_access_size;
    base.access_size.imin = ops->impl.min_access_size;
    base.access_size.imax = ops->impl.max_access_size;
    base.access_size.endianness = ops->endianness == DEVICE_BIG_ENDIAN;

    name = g_strdup_printf("memadj_testdev_%ce_v%u%u_i%u%u",
        endian_ch[ops->endianness],
        ops->valid.min_access_size, ops->valid.max_access_size,
        ops->impl.min_access_size, ops->impl.max_access_size);

    memory_region_init_io(&s->iomem[idx], OBJECT(s), ops, s,
                        name, IOMEM_SIZE);
    memory_region_add_subregion(get_system_memory(), base.addr, &s->iomem[idx]);
    g_free(name);
}

static void memadj_testdev_realizefn(DeviceState *dev, Error **errp)
{
    static const int endianness_tested[] = {
        DEVICE_LITTLE_ENDIAN,
        DEVICE_BIG_ENDIAN
    };
    MmioTestState *s = MMIO_TEST_DEV(dev);
    MemoryRegionOps *ops;
    int imin_bit, imax_bit, e;

    for (size_t i = 0; i < IOMEM_SIZE; i++) {
        s->buf.u8[i] = 0x11 * i;
    }

    for (e = 0; e < ARRAY_SIZE(endianness_tested); e++) {
        for (imin_bit = 0; imin_bit < IMINBIT_MAX; imin_bit++) {
            for (imax_bit = imin_bit; imax_bit < IMAXBIT_MAX; imax_bit++) {
                ops = g_malloc0(sizeof(MemoryRegionOps));
                ops->endianness = endianness_tested[e];
                ops->read = memadj_testdev_read;
                ops->write = memadj_testdev_write;
                ops->valid.min_access_size = 1;
                ops->valid.max_access_size = 8;
                ops->impl.min_access_size = 1 << imin_bit;
                ops->impl.max_access_size = 1 << imax_bit;
                memadj_testdev_add_region(s, ops);
            }
        }
    }
}

static void memadj_testdev_unrealizefn(DeviceState *dev, Error **errp)
{
    MmioTestState *s = MMIO_TEST_DEV(dev);
    int i;

    for (i = 0; i < s->iocnt; i++) {
        g_free(&s->iomem[i].ops);
    }
}

static void memadj_testdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->realize = memadj_testdev_realizefn;
    dc->unrealize = memadj_testdev_unrealizefn;
}

static const TypeInfo memadj_testdev_info = {
    .name           = TYPE_MMIO_TEST_DEV,
    .parent         = TYPE_DEVICE,
    .instance_size  = sizeof(MmioTestState),
    .class_init     = memadj_testdev_class_init,
};

static void memadj_testdev_register_types(void)
{
    type_register_static(&memadj_testdev_info);
}

type_init(memadj_testdev_register_types);
