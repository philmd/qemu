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

static void test_endianness(gconstpointer data)
{
    QTestState *s = NULL;

    s = qtest_start("-M none -device mem-adjust-testdev");

    g_assert_cmphex(qtest_readq(s, 0x00181100), ==, const_be64(0x0011223344556677));
    g_assert_cmphex(qtest_readl(s, 0x00181104), ==, const_be32(0x44556677));
    g_assert_cmphex(qtest_readw(s, 0x00181202), ==, const_be16(0x2233));
    g_assert_cmphex(qtest_readl(s, 0x00181200), ==, const_be32(0x00112233));
    g_assert_cmphex(qtest_readq(s, 0x00181200), ==, const_be64(0x0011223344556677));
    g_assert_cmphex(qtest_readl(s, 0x00181400), ==, const_be32(0x00112233));

    g_assert_cmphex(qtest_readq(s, 0x01181100), ==, const_be64(0x0011223344556677));
    g_assert_cmphex(qtest_readw(s, 0x01181100), ==, const_be16(0x0011));
    g_assert_cmphex(qtest_readw(s, 0x01181102), ==, const_be16(0x2233));
    g_assert_cmphex(qtest_readw(s, 0x01181200), ==, const_be16(0x0011));
    g_assert_cmphex(qtest_readw(s, 0x01181202), ==, const_be16(0x2233));
    g_assert_cmphex(qtest_readl(s, 0x01181200), ==, const_be32(0x00112233));

    g_assert_cmphex(qtest_readl(s, 0x01181100), ==, const_be32(0x00112233));
    g_assert_cmphex(qtest_readl(s, 0x01181104), ==, const_be32(0x44556677));
    g_assert_cmphex(qtest_readq(s, 0x01181100), ==, const_be64(0x0011223344556677));

    g_assert_cmphex(qtest_readl(s, 0x01181200), ==, const_be32(0x00112233));
    g_assert_cmphex(qtest_readl(s, 0x01181204), ==, const_be32(0x44556677));
    g_assert_cmphex(qtest_readq(s, 0x01181200), ==, const_be64(0x0011223344556677));

    g_assert_cmphex(qtest_readl(s, 0x01181400), ==, const_be32(0x00112233));
    g_assert_cmphex(qtest_readl(s, 0x01181404), ==, const_be32(0x44556677));
    g_assert_cmphex(qtest_readq(s, 0x01181400), ==, const_be64(0x0011223344556677));
    g_assert_cmphex(qtest_readq(s, 0x01181800), ==, const_be64(0x0011223344556677));

    qtest_quit(global_qtest);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_data_func("memory-adjust", qtest_get_arch(), test_endianness);

    return g_test_run();
}
