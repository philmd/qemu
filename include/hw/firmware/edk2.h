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

#ifndef HW_FIRMWARE_EDK2_H
#define HW_FIRMWARE_EDK2_H

#include "hw/nvram/fw_cfg.h"

/**
 * edk2_add_host_crypto_policy:
 * @s: fw_cfg device being modified
 *
 * Add new NAMED fw_cfg items containing the host crypto policy.
 */
void edk2_add_host_crypto_policy(FWCfgState *s);

#endif /* HW_FIRMWARE_EDK2_H */
