/*
 * QTest testcase for memory::access_with_adjusted_size()
 *
 * Copyright (c) 2017 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "libqtest.h"

#undef CONFIG_USER_ONLY // kludge DEVICE_x_ENDIAN
#include "exec/cpu-common.h"

#if defined(HOST_WORDS_BIGENDIAN)
#error HOST_WORDS_BIGENDIAN NOT YET TESTED
#endif

#define TESTADDR(endianness, valid_min, valid_max, implemented_min, \
            implemented_max, address) \
        ((endianness << 24) | (valid_min << 20) | (valid_max << 16) | \
        (implemented_min << 12) | (implemented_max << 8) | (address & 0x1f))
#if 0
#define LE_ADDR(vmin, vmax, imin, imax, address) \
            TESTADDR(DEVICE_LITTLE_ENDIAN, vmin, vmax, imin, imax, address)
#endif
#define BE_ADDR(vmin, vmax, imin, imax, address) \
            TESTADDR(DEVICE_BIG_ENDIAN, vmin, vmax, imin, imax, address)

typedef struct {
    int endianness;
    int vmin, vmax;
    int imin, imax;
    uint32_t offset;
    uint64_t value;
} TestArgs;

typedef union {
    uint32_t addr;
    struct {
#ifdef HOST_WORDS_BIGENDIAN
#else
        uint8_t data:8, imax:4, imin:4, vmax:4, vmin:4, endianness:4, :4;
#endif
    } access_size;
} iobase_t;


static const char *cmdline_args = "-M none -device memadjust-testdev -trace events=/tmp/events";

static const bool exp_dbg = false;

static const int bits[] = {
    1,
    2,
    4,
    8
};
static const char endian_ch[] = {
    //[DEVICE_NATIVE_ENDIAN] = 'n',
    [DEVICE_BIG_ENDIAN] = 'b',
    [DEVICE_LITTLE_ENDIAN] = 'l',
};
static const char size_ch[] = {
    [1] = 'b',
    [2] = 'w',
    [4] = 'l',
    [8] = 'q',
};

static uint64_t endian_adjust(QTestState *s, int size, uint64_t value)
{
    if (!qtest_big_endian(s)) {
        switch (size) {
        case 2:
            value = be16_to_cpu(value);
            break;
        case 4:
            value = be32_to_cpu(value);
            break;
        case 8:
            value = be64_to_cpu(value);
            break;
        }
    }
    return value;
}

static char *test_desc(const TestArgs *t, int size)
{
    return g_strdup_printf("memory-adjust/read%c/%ce/@%u/vmin%u/vmax%u/imin%u/imax%u",
        size_ch[size],
        endian_ch[t->endianness],
        t->offset,
        t->vmin, t->vmax,
        t->imin, t->imax);
}

static void test_info(const TestArgs *args, int size)
{
    char *name = test_desc(args, size);
    fprintf(stderr, "\n--------\n%s\n", name);
    g_free(name);
}

static void test_memadjust_readb(gconstpointer data)
{
    const TestArgs *a = data;
    uint64_t exp_value;
    QTestState *s = NULL;

    test_info(a, 1);
    s = qtest_start(cmdline_args);
    exp_value = endian_adjust(s, 1, a->value);

    if (exp_dbg) fprintf(stderr, "@%08x exp_i:0x%" PRIx64 "\n", BE_ADDR(a->vmin, a->vmax, a->imin, a->imax, a->offset), exp_value);
    uint8_t x = qtest_readb(s, TESTADDR(a->endianness, a->vmin, a->vmax, a->imin, a->imax, a->offset));
    if (exp_dbg) fprintf(stderr, "@%08x exp_o:0x%" PRIx8 "\n", BE_ADDR(a->vmin, a->vmax, a->imin, a->imax, a->offset), x);
    g_assert_cmphex(x, ==, exp_value);

    //qtest_quit(global_qtest);
    fputc('\n', stderr);
    qtest_end();
}

static void test_memadjust_readw(gconstpointer data)
{
    const TestArgs *a = data;
    uint64_t exp_value;
    QTestState *s = NULL;

    test_info(a, 2);
    s = qtest_start(cmdline_args);
    exp_value = endian_adjust(s, 2, a->value);

    if (exp_dbg) fprintf(stderr, "@%08x exp_i:0x%" PRIx32 "\n", BE_ADDR(a->vmin, a->vmax, a->imin, a->imax, a->offset), cpu_to_be16(exp_value));
    uint16_t x = qtest_readw(s, TESTADDR(a->endianness, a->vmin, a->vmax, a->imin, a->imax, a->offset));
    if (exp_dbg) fprintf(stderr, "@%08x exp_o:0x%" PRIx32 "\n", BE_ADDR(a->vmin, a->vmax, a->imin, a->imax, a->offset), cpu_to_be16(x));
    g_assert_cmphex(x, ==, exp_value);

    //qtest_quit(global_qtest);
    fputc('\n', stderr);
    qtest_end();
}

static void test_memadjust_readl(gconstpointer data)
{
    const TestArgs *a = data;
    uint64_t exp_value;
    QTestState *s = NULL;

    test_info(a, 4);
    s = qtest_start(cmdline_args);
    exp_value = endian_adjust(s, 4, a->value);

    if (exp_dbg) fprintf(stderr, "@%08x exp_i:0x%" PRIx32 "\n", BE_ADDR(a->vmin, a->vmax, a->imin, a->imax, a->offset), cpu_to_be32(exp_value));
    uint32_t x = qtest_readl(s, TESTADDR(a->endianness, a->vmin, a->vmax, a->imin, a->imax, a->offset));
    if (exp_dbg) fprintf(stderr, "@%08x exp_o:0x%" PRIx32 "\n", BE_ADDR(a->vmin, a->vmax, a->imin, a->imax, a->offset), cpu_to_be32(x));
    g_assert_cmphex(x, ==, exp_value);

    //qtest_quit(global_qtest);
    fputc('\n', stderr);
    qtest_end();
}

static void test_memadjust_readq(gconstpointer data)
{
    const TestArgs *a = data;
    uint64_t exp_value;
    QTestState *s = NULL;

    test_info(a, 8);
    s = qtest_start(cmdline_args);
    exp_value = endian_adjust(s, 8, a->value);

    if (exp_dbg) fprintf(stderr, "@%08x exp_i:0x%" PRIx64 "\n", BE_ADDR(a->vmin, a->vmax, a->imin, a->imax, a->offset), cpu_to_be64(exp_value));
    uint64_t x = qtest_readq(s, TESTADDR(a->endianness, a->vmin, a->vmax, a->imin, a->imax, a->offset));
    if (exp_dbg) fprintf(stderr, "@%08x exp_o:0x%" PRIx64 "\n", BE_ADDR(a->vmin, a->vmax, a->imin, a->imax, a->offset), cpu_to_be64(x));
    g_assert_cmphex(x, ==, exp_value);

    //qtest_quit(global_qtest);
    fputc('\n', stderr);
    qtest_end();
}

static void dump_ram(void *ptr, size_t len, const char *desc)
{
#if 0
    int i;
    uint8_t *p = (uint8_t *)ptr;

    fputs("RAM: ", stderr);
    for (i = 0; i < len; i++)
        fprintf(stderr, "%02x ", p[i]);
    if (desc)
        fprintf(stderr, "[%s]", desc);
    fputc('\n', stderr);
#endif
}

static void add_memadjust_test_case(int endianness, int size, int vmin, int vmax, int imin, int imax, uint32_t offset, uint64_t value, void (*fn)(const void *))
{
    char *name;
    //static int test_cnt = 0;

    if (size > vmax) {
        return;
    }

    TestArgs *args = g_malloc0(sizeof(TestArgs));
    args->endianness = endianness;
    args->vmin = vmin;
    args->vmax = vmax;
    args->imin = imin;
    args->imax = imax;
    args->offset = offset;
    args->value = value;

    name = test_desc(args, size);
    dump_ram(&value, size, NULL);

    qtest_add_data_func(name, args, fn);

    g_free(name);
}

static void add_memadjust_test_cases_endian(int endianness, int size, uint32_t offset, uint64_t value, void (*fn)(const void *))
{
    int vi, va, ii, ia;

    for (vi = 0; vi < ARRAY_SIZE(bits); vi++) {
        for (va = 0; va < ARRAY_SIZE(bits); va++) {
            if (bits[vi] > bits[va]) {
                continue;
            }
            for (ii = 0; ii < ARRAY_SIZE(bits); ii++) {
                for (ia = 0; ia < ARRAY_SIZE(bits); ia++) {
                    if (bits[ii] > bits[ia]) {
                        continue;
                    }
                    add_memadjust_test_case(endianness, size, bits[vi], bits[va], bits[ii], bits[ia], offset, value, fn);
                }
            }
        }
    }
}

static void add_memadjust_test_cases(int size, uint32_t offset, uint64_t value, void (*fn)(const void *))
{
    add_memadjust_test_cases_endian(DEVICE_BIG_ENDIAN, size, offset, value, fn);
    //add_memadjust_test_cases_endian(DEVICE_LITTLE_ENDIAN, size, offset, value, fn);
}

int main(int argc, char **argv)
{
#if defined(HOST_WORDS_BIGENDIAN)
    fprintf(stderr, "HOST:   BE\n");
#else
    fprintf(stderr, "HOST:   LE\n");
#endif
    fprintf(stderr, "DEVICE_BIG_ENDIAN:      %d\n", DEVICE_BIG_ENDIAN);
    fprintf(stderr, "DEVICE_LITTLE_ENDIAN:   %d\n", DEVICE_LITTLE_ENDIAN);

    g_test_init(&argc, &argv, NULL);

    if (0) {
    add_memadjust_test_cases(1, 0x01, 0x11, test_memadjust_readb);
    add_memadjust_test_cases(1, 0x02, 0x22, test_memadjust_readb);
    add_memadjust_test_cases(1, 0x03, 0x33, test_memadjust_readb);
    //..
    add_memadjust_test_cases(1, 0x07, 0x77, test_memadjust_readb);

    add_memadjust_test_cases(2, 0x00, 0x0011, test_memadjust_readw);
    add_memadjust_test_cases(2, 0x02, 0x2233, test_memadjust_readw);
    add_memadjust_test_cases(2, 0x04, 0x4455, test_memadjust_readw);
    add_memadjust_test_cases(2, 0x06, 0x6677, test_memadjust_readw);
    }

    add_memadjust_test_cases(4, 0x00, 0x00112233, test_memadjust_readl);
    if (0) {
    add_memadjust_test_cases(4, 0x04, 0x44556677, test_memadjust_readl);
    add_memadjust_test_cases(8, 0x00, 0x0011223344556677, test_memadjust_readq);
    }

    return g_test_run();
}
