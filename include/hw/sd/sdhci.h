/*
 * SD Association Host Standard Specification v2.0 controller emulation
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Mitsyanko Igor <i.mitsyanko@samsung.com>
 * Peter A.G. Crosthwaite <peter.crosthwaite@petalogix.com>
 *
 * Based on MMC controller for Samsung S5PC1xx-based board emulation
 * by Alexey Merkulov and Vladimir Monakhov.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU _General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SDHCI_H
#define SDHCI_H

#include "qemu-common.h"
#include "hw/pci/pci.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"

/* SD/MMC host controller state */
typedef struct SDHCIState {
    /*< private >*/
    union {
        PCIDevice pcidev;
        SysBusDevice busdev;
    };

    /*< public >*/
    SDBus sdbus;
    MemoryRegion iomem;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;

    QEMUTimer *insert_timer;       /* timer for 'changing' sd card. */
    QEMUTimer *transfer_timer;
    qemu_irq irq;
    qemu_irq access_led;
    int access_led_level;

    /* Registers cleared on reset */
    /* 0x00 */
    uint32_t sdmasysad;    /* SDMA System Address register */
    uint16_t blksize;      /* Host DMA Buff Boundary and Transfer BlkSize Reg */
    uint16_t blkcnt;       /* Blocks count for current transfer */
    /* 0x08 */
    uint32_t argument;     /* Command Argument Register */
    uint16_t trnmod;       /* Transfer Mode Setting Register */
    uint16_t cmdreg;       /* Command Register */
    /* 0x10 */
    uint32_t rspreg[4];    /* Response Registers 0-3 */
    /* 0x20 */
    /* Buffer Data Port Register - virtual access point to R and W buffers */
    uint32_t prnsts;       /* Present State Register */
    /* 0x28 */
    uint8_t  hostctl;      /* Host Control Register */
    uint8_t  pwrcon;       /* Power control Register */
    uint8_t  blkgap;       /* Block Gap Control Register */
    uint8_t  wakcon;       /* WakeUp Control Register */
    uint16_t clkcon;       /* Clock control Register */
    uint8_t  timeoutcon;   /* Timeout Control Register */
    uint8_t  admaerr;      /* ADMA Error Status Register */
    /* 0x30 */
    uint16_t norintsts;    /* Normal Interrupt Status Register */
    uint16_t errintsts;    /* Error Interrupt Status Register */
    uint16_t norintstsen;  /* Normal Interrupt Status Enable Register */
    uint16_t errintstsen;  /* Error Interrupt Status Enable Register */
    uint16_t norintsigen;  /* Normal Interrupt Signal Enable Register */
    uint16_t errintsigen;  /* Error Interrupt Signal Enable Register */
    uint16_t acmd12errsts; /* Auto CMD12 error status register */
    /* 0x50 */
    /* Force Event Auto CMD12 Error Interrupt Reg - write only */
    /* Force Event Error Interrupt Register- write only */
    /* 0x58 */
    uint64_t admasysaddr;  /* ADMA System Address Register */

    /* Read-only registers */
    /* 0x40 */
    uint64_t capareg;      /* Capabilities Register */
    /* 0x48 */
    uint64_t maxcurr;      /* Maximum Current Capabilities Register */
    /* 0xfe */
    uint16_t version;      /* Host Controller Version Register */

    uint8_t  *fifo_buffer; /* SD host i/o FIFO buffer */
    uint32_t buf_maxsz;
    uint16_t data_count;   /* current element in FIFO buffer */
    uint8_t  stopped_state;/* Current SDHC state */
    bool     pending_insert_state;
    /* Configurable properties */
    bool pending_insert_quirk; /* Quirk for Raspberry Pi card insert int */
    bool dma; /* shortcut for sdma + adma* */
    uint8_t spec_version;
    struct {
        /* v1 */
        uint8_t timeout_clk_freq, base_clk_freq_mhz;
        bool timeout_clk_in_mhz;
        uint16_t max_blk_len;
        bool suspend;
        bool high_speed;
        bool sdma;
        bool v33, v30, v18;
        /* v2 */
        bool adma1, adma2;
        bool bus64;
    } cap;
} SDHCIState;

#define TYPE_PCI_SDHCI "sdhci-pci"
#define PCI_SDHCI(obj) OBJECT_CHECK(SDHCIState, (obj), TYPE_PCI_SDHCI)

#define TYPE_SYSBUS_SDHCI "generic-sdhci"
#define SYSBUS_SDHCI(obj)                               \
     OBJECT_CHECK(SDHCIState, (obj), TYPE_SYSBUS_SDHCI)

#endif /* SDHCI_H */
