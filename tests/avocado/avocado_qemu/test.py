# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# See LICENSE for more details.
#
# Copyright (C) 2017 Red Hat Inc
#
# Authors:
#  Amador Pahim <apahim@redhat.com>
#
# Based on code from:
#   https://github.com/avocado-framework/avocado-virt


"""
Avocado Qemu Test module to extend the Avocado Test module providing
extra features intended for Qemu testing.
"""


import logging
import os
import re
import sys
import tempfile
import time
import uuid

import aexpect

from avocado import Test
from avocado.utils import network
from avocado.utils import process
from avocado.utils import path as utils_path
from avocado.utils import wait

QEMU_ROOT = os.path.abspath(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(__file__)))))
sys.path.append(os.path.join(QEMU_ROOT, 'scripts'))
import qemu


class QEMULoginTimeoutError(Exception):
    """
    If timeout expires
    """


class QEMULoginAuthenticationError(Exception):
    """
    If authentication fails
    """


class QEMULoginProcessTerminatedError(Exception):
    """
    If the client terminates during login
    """


class QEMULoginError(Exception):
    """
    If some other error occurs
    """


class QEMUConsoleError(Exception):
    """
    If some error with the console access happens
    """


class QEMUMigrationError(Exception):
    """
    If some error with the migration happens
    """


class QEMUCloudinitError(Exception):
    """
    If some error with the cloudinit happens
    """


def _get_qemu_bin(arch):
    git_root = process.system_output('git rev-parse --show-toplevel',
                                     ignore_status=True,
                                     verbose=False)
    qemu_binary = os.path.join(git_root,
                               "%s-softmmu" % arch,
                               "qemu-system-%s" % arch)
    if not os.path.exists(qemu_binary):
        qemu_binary = utils_path.find_command('qemu-system-%s' % arch)
    return qemu_binary


def _handle_prompts(session, username, password, prompt, timeout=60,
                    debug=False):
    """
    Connect to a remote host (guest) using SSH or Telnet or else.

    Wait for questions and provide answers.  If timeout expires while
    waiting for output from the child (e.g. a password prompt or
    a shell prompt) -- fail.

    :param session: An Expect or ShellSession instance to operate on
    :param username: The username to send in reply to a login prompt
    :param password: The password to send in reply to a password prompt
    :param prompt: The shell prompt that indicates a successful login
    :param timeout: The maximal time duration (in seconds) to wait for each
            step of the login procedure (i.e. the "Are you sure" prompt, the
            password prompt, the shell prompt, etc)
    :raise QEMULoginTimeoutError: If timeout expires
    :raise QEMULoginAuthenticationError: If authentication fails
    :raise QEMULoginProcessTerminatedError: If the client terminates during login
    :raise QEMULoginError: If some other error occurs
    :return: If connect succeed return the output text to script for further
             debug.
    """
    re_kernel_message = re.compile(r"^\[\s*\d+.\d+\] ")

    def get_last_nonempty_line(cont):
        """Return last non-empty non-kernel line"""
        nonempty_lines = [_ for _ in cont.splitlines()
                          if _.strip() and not re_kernel_message.match(_)]
        if nonempty_lines:
            return nonempty_lines[-1]
        else:
            return ""

    password_prompt_count = 0
    login_prompt_count = 0
    last_chance = False
    # Send enter to refresh output (in case session was attached after boot)
    session.sendline()
    output = ""
    while True:
        try:
            match, text = session.read_until_output_matches(
                [r"[Aa]re you sure", r"[Pp]assword:\s*",
                 # Prompt of rescue mode for Red Hat.
                 r"\(or (press|type) Control-D to continue\):\s*",
                 r"[Gg]ive.*[Ll]ogin:\s*",  # Prompt of rescue mode for SUSE.
                 r"(?<![Ll]ast )[Ll]ogin:\s*",  # Don't match "Last Login:"
                 r"[Cc]onnection.*closed", r"[Cc]onnection.*refused",
                 r"[Pp]lease wait", r"[Ww]arning", r"[Ee]nter.*username",
                 r"[Ee]nter.*password", r"[Cc]onnection timed out", prompt,
                 r"Escape character is.*"], get_last_nonempty_line,
                timeout=timeout, internal_timeout=0.5)
            output += text
            if match == 0:  # "Are you sure you want to continue connecting"
                if debug:
                    logging.debug("Got 'Are you sure...', sending 'yes'")
                session.sendline("yes")
                continue
            elif match in [1, 2, 3, 10]:  # "password:"
                if password_prompt_count == 0:
                    if debug:
                        logging.debug("Got password prompt, sending '%s'",
                                      password)
                    session.sendline(password)
                    password_prompt_count += 1
                    continue
                else:
                    raise QEMULoginAuthenticationError("Got password prompt "
                                                       "twice", text)
            elif match == 4 or match == 9:  # "login:"
                if login_prompt_count == 0 and password_prompt_count == 0:
                    if debug:
                        logging.debug("Got username prompt; sending '%s'",
                                      username)
                    session.sendline(username)
                    login_prompt_count += 1
                    continue
                else:
                    if login_prompt_count > 0:
                        msg = "Got username prompt twice"
                    else:
                        msg = "Got username prompt after password prompt"
                    raise QEMULoginAuthenticationError(msg, text)
            elif match == 5:  # "Connection closed"
                raise QEMULoginError("Client said 'connection closed'", text)
            elif match == 6:  # "Connection refused"
                raise QEMULoginError("Client said 'connection refused'", text)
            elif match == 11:  # Connection timeout
                raise QEMULoginError("Client said 'connection timeout'", text)
            elif match == 7:  # "Please wait"
                if debug:
                    logging.debug("Got 'Please wait'")
                timeout = 30
                continue
            elif match == 8:  # "Warning added RSA"
                if debug:
                    logging.debug("Got 'Warning added RSA to known host list")
                continue
            elif match == 12:  # prompt
                if debug:
                    logging.debug("Got shell prompt -- logged in")
                break
            elif match == 13:  # console prompt
                logging.debug("Got console prompt, send return to show login")
                session.sendline()
        except aexpect.ExpectTimeoutError as details:
            # sometimes, linux kernel print some message to console
            # the message maybe impact match login pattern, so send
            # a empty line to avoid unexpect login timeout
            if not last_chance:
                time.sleep(0.5)
                session.sendline()
                last_chance = True
                continue
            else:
                raise QEMULoginTimeoutError(details.output)
        except aexpect.ExpectProcessTerminatedError as details:
            raise QEMULoginProcessTerminatedError(details.status, details.output)

    return output


