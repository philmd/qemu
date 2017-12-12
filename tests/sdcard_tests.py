#!/usr/bin/env python
#
# Tests for the SD-Bus protocol
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import os
import base64
import struct
import binascii
import logging

import qtest


# CMD
(GO_IDLE_STATE, SEND_OP_CMD, ALL_SEND_CID, SEND_RELATIVE_ADDR, # 0 ...
SEND_DSR, CMD5, SWITCH_FUNCTION, CMD7,
SEND_IF_COND, SEND_CSD, SEND_CID, READ_DAT_UNTIL_STOP, # 8 ...
STOP_TRANSMISSION, SEND_STATUS, CMD14, GO_INACTIVE_STATE,
SET_BLOCKLEN, READ_SINGLE_BLOCK, READ_MULTIPLE_BLOCK, CMD19, # 16 ...
CMD20, CMD21, CMD22, SET_BLOCK_COUNT,
WRITE_SINGLE_BLOCK, WRITE_MULTIPLE_BLOCK, PROGRAM_CID, PROGRAM_CSD,  # 24 ...
SET_WRITE_PROT, CLR_WRITE_PROT, SEND_WRITE_PROT, CMD31,
ERASE_WR_BLK_START, ERASE_WR_BLK_END, CMD34, CMD35, # 32 ...
CMD36, CMD37, ERASE, CMD39,
CMD40, CMD41, LOCK_UNLOCK, CMD43,
CMD44, CMD45, CMD46, CMD47,
CMD48, CMD49, CMD50, CMD51,
CMD52, CMD53, CMD54, APP_CMD,
GEN_CMD,) = range(57)

# ACMD
SET_BUS_WIDTH = 6
SD_STATUS = 13
SEND_NUM_WR_BLOCKS = 22
SET_WR_BLK_ERASE_COUNT = 23
SD_APP_OP_COND = 41
SET_CLR_CARD_DETECT = 42
SEND_SCR = 51


class SDBus(object):
    def __init__(self, vm, qom_path, verbose=False):
        self.vm = vm
        self.path = qom_path
        self.verbose = verbose

    def do_cmd(self, command, arg=0, verbose=None):
        assert command < 64
        if verbose is None:
            verbose = self.verbose
        data = self.vm.qmp('sdbus-command', qom_path=self.path, command=command,
                           arg=arg)['return']
        datab = base64.b64decode(data['base64']) if 'base64' in data else None
        datas = binascii.hexlify(datab).decode() if datab else "(none)"
        logging.info("CMD#%02d -> %s" % (command, datas))
        return datab

    def do_acmd(self, command, arg=0, verbose=None):
        self.do_cmd(APP_CMD, verbose=False)
        return self.do_cmd(command, arg, verbose if verbose else self.verbose)


def sdbus_get_qom_path(vm, bus=0):
        qom_paths = []

        result = vm.qmp('query-block')
        for block in result['return']:
            if not 'qdev' in block:
                continue
            d = vm.qmp('qom-get', path=block['qdev'], property="parent_bus")
            qom_paths += [d['return']]
        assert len(qom_paths) > 0

        return qom_paths[bus]


# dumb helper
def l(buf):
    return struct.unpack("<L", buf)[0]


class TestSdCardSpecV2(qtest.QMPTestCase):
    def setUp(self):
        self.vm = qtest.createQtestMachine()

        self.vm._args.append('-drive')
        self.vm._args.append("id=testcard,driver=null-co,if=sd")
        self.vm._args.append("-machine")
        self.vm._args.append("xilinx-zynq-a9")
        self.vm.launch()

        bus_path = sdbus_get_qom_path(self.vm)
        self.bus = SDBus(self.vm, bus_path, True)

    def tearDown(self):
        self.vm.shutdown()

    def test_power_on_v2(self):

        self.bus.do_cmd(GO_IDLE_STATE)

        # get voltages
        for i in range(6):
            vhs = 1 << (8 + i)
            data = self.bus.do_cmd(SEND_IF_COND, vhs)
            v = l(data)
            self.assertNotEqual(v, 0)
            self.assertEqual(vhs, v >> 8)

        # get OCR
        data = self.bus.do_acmd(SD_APP_OP_COND)
        v = l(data)
        ocr = (v >> 8) & 0xffff
        s1_8 = (v >> 24) & 1
        uhs_ii = (v >> 29) & 1
        ccs = (v >> 30) & 1
        init = (v >> 31) & 1
        # all those are null for v2.00
        self.assertEqual(s1_8, 0)
        self.assertEqual(uhs_ii, 0)
        self.assertEqual(ccs, 0)
        self.assertEqual(init, 0)

        # ocr << 8
        # 0 << 24 # use current voltage
        # 0 << 28 # powersave
        # 0 << 30 # SDSC
        data = self.bus.do_acmd(SD_APP_OP_COND, ocr << 8)
        v = l(data)
        # check OCR accepted
        self.assertEqual(ocr, v >> 8)

        # check CID
        data = self.bus.do_cmd(ALL_SEND_CID)
        oid, pnm, psn = struct.unpack(">x2s5sxLxxx", data)
        self.assertEqual(oid, b"XY") # QEMU default
        self.assertEqual(pnm, b"QEMU!") # QEMU default
        self.assertEqual(psn, 0xdeadbeef) # QEMU default

        # check non null RCA
        data = self.bus.do_cmd(SEND_RELATIVE_ADDR)
        rca, = struct.unpack(">H", data[:2])
        self.assertNotEqual(rca, 0)

        self.assertEqual(rca, 0x4567) # QEMU default

        # check for new RCA
        data = self.bus.do_cmd(SEND_RELATIVE_ADDR)
        new_rca, = struct.unpack(">H", data[:2])
        self.assertNotEqual(new_rca, rca)


if __name__ == '__main__':
    qtest.main(supported_machines=['xilinx-zynq-a9'])
