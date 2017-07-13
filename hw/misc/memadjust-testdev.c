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

#define ENDIAN_COUNT 2
#define VMINBIT_COUNT 4
#define VMAXBIT_COUNT 4
#define IMINBIT_COUNT 4
#define IMAXBIT_COUNT 4

#define IOMEM_COUNT (ENDIAN_COUNT * \
            VMINBIT_COUNT * VMAXBIT_COUNT * \
            IMINBIT_COUNT * IMAXBIT_COUNT)
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
    uint64_t base;

    union {
        char data[IOMEM_SIZE];
        uint8_t   u8[IOMEM_SIZE / sizeof(uint8_t)];
        uint16_t u16[IOMEM_SIZE / sizeof(uint16_t)]; //x
        uint32_t u32[IOMEM_SIZE / sizeof(uint32_t)]; //x
        uint64_t u64[IOMEM_SIZE / sizeof(uint64_t)]; //x
    } buf;
} MmioTestState;


#define TYPE_MMIO_TEST_DEV "memadjust-testdev"
#define MMIO_TEST_DEV(obj) \
        OBJECT_CHECK(MmioTestState, (obj), TYPE_MMIO_TEST_DEV)

static void dump_ram(void *ptr, size_t len, const char *desc)
{
    int i;
    uint8_t *p = (uint8_t *)ptr;

    fputs("RAM: ", stderr);
    for (i = 0; i < len; i++)
        fprintf(stderr, "%02x ", p[i]);
    if (desc)
        fprintf(stderr, "[%s]", desc);
    fputc('\n', stderr);
}

static uint64_t mmio_testdev_read_be(void *opaque, hwaddr offset, unsigned size)
{
    MmioTestState *s = MMIO_TEST_DEV(opaque);
    uint64_t value = 0;

    dump_ram(&s->buf.u8[offset], size, "dev_read_be");

    offset /= size;
    switch (size) {
    case 1:
        value = ldub_p(&s->buf.u8[offset]);
        fprintf(stderr, "dev_read_BE_%d @%" PRId64 ": 0x%" PRIx64 "\n", size, offset, value); // 02
        break;
    case 2:
        value = lduw_be_p(&s->buf.u16[offset]);
        fprintf(stderr, "dev_read_BE_%d @%" PRId64 ": 0x%" PRIx64 "\n", size, offset, value); // 04
        break;
    case 4:
        //value = s->buf.u32[offset]; // XXX host?
        value = cpu_to_be32(s->buf.u32[offset]); // host cpu to BE
        //value = ldl_be_p(&s->buf.u32[offset]);
        fprintf(stderr, "dev_read_BE_%d @%" PRId64 ": 0x%" PRIx64 "\n", size, offset, value); // 08
        break;
    case 8:
        value = ldq_be_p(&s->buf.u64[offset]);
        fprintf(stderr, "dev_read_BE_%d @%" PRId64 ": 0x%" PRIx64 "\n", size, offset, value); // 16
        break;
    }

    return value;
}

static uint64_t mmio_testdev_read_le(void *opaque, hwaddr offset, unsigned size)
{
    MmioTestState *s = MMIO_TEST_DEV(opaque);
    uint64_t value = 0;

    dump_ram(&s->buf.u8[offset], size, "dev_read_le");

    offset /= size;
    switch (size) {
    case 1:
        value = ldub_p(&s->buf.u8[offset]);
        fprintf(stderr, "dev_read_LE_%d @%" PRId64 ": 0x%" PRIx64 "\n", size, offset, value);
        break;
    case 2:
        value = lduw_le_p(&s->buf.u16[offset]);
        fprintf(stderr, "dev_read_LE_%d @%" PRId64 ": 0x%" PRIx64 "\n", size, offset, value);
        break;
    case 4:
        value = ldl_le_p(&s->buf.u32[offset]);
        //value = cpu_to_le32(s->buf.u32[offset]); // host cpu to BE
        fprintf(stderr, "dev_read_LE_%d @%" PRId64 ": 0x%" PRIx64 "\n", size, offset, value);
        break;
    case 8:
        value = ldq_le_p(&s->buf.u64[offset]);
        fprintf(stderr, "dev_read_LE_%d @%" PRId64 ": 0x%" PRIx64 "\n", size, offset, value);
        break;
    }

    return value;
}