class _VM(qemu.QEMUMachine):
    '''A QEMU VM'''

    def __init__(self, qemu_bin=None, arch=None, qemu_dst_bin=None,
                 username=None, password=None):
        if arch is None:
            arch = os.uname()[4]
        self.arch = arch
        self.ports = network.PortTracker()
        self.name = "qemu-%s" % str(uuid.uuid4())[:8]
        if qemu_bin is None:
            qemu_bin = _get_qemu_bin(arch)
        if qemu_dst_bin is None:
            qemu_dst_bin = qemu_bin
        self.qemu_bin = qemu_bin
        self.qemu_dst_bin = qemu_dst_bin
        self.username = username
        self.password = password
        super(_VM, self).__init__(qemu_bin, name=self.name, arch=arch)
        logging.getLogger('QMP').setLevel(logging.INFO)

    def get_console(self, console_address=None, prompt=r"[\#\$] "):
        """
        :param address: Socket address, can be either a unix socket path
                        (string) or a tuple in the form (address, port)
                        for a TCP connection
        :param prompt: The regex to identify we reached the prompt.
        """

        if not all((self.username, self.password)):
            raise QEMULoginError('Username or password not set.')

        if not self.is_running():
            raise QEMULoginError('VM is not running.')

        if console_address is None:
            if self._console_address is None:
                raise QEMUConsoleError("Can't determine the console address "
                                       "to connect to.")
            else:
                console_address = self._console_address

        nc_cmd = 'nc'
        if isinstance(console_address, tuple):
            nc_cmd += ' %s %s' % (console_address[0], console_address[1])
        else:
            nc_cmd += ' -U %s' % console_address

        console = aexpect.ShellSession(nc_cmd)
        try:
            logging.info('Console: Waiting login prompt...')
            _handle_prompts(console, self.username, self.password, prompt)
            logging.info('Console: Ready!')
        except:
            console.close()
            raise

        return console

    def migrate(self, console_address=None, timeout=20):
        def migrate_complete():
            cmd = 'info migrate'
            res = self.qmp('human-monitor-command', command_line=cmd)
            if 'completed' in res['return']:
                logging.info("Migration successful")
                return True
            elif 'failed' in res['return']:
                raise QEMUMigrateError("Migration of %s failed" % self)
            return False

        port = self.ports.find_free_port()
        newvm = _VM(self.qemu_dst_bin, self._arch, username=self.username,
                    password=self.password)
        newvm.args = self.args
        newvm.args.extend(['-incoming', 'tcp:0:%s' % port])
        newvm.username = self.username
        newvm.password = self.password

        newvm.launch(console_address)
        cmd = 'migrate -d tcp:0:%s' % port
        self.qmp('human-monitor-command', command_line=cmd)
        mig_result = wait.wait_for(migrate_complete, timeout=timeout,
                                   text='Waiting for migration to complete')

        if mig_result is None:
            raise QEMUMigrateError("Migration of %s did not complete after "
                                   "%s s" % (self.name, timeout))

        return newvm

    def add_image(self, path, username=None, password=None, cloudinit=False,
                  snapshot=True, extra=None):
        """
        Adds the '-drive' command line option and its parameters to
        the Qemu VM

        :param path: Image path (i.e. /var/lib/images/guestos.qcow2)
        :param username: The username to log into the Guest OS with
        :param password: The password to log into the Guest OS with
        :param cloudinit: Whether the cloudinit cdrom will be attached to
                          the image
        :param snapshot: Whether the parameter snapshot=on will be used
        :param extra: Extra parameters to the -drive option
        """
        file_option = 'file=%s' % path
        for item in self.args:
            if file_option in item:
                logging.error('Image %s already present', path)
                return

        if extra is not None:
            file_option += ',%s' % extra

        if snapshot:
            file_option += ',snapshot=on'

        self.args.extend(['-drive', file_option])

        if username is not None:
            self.username = username

        if password is not None:
            self.password = password

        if cloudinit:
            self._cloudinit()

    def _cloudinit(self):
        """
        Creates a CDROM Iso Image with the required cloudinit files
        (meta-data and user-data) to make the initial Cloud Image
        configuration, attaching the CDROM to the VM.
        """
        try:
            geniso_bin = utils_path.find_command('genisoimage')
        except:
            raise QEMUCloudinitError('Command not found (genisoimage)')

        data_dir = tempfile.mkdtemp()

        metadata_path = os.path.join(data_dir, 'meta-data')
        metadata_content = ("instance-id: %s\n"
                            "local-hostname: %s\n" % (self.name, self.name))
        with open(metadata_path, 'w') as metadata_file:
            metadata_file.write(metadata_content)

        userdata_path = os.path.join(data_dir, 'user-data')
        userdata_content = ("#cloud-config\n"
                            "password: %s\n"
                            "ssh_pwauth: True\n"
                            "chpasswd: { expire: False }\n"
                            "system_info:\n"
                            "    default_user:\n"
                            "        name: %s\n" %
                            (self.password, self.username))

        with open(userdata_path, 'w') as userdata_file:
            userdata_file.write(userdata_content)

        iso_path = os.path.join(data_dir, 'cdrom.iso')
        process.run("%s -output %s -volid cidata -joliet -rock %s %s" %
                    (geniso_bin, iso_path, metadata_path, userdata_path))

        self.args.extend(['-cdrom', iso_path])

class QemuTest(Test):

    def __init__(self, methodName=None, name=None, params=None,
                 base_logdir=None, job=None, runner_queue=None):
        super(QemuTest, self).__init__(methodName=methodName, name=name,
                                       params=params, base_logdir=base_logdir,
                                       job=job, runner_queue=runner_queue)
        self.vm = _VM(qemu_bin=self.params.get('qemu_bin'),
                      arch=self.params.get('arch'),
                      qemu_dst_bin=self.params.get('qemu_dst_bin'),
                      username=self.params.get('image_user',
                                               default='avocado'),
                      password=self.params.get('image_pass',
                                               default='avocado'))

        machine_type = self.params.get('machine_type')
        machine_accel = self.params.get('machine_accel')
        machine_kvm_type = self.params.get('machine_kvm_type')
        machine = ""
        if machine_type is not None:
            machine += "%s," % machine_type
        if machine_accel is not None:
            machine += "accel=%s," % machine_accel
        if machine_kvm_type is not None:
            machine += "kvm-type=%s," % machine_kvm_type
        if machine:
            self.vm.args.extend(['-machine', machine])
