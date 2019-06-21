# Test the bFLT format
#
# Author:
#  Philippe Mathieu-Daudé <f4bug@amsat.org>
#
# SPDX-License-Identifier: GPL-2.0-or-later


import os
import bz2
import subprocess

from avocado_qemu import LinuxUserTest


class LoadBFLT(LinuxUserTest):

    def extract_cpio(self, cpio_path):
        """
        Extracts a cpio archive into the test workdir

        :param cpio_path: path to the cpio archive
        """
        cwd = os.getcwd()
        os.chdir(self.workdir)
        with bz2.open(cpio_path, 'rb') as archive_cpio:
            subprocess.run(['cpio', '-i'], input=archive_cpio.read(),
                           stderr=subprocess.DEVNULL)
        os.chdir(cwd)

    def test_stm32(self):
        """
        :avocado: tags=arch:arm
        :avocado: tags=linux_user
        :avocado: tags=quick
        """
        rootfs_url = ('https://elinux.org/images/5/51/'
                      'Stm32_mini_rootfs.cpio.bz2')
        rootfs_hash = '9f065e6ba40cce7411ba757f924f30fcc57951e6'
        rootfs_path_bz2 = self.fetch_asset(rootfs_url, asset_hash=rootfs_hash)
        busybox_path = self.workdir + "/bin/busybox"

        self.extract_cpio(rootfs_path_bz2)

        cmd = ''
        res = self.run("%s %s" % (busybox_path, cmd))
        ver = 'BusyBox v1.24.0.git (2015-02-03 22:17:13 CET) multi-call binary.'
        self.assertIn(ver, res.stdout_text)

        cmd = 'uname -a'
        res = self.run("%s %s" % (busybox_path, cmd))
        unm = 'armv7l GNU/Linux'
        self.assertIn(unm, res.stdout_text)
