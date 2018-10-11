/*
 * UEFI EDK2 Support
 *
 * Copyright (c) 2018 Red Hat Inc.
 *
 * Author:
 *  Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/firmware/edk2.h"
#include "trace.h"

/* the acceptable ciphersuites and the preferred order
 * from the host-side crypto policy */
#define TRUSTED_CIPHER_SUITES_PATH \
        "/etc/crypto-policies/back-ends/edk2.config"

/* the trusted CA certificates configured on the host side */
#define TRUSTED_CERTIFICATES_DATABASE_PATH \
        "/etc/pki/ca-trust/extracted/edk2/cacerts.bin"

void edk2_add_host_crypto_policy(FWCfgState *s)
{
    fw_cfg_add_file_from_host(s, "etc/edk2/https/ciphers",
                              TRUSTED_CIPHER_SUITES_PATH, NULL);

    fw_cfg_add_file_from_host(s, "etc/edk2/https/cacerts",
                              TRUSTED_CERTIFICATES_DATABASE_PATH, NULL);
}