static void mmio_testdev_write(void *opaque, hwaddr offset, uint64_t value,
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

static void mmio_testdev_add_region(MmioTestState *s, MemoryRegionOps *ops)
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

    name = g_strdup_printf("mmio_testdev_%ce_v%u%u_i%u%u",
                           endian_ch[ops->endianness],
                           ops->valid.min_access_size,
                           ops->valid.max_access_size,
                           ops->impl.min_access_size,
                           ops->impl.max_access_size);

    memory_region_init_io(&s->iomem[idx], OBJECT(s), ops, s,
                          name, IOMEM_SIZE);
    memory_region_add_subregion(get_system_memory(), base.addr, &s->iomem[idx]);
    g_free(name);
}

static void mmio_testdev_realizefn(DeviceState *dev, Error **errp)
{
    static const int endianness_tested[] = {
        DEVICE_LITTLE_ENDIAN,
        DEVICE_BIG_ENDIAN
    };
    MmioTestState *s = MMIO_TEST_DEV(dev);
    MemoryRegionOps *ops;
    int e, vmin_bit, vmax_bit, imin_bit, imax_bit;

    /* fill 0x00112233445566778899aabbccddeeff (2 times) */
    for (size_t i = 0; i < IOMEM_SIZE; i++) {
        s->buf.u8[i] = 0x11 * i;
    }

    for (e = 0; e < ARRAY_SIZE(endianness_tested); e++) {
        for (vmin_bit = 0; vmin_bit < VMINBIT_COUNT; vmin_bit++) {
            for (vmax_bit = vmin_bit; vmax_bit < VMAXBIT_COUNT; vmax_bit++) {
                for (imin_bit = 0; imin_bit < IMINBIT_COUNT; imin_bit++) {
                    for (imax_bit = imin_bit; imax_bit < IMAXBIT_COUNT;
                         imax_bit++) {
                        ops = g_malloc0(sizeof(MemoryRegionOps));
                        ops->endianness = endianness_tested[e];
                        if (ops->endianness == DEVICE_BIG_ENDIAN) {
                            ops->read = mmio_testdev_read_be;
                            ops->write = mmio_testdev_write; // XXX
                        } else {
                            ops->read = mmio_testdev_read_le;
                            ops->write = mmio_testdev_write; // XXX
                        }                            
                        ops->valid.min_access_size = BIT(vmin_bit);
                        ops->valid.max_access_size = BIT(vmax_bit);
                        ops->impl.min_access_size = BIT(imin_bit);
                        ops->impl.max_access_size = BIT(imax_bit);
                        mmio_testdev_add_region(s, ops);
                    }
                }
            }
        }
    }
}

static void mmio_testdev_unrealizefn(DeviceState *dev, Error **errp)
{
    MmioTestState *s = MMIO_TEST_DEV(dev);
    int i;

    for (i = 0; i < s->iocnt; i++) {
        g_free(&s->iomem[i].ops);
    }
}

static Property mmio_interface_properties[] = {
    DEFINE_PROP_UINT64("base", MmioTestState, base, 0),
};

static void mmio_testdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->realize = mmio_testdev_realizefn;
    dc->unrealize = mmio_testdev_unrealizefn;
    dc->props = mmio_interface_properties;
}

static const TypeInfo mmio_testdev_info = {
    .name           = TYPE_MMIO_TEST_DEV,
    .parent         = TYPE_DEVICE,
    .instance_size  = sizeof(MmioTestState),
    .class_init     = mmio_testdev_class_init,
};

static void mmio_testdev_register_types(void)
{
    type_register_static(&mmio_testdev_info);
}

type_init(mmio_testdev_register_types);
