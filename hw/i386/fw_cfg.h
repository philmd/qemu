/*
 * QEMU fw_cfg helpers (X86 specific)
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_I386_FW_CFG_H
#define HW_I386_FW_CFG_H

#include "hw/boards.h"
#include "hw/nvram/fw_cfg.h"

#define FW_CFG_ACPI_TABLES      (FW_CFG_ARCH_LOCAL + 0)
#define FW_CFG_SMBIOS_ENTRIES   (FW_CFG_ARCH_LOCAL + 1)
#define FW_CFG_IRQ0_OVERRIDE    (FW_CFG_ARCH_LOCAL + 2)
#define FW_CFG_E820_TABLE       (FW_CFG_ARCH_LOCAL + 3)
#define FW_CFG_HPET             (FW_CFG_ARCH_LOCAL + 4)

void fw_cfg_build_smbios(MachineState *ms, FWCfgState *fw_cfg);
void fw_cfg_build_feature_control(MachineState *ms, FWCfgState *fw_cfg);
FWCfgState *fw_cfg_init(MachineState *ms, const CPUArchIdList *cpus,
                        uint16_t boot_cpus, uint16_t apic_id_limit);

#endif
