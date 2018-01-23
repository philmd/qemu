/*
 * QTest testcase for the SD/MMC cards
 *
 * Copyright (c) 2017 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/cutils.h"

#include "libqtest.h"
#include "libqos/sdbus.h"

enum {
    PROTO_SD,
    PROTO_MMC,
    PROTO_SPI,
    PROTO_COUNT
};

static const char *proto_name[PROTO_COUNT] = {
    [PROTO_SD]  = "sd",
    [PROTO_MMC] = "mmc",
    [PROTO_SPI] = "spi"
};

static const char *machines[PROTO_COUNT] = {
    //[PROTO_SD] = "nuri",
    [PROTO_MMC] = "vexpress-a9", // No 'sd-bus' bus found for device 'sd-card'
    //[PROTO_SPI] = "lm3s6965evb" // No 'sd-bus' bus found for device 'sd-card'
};

static const struct {
    const char *name;
    uint64_t val;
} sizes[] = {
    {"512MB", 512 * M_BYTE},
    {"1GB", 1 * G_BYTE},
    {"4GB", 4 * G_BYTE},
    {"64GB", 64 * G_BYTE},
};

typedef struct {
    int protocol;
    uint64_t size;
} TestCase;

static void test_sd(SDBusAdapter *bus, uint64_t size)
{
    uint8_t *response;
    uint16_t rca;
    ssize_t sz;

    sz = sdbus_do_cmd(bus, GO_IDLE_STATE, 0, NULL);
    g_assert_cmpuint(sz, ==, 0);

    sz = sdbus_do_cmd(bus, SEND_IF_COND, 0x1aa, NULL);
    //g_assert_cmpuint(sz, ==, 0);

    sz = sdbus_do_acmd(bus, SEND_OP_COND, 0x40300000, 0, NULL);
    g_assert_cmpuint(sz, ==, 4);

    /* CID */
    sz = sdbus_do_cmd(bus, ALL_SEND_CID, 0, &response);
    g_assert_cmpuint(sz, ==, 16);
    g_assert_cmpmem (&response[3], 5, "QEMU!", 5);
    g_assert_cmphex(be32_to_cpu(*(uint32_t *)&response[9]), ==, 0xdeadbeef);
    g_free(response);

    /* RCA */
    sz = sdbus_do_cmd(bus, SEND_RELATIVE_ADDR, 0, &response);
    g_assert_cmpuint(sz, ==, 4);
    rca = be16_to_cpu(*(uint16_t *)response);
    g_assert_cmphex(rca, ==, 0x4567);
    g_free(response);

    /* CSD */
    sz = sdbus_do_cmd(bus, SEND_CSD, rca << 16, &response);
    g_assert_cmpuint(sz, ==, 16);
    g_assert_cmphex(response[3], ==, 0x32);
    if (size > 1 * G_BYTE) {
        g_assert_cmphex(response[4], ==, 0x5b); /* class */
        g_assert_cmphex(response[5], ==, 0x59);
        /* (SDHC test) */
        g_assert_cmphex(be32_to_cpu(*(uint32_t *)&response[6]),
                        ==, (size >> 19) - 1);
        g_assert_cmphex(response[10], ==, 0x7f);
        g_assert_cmphex(response[11], ==, 0x80);
        g_assert_cmphex(response[12], ==, 0x0a);
        g_assert_cmphex(response[13], ==, 0x40);
    }
    g_assert_cmphex(response[14], ==, 0);
    g_free(response);

    sz = sdbus_do_cmd(bus, SELECT_CARD, rca << 16, NULL);

    sz = sdbus_do_acmd(bus, SEND_SCR, 0, rca, &response);
    g_assert_cmpuint(sz, ==, 4);
    g_free(response);

    // TODO 8x: sdcard_read_data len 512

    //sz = sdbus_do_acmd(mmc, SEND_STATUS, 0, rca, &response);
    //g_free(response);
}

static void sdcard_tests(gconstpointer data)
{
    const TestCase *test = data;
    SDBusAdapter *sdbus;
    const char *machine = machines[test->protocol];
    const char *extra_params = "";

    if (!strcmp(qtest_get_arch(), "x86_64")) {
        machine = "pc";
        extra_params = "-device sdhci-pci";
    }

    global_qtest = qtest_startf("-machine %s %s " "-trace events=/tmp/events -d unimp,guest_errors "
                         "-drive if=none,id=card0,driver=null-co,size=%" PRIu64 ",id=mmc0 "
                         "-device sd-card,drive=card0",
                         machine, extra_params, test->size);
    sdbus = qmp_sdbus_create("sd-bus");

    if (test->protocol == PROTO_SD) {
        test_sd(sdbus, test->size);
    }
    g_free(sdbus);

    qtest_quit(global_qtest);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();
    int iproto, isize;
    gchar *path;
    TestCase *test;

    g_test_init(&argc, &argv, NULL);

    for (iproto = 0; iproto < PROTO_COUNT; iproto++) {
        if (!strcmp(arch, "arm") || !strcmp(arch, "aarch64")) {
            if (!machines[iproto]) {
                continue;
            }
        } else if (strcmp(arch, "x86_64")) {
            continue;
        }
            
        for (isize = 0; isize < ARRAY_SIZE(sizes); isize++) {
            test = g_new(TestCase, 1);

            test->protocol = iproto;
            test->size = sizes[isize].val;

            path = g_strdup_printf("sdcard/%s/%s", proto_name[iproto], sizes[isize].name);
            qtest_add_data_func(path, test, sdcard_tests);
            g_free(path);
            // g_free(test)?
        }
    }

    return g_test_run();
}
