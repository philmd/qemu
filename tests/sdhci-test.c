/*
 * QTest testcase for SDHCI controllers
 *
 * Written by Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "libqtest.h"

#define SDHC_CAPAB                      0x40
#define SDHC_HCVER                      0xFE

static const struct sdhci_t {
    const char *arch;
    const char *machine;
    struct {
        uintptr_t addr;
        uint8_t version;
        uint8_t baseclock;
        struct {
            bool sdma;
            uint64_t reg;
        } capab;
    } sdhci;
} models[] = {
    /* Exynos4210 */
    { "arm",    "smdkc210",
        {0x12510000, 2, 0, {1, 0x5e80080} } },

    /* i.MX 6 */
    { "arm",    "sabrelite",
        {0x02190000, 3, 0, {1, 0x057834b4} } },

    /* BCM2835 */
    { "arm",    "raspi2",
        {0x3f300000, 3, 52, {0, 0x52034b4} } },

    /* Zynq-7000 */
    { "arm",    "xilinx-zynq-a9",
        {0xe0100000, 2, 0, {1, 0x01790080} } },

    /* ZynqMP */
    { "aarch64", "xlnx-zcu102",
        {0xff160000, 3, 0, {1, 0x7715e80080} } },
};

static uint32_t sdhci_readl(uintptr_t base, uint32_t reg_addr)
{
    QTestState *qtest = global_qtest;

    return qtest_readl(qtest, base + reg_addr);
}

static uint64_t sdhci_readq(uintptr_t base, uint32_t reg_addr)
{
    QTestState *qtest = global_qtest;

    return qtest_readq(qtest, base + reg_addr);
}

static void sdhci_writeq(uintptr_t base, uint32_t reg_addr, uint64_t value)
{
    QTestState *qtest = global_qtest;

    qtest_writeq(qtest, base + reg_addr, value);
}

static void check_specs_version(uintptr_t addr, uint8_t version)
{
    uint32_t v;

    v = sdhci_readl(addr, SDHC_HCVER);
    v &= 0xff;
    v += 1;
    g_assert_cmpuint(v, ==, version);
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

static void test_machine(const void *data)
{
    const struct sdhci_t *test = data;

    global_qtest = qtest_startf("-machine %s -d unimp", test->machine);

    check_capab_capareg(test->sdhci.addr, test->sdhci.capab.reg);
    check_specs_version(test->sdhci.addr, test->sdhci.version);
    check_capab_readonly(test->sdhci.addr);

    qtest_quit(global_qtest);
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
