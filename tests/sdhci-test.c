/*
 * QTest testcase for SDHCI controllers
 *
 * Written by Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/registerfields.h"
#include "libqtest.h"
#include "libqos/pci-pc.h"
#include "hw/pci/pci.h"

#define SDHC_CAPAB                      0x40
#define SDHC_HCVER                      0xFE

static const struct sdhci_t {
    const char *arch, *machine;
    struct {
        uintptr_t addr;
        uint8_t version;
        uint8_t baseclock;
        struct {
            bool sdma;
            uint64_t reg;
        } capab;
    } sdhci;
    struct {
        uint16_t vendor_id, device_id;
    } pci;
} models[] = {
    /* PC via PCI */
    { "x86_64", "pc",
        {-1,         2, 0,  {1, 0x057834b4} },
        .pci = { PCI_VENDOR_ID_REDHAT, PCI_DEVICE_ID_REDHAT_SDHCI } },

    /* Exynos4210 */
    { "arm",    "smdkc210",
        {0x12510000, 2, 0,  {1, 0x5e80080} } },
};

static struct {
    QPCIBus *pcibus;
    QPCIDevice *dev;
    QPCIBar mem_bar;
} g = { };

static uint64_t sdhci_readq(uintptr_t base, uint32_t reg_addr)
{
    if (g.dev) {
        uint64_t value;

        qpci_memread(g.dev, g.mem_bar, reg_addr, &value, sizeof(value));

        return value;
    } else {
        QTestState *qtest = global_qtest;

        return qtest_readq(qtest, base + reg_addr);
    }
}

static void sdhci_writeq(uintptr_t base, uint32_t reg_addr, uint64_t value)
{
    if (g.dev) {
        qpci_memwrite(g.dev, g.mem_bar, reg_addr, &value, sizeof(value));
    } else {
        QTestState *qtest = global_qtest;

        qtest_writeq(qtest, base + reg_addr, value);
    }
}

static void check_capab_capareg(uintptr_t addr, uint64_t expected_capab)
{
    uint64_t capab;

    capab = sdhci_readq(addr, SDHC_CAPAB);
    g_assert_cmphex(capab, ==, expected_capab);
}

static void check_capab_readonly(uintptr_t addr)
{
    const uint64_t vrand = 0x123456789abcdef;
    uint64_t capab0, capab1;

    capab0 = sdhci_readq(addr, SDHC_CAPAB);
    g_assert_cmpuint(capab0, !=, vrand);

    sdhci_writeq(addr, SDHC_CAPAB, vrand);
    capab1 = sdhci_readq(addr, SDHC_CAPAB);
    g_assert_cmpuint(capab1, !=, vrand);
    g_assert_cmpuint(capab1, ==, capab0);
}

static void machine_start(const struct sdhci_t *test)
{
    if (test->pci.vendor_id) {
        /* PCI */
        uint16_t vendor_id, device_id;
        uint64_t barsize;

        global_qtest = qtest_startf("-machine %s -d unimp -device sdhci-pci",
                                    test->machine);

        g.pcibus = qpci_init_pc(NULL);

        /* Find PCI device and verify it's the right one */
        g.dev = qpci_device_find(g.pcibus, QPCI_DEVFN(4, 0));
        g_assert_nonnull(g.dev);
        vendor_id = qpci_config_readw(g.dev, PCI_VENDOR_ID);
        device_id = qpci_config_readw(g.dev, PCI_DEVICE_ID);
        g_assert(vendor_id == test->pci.vendor_id);
        g_assert(device_id == test->pci.device_id);
        g.mem_bar = qpci_iomap(g.dev, 0, &barsize);
        qpci_device_enable(g.dev);
    } else {
        /* SysBus */
        global_qtest = qtest_startf("-machine %s -d unimp", test->machine);
    }
}

static void machine_stop(void)
{
    g_free(g.dev);
    qtest_quit(global_qtest);
}

static void test_machine(const void *data)
{
    const struct sdhci_t *test = data;

    machine_start(test);

    check_capab_capareg(test->sdhci.addr, test->sdhci.capab.reg);
    check_capab_readonly(test->sdhci.addr);

    machine_stop();
}

int main(int argc, char *argv[])
{
    const char *arch = qtest_get_arch();
    char *name;
    int i;

    g_test_init(&argc, &argv, NULL);
    for (i = 0; i < ARRAY_SIZE(models); i++) {
        if (strcmp(arch, models[i].arch)) {
            continue;
        }
        name = g_strdup_printf("sdhci/%s", models[i].machine);
        qtest_add_data_func(name, &models[i], test_machine);
        g_free(name);
    }

    return g_test_run();
}
