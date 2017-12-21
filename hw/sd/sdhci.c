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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "sysemu/dma.h"
#include "qemu/timer.h"
#include "qemu/bitops.h"
#include "hw/sd/sdhci.h"
#include "sdhci-internal.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "trace.h"

#define TYPE_SDHCI_BUS "sdhci-bus"
#define SDHCI_BUS(obj) OBJECT_CHECK(SDBus, (obj), TYPE_SDHCI_BUS)

#define MASKED_WRITE(reg, mask, val)  (reg = (reg & (mask)) | (val))

static void sdhci_check_capab_freq_range(SDHCIState *s, const char *desc,
                                         uint8_t freq, Error **errp)
{
    switch (freq) {
    case 0:
    case 10 ... 63:
        break;
        error_setg(errp, "SD %s clock frequency can have value"
                   "in range 0-63 only", desc);
        return;
    }
}

/* Default SD/MMC host controller features information, which will be
 * presented in CAPABILITIES register of generic SD host controller at reset. */
static void sdhci_init_capareg(SDHCIState *s, Error **errp)
{
    uint64_t capareg = 0;
    uint32_t val;

    switch (s->spec_version) {
    /* fallback */
    case 2:
        capareg = FIELD_DP64(capareg, SDHC_CAPAB, ADMA1, s->cap.adma1);
        capareg = FIELD_DP64(capareg, SDHC_CAPAB, ADMA2, s->cap.adma2);
        /* 64-bit System Bus Support */
        capareg = FIELD_DP64(capareg, SDHC_CAPAB, BUS64BIT, s->cap.bus64);

    /* fallback */
    case 1:
        sdhci_check_capab_freq_range(s, "Timeout", s->cap.timeout_clk_freq,
                                     errp);
        capareg = FIELD_DP64(capareg, SDHC_CAPAB, TOCLKFREQ,
                             s->cap.timeout_clk_freq);
        sdhci_check_capab_freq_range(s, "Base", s->cap.base_clk_freq_mhz, errp);
        capareg = FIELD_DP64(capareg, SDHC_CAPAB, BASECLKFREQ,
                             s->cap.base_clk_freq_mhz);
        capareg = FIELD_DP64(capareg, SDHC_CAPAB, TOUNIT,
                             s->cap.timeout_clk_in_mhz);

        val = ctz32(s->cap.max_blk_len >> 9);
        if (val >= 0b11) {
            error_setg(errp, "block size can be 512, 1024 or 2048 only");
            return;
        }
        capareg = FIELD_DP64(capareg, SDHC_CAPAB, MAXBLOCKLENGTH, val);

        capareg = FIELD_DP64(capareg, SDHC_CAPAB, HIGHSPEED, s->cap.high_speed);
        capareg = FIELD_DP64(capareg, SDHC_CAPAB, SDMA, s->cap.sdma);
        capareg = FIELD_DP64(capareg, SDHC_CAPAB, SUSPRESUME, s->cap.suspend);
        capareg = FIELD_DP64(capareg, SDHC_CAPAB, V33, s->cap.v33);
        capareg = FIELD_DP64(capareg, SDHC_CAPAB, V30, s->cap.v30);
        capareg = FIELD_DP64(capareg, SDHC_CAPAB, V18, s->cap.v18);
        break;

    default:
        error_setg(errp, "Unsupported spec version: %u", s->spec_version);
    }
    s->capareg = capareg;
}

static uint8_t sdhci_slotint(SDHCIState *s)
{
    return (s->norintsts & s->norintsigen) || (s->errintsts & s->errintsigen) ||
         ((s->norintsts & SDHC_NIS_INSERT) && (s->wakcon & SDHC_WKUP_ON_INS)) ||
         ((s->norintsts & SDHC_NIS_REMOVE) && (s->wakcon & SDHC_WKUP_ON_RMV));
}

static inline void sdhci_update_irq(SDHCIState *s)
{
    qemu_set_irq(s->irq, sdhci_slotint(s));
}

static void sdhci_raise_insertion_irq(void *opaque)
{
    SDHCIState *s = (SDHCIState *)opaque;

    if (s->norintsts & SDHC_NIS_REMOVE) {
        timer_mod(s->insert_timer,
                       qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + SDHC_INSERTION_DELAY);
    } else {
        s->prnsts = 0x1ff0000;
        if (s->norintstsen & SDHC_NISEN_INSERT) {
            s->norintsts |= SDHC_NIS_INSERT;
        }
        sdhci_update_irq(s);
    }
}

static void sdhci_set_inserted(DeviceState *dev, bool level)
{
    SDHCIState *s = (SDHCIState *)dev;

    trace_sdhci_set_inserted(level ? "insert" : "eject");
    if ((s->norintsts & SDHC_NIS_REMOVE) && level) {
        /* Give target some time to notice card ejection */
        timer_mod(s->insert_timer,
                       qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + SDHC_INSERTION_DELAY);
    } else {
        if (level) {
            s->prnsts = 0x1ff0000;
            if (s->norintstsen & SDHC_NISEN_INSERT) {
                s->norintsts |= SDHC_NIS_INSERT;
            }
        } else {
            s->prnsts = 0x1fa0000;
            s->pwrcon &= ~SDHC_POWER_ON;
            s->clkcon &= ~SDHC_CLOCK_SDCLK_EN;
            if (s->norintstsen & SDHC_NISEN_REMOVE) {
                s->norintsts |= SDHC_NIS_REMOVE;
            }
        }
        sdhci_update_irq(s);
    }
}

static void sdhci_set_readonly(DeviceState *dev, bool level)
{
    SDHCIState *s = (SDHCIState *)dev;

    /* Write enabled */
    s->prnsts = FIELD_DP32(s->prnsts, SDHC_PRNSTS, WRITE_PROTECT, level);
}

static void sdhci_reset(SDHCIState *s)
{
    DeviceState *dev = DEVICE(s);

    timer_del(s->insert_timer);
    timer_del(s->transfer_timer);

    /* Set all registers to 0. Capabilities/Version registers are not cleared
     * and assumed to always preserve their value, given to them during
     * initialization */
    memset(&s->sdmasysad, 0, (uintptr_t)&s->capareg - (uintptr_t)&s->sdmasysad);

    /* Reset other state based on current card insertion/readonly status */
    sdhci_set_inserted(dev, sdbus_get_inserted(&s->sdbus));
    sdhci_set_readonly(dev, sdbus_get_readonly(&s->sdbus));

    s->data_count = 0;
    s->stopped_state = sdhc_not_stopped;
    s->pending_insert_state = false;
}

static void sdhci_poweron_reset(DeviceState *dev)
{
    /* QOM (ie power-on) reset. This is identical to reset
     * commanded via device register apart from handling of the
     * 'pending insert on powerup' quirk.
     */
    SDHCIState *s = (SDHCIState *)dev;

    sdhci_reset(s);

    if (s->pending_insert_quirk) {
        s->pending_insert_state = true;
    }
}

static void sdhci_led_handler(void *opaque, int line, int level)
{
    SDHCIState *s = (SDHCIState *)opaque;

    if (s->access_led_level != level) {
        trace_sdhci_led(level);
        s->access_led_level = level;
    }
}

static void sdhci_data_transfer(void *opaque);

static void sdhci_send_command(SDHCIState *s)
{
    SDRequest request;
    uint8_t response[16];
    int rlen;

    s->errintsts = 0;
    s->acmd12errsts = 0;
    request.cmd = s->cmdreg >> 8;
    request.arg = s->argument;

    trace_sdhci_send_command(request.cmd, request.arg);
    rlen = sdbus_do_command(&s->sdbus, &request, response);

    if (s->cmdreg & SDHC_CMD_RESPONSE) {
        if (rlen == 4) {
            s->rspreg[0] = (response[0] << 24) | (response[1] << 16) |
                           (response[2] << 8)  |  response[3];
            s->rspreg[1] = s->rspreg[2] = s->rspreg[3] = 0;
            trace_sdhci_response4(s->rspreg[0]);
        } else if (rlen == 16) {
            s->rspreg[0] = (response[11] << 24) | (response[12] << 16) |
                           (response[13] << 8) |  response[14];
            s->rspreg[1] = (response[7] << 24) | (response[8] << 16) |
                           (response[9] << 8)  |  response[10];
            s->rspreg[2] = (response[3] << 24) | (response[4] << 16) |
                           (response[5] << 8)  |  response[6];
            s->rspreg[3] = (response[0] << 16) | (response[1] << 8) |
                            response[2];
            trace_sdhci_response16(s->rspreg[3], s->rspreg[2],
                                   s->rspreg[1], s->rspreg[0]);
        } else {
            trace_sdhci_error("timeout waiting for command response");
            if (s->errintstsen & SDHC_EISEN_CMDTIMEOUT) {
                s->errintsts |= SDHC_EIS_CMDTIMEOUT;
                s->norintsts |= SDHC_NIS_ERR;
            }
        }

        if ((s->norintstsen & SDHC_NISEN_TRSCMP) &&
            (s->cmdreg & SDHC_CMD_RESPONSE) == SDHC_CMD_RSP_WITH_BUSY) {
            s->norintsts |= SDHC_NIS_TRSCMP;
        }
    }

    if (s->norintstsen & SDHC_NISEN_CMDCMP) {
        s->norintsts |= SDHC_NIS_CMDCMP;
    }

    sdhci_update_irq(s);

    if (s->blksize && (s->cmdreg & SDHC_CMD_DATA_PRESENT)) {
        s->data_count = 0;
        sdhci_data_transfer(s);
    }
}

static void sdhci_end_transfer(SDHCIState *s)
{
    /* Automatically send CMD12 to stop transfer if AutoCMD12 enabled */
    if ((s->trnmod & SDHC_TRNS_ACMD12) != 0) {
        SDRequest request;
        uint8_t response[16];

        request.cmd = 0x0C;
        request.arg = 0;
        trace_sdhci_end_transfer(request.cmd, request.arg);
        sdbus_do_command(&s->sdbus, &request, response);
        /* Auto CMD12 response goes to the upper Response register */
        s->rspreg[3] = (response[0] << 24) | (response[1] << 16) |
                (response[2] << 8) | response[3];
    }

    s->prnsts &= ~(SDHC_DOING_READ | SDHC_DOING_WRITE |
            SDHC_DAT_LINE_ACTIVE | SDHC_DATA_INHIBIT |
            SDHC_SPACE_AVAILABLE | SDHC_DATA_AVAILABLE);

    if (s->norintstsen & SDHC_NISEN_TRSCMP) {
        s->norintsts |= SDHC_NIS_TRSCMP;
    }

    sdhci_update_irq(s);
}

/*
 * Programmed i/o data transfer
 */

/* Fill host controller's read buffer with BLKSIZE bytes of data from card */
static void sdhci_read_block_from_card(SDHCIState *s)
{
    int index = 0;

    if ((s->trnmod & SDHC_TRNS_MULTI) &&
            (s->trnmod & SDHC_TRNS_BLK_CNT_EN) && (s->blkcnt == 0)) {
        return;
    }

    for (index = 0; index < (s->blksize & 0x0fff); index++) {
        s->fifo_buffer[index] = sdbus_read_data(&s->sdbus);
    }

    /* New data now available for READ through Buffer Port Register */
    s->prnsts |= SDHC_DATA_AVAILABLE;
    if (s->norintstsen & SDHC_NISEN_RBUFRDY) {
        s->norintsts |= SDHC_NIS_RBUFRDY;
    }

    /* Clear DAT line active status if that was the last block */
    if ((s->trnmod & SDHC_TRNS_MULTI) == 0 ||
            ((s->trnmod & SDHC_TRNS_MULTI) && s->blkcnt == 1)) {
        s->prnsts &= ~SDHC_DAT_LINE_ACTIVE;
    }

    /* If stop at block gap request was set and it's not the last block of
     * data - generate Block Event interrupt */
    if (s->stopped_state == sdhc_gap_read && (s->trnmod & SDHC_TRNS_MULTI) &&
            s->blkcnt != 1)    {
        s->prnsts &= ~SDHC_DAT_LINE_ACTIVE;
        if (s->norintstsen & SDHC_EISEN_BLKGAP) {
            s->norintsts |= SDHC_EIS_BLKGAP;
        }
    }

    sdhci_update_irq(s);
}

/* Read @size byte of data from host controller @s BUFFER DATA PORT register */
static uint32_t sdhci_read_dataport(SDHCIState *s, unsigned size)
{
    uint32_t value = 0;
    int i;

    /* first check that a valid data exists in host controller input buffer */
    if ((s->prnsts & SDHC_DATA_AVAILABLE) == 0) {
        trace_sdhci_error("read from empty buffer");
        return 0;
    }

    for (i = 0; i < size; i++) {
        value |= s->fifo_buffer[s->data_count] << i * 8;
        s->data_count++;
        /* check if we've read all valid data (blksize bytes) from buffer */
        if ((s->data_count) >= (s->blksize & 0x0fff)) {
            trace_sdhci_read_dataport(s->data_count);
            s->prnsts &= ~SDHC_DATA_AVAILABLE; /* no more data in a buffer */
            s->data_count = 0;  /* next buff read must start at position [0] */

            if (s->trnmod & SDHC_TRNS_BLK_CNT_EN) {
                s->blkcnt--;
            }

            /* if that was the last block of data */
            if ((s->trnmod & SDHC_TRNS_MULTI) == 0 ||
                ((s->trnmod & SDHC_TRNS_BLK_CNT_EN) && (s->blkcnt == 0)) ||
                 /* stop at gap request */
                (s->stopped_state == sdhc_gap_read &&
                 !(s->prnsts & SDHC_DAT_LINE_ACTIVE))) {
                sdhci_end_transfer(s);
            } else { /* if there are more data, read next block from card */
                sdhci_read_block_from_card(s);
            }
            break;
        }
    }

    return value;
}

/* Write data from host controller FIFO to card */
static void sdhci_write_block_to_card(SDHCIState *s)
{
    int index = 0;

    if (s->prnsts & SDHC_SPACE_AVAILABLE) {
        if (s->norintstsen & SDHC_NISEN_WBUFRDY) {
            s->norintsts |= SDHC_NIS_WBUFRDY;
        }
        sdhci_update_irq(s);
        return;
    }

    if (s->trnmod & SDHC_TRNS_BLK_CNT_EN) {
        if (s->blkcnt == 0) {
            return;
        } else {
            s->blkcnt--;
        }
    }

    for (index = 0; index < (s->blksize & 0x0fff); index++) {
        sdbus_write_data(&s->sdbus, s->fifo_buffer[index]);
    }

    /* Next data can be written through BUFFER DATORT register */
    s->prnsts |= SDHC_SPACE_AVAILABLE;

    /* Finish transfer if that was the last block of data */
    if ((s->trnmod & SDHC_TRNS_MULTI) == 0 ||
            ((s->trnmod & SDHC_TRNS_MULTI) &&
            (s->trnmod & SDHC_TRNS_BLK_CNT_EN) && (s->blkcnt == 0))) {
        sdhci_end_transfer(s);
    } else if (s->norintstsen & SDHC_NISEN_WBUFRDY) {
        s->norintsts |= SDHC_NIS_WBUFRDY;
    }

    /* Generate Block Gap Event if requested and if not the last block */
    if (s->stopped_state == sdhc_gap_write && (s->trnmod & SDHC_TRNS_MULTI) &&
            s->blkcnt > 0) {
        s->prnsts &= ~SDHC_DOING_WRITE;
        if (s->norintstsen & SDHC_EISEN_BLKGAP) {
            s->norintsts |= SDHC_EIS_BLKGAP;
        }
        sdhci_end_transfer(s);
    }

    sdhci_update_irq(s);
}

/* Write @size bytes of @value data to host controller @s Buffer Data Port
 * register */
static void sdhci_write_dataport(SDHCIState *s, uint32_t value, unsigned size)
{
    unsigned i;

    /* Check that there is free space left in a buffer */
    if (!(s->prnsts & SDHC_SPACE_AVAILABLE)) {
        trace_sdhci_error("Can't write to data buffer: buffer full");
        return;
    }

    for (i = 0; i < size; i++) {
        s->fifo_buffer[s->data_count] = value & 0xFF;
        s->data_count++;
        value >>= 8;
        if (s->data_count >= (s->blksize & 0x0fff)) {
            trace_sdhci_write_dataport(s->data_count);
            s->data_count = 0;
            s->prnsts &= ~SDHC_SPACE_AVAILABLE;
            if (s->prnsts & SDHC_DOING_WRITE) {
                sdhci_write_block_to_card(s);
            }
        }
    }
}

/*
 * Single DMA data transfer
 */

/* Multi block SDMA transfer */
static void sdhci_sdma_transfer_multi_blocks(SDHCIState *s)
{
    bool page_aligned = false;
    unsigned int n, begin;
    const uint16_t block_size = s->blksize & 0x0fff;
    uint32_t boundary_chk = 1 << (((s->blksize & 0xf000) >> 12) + 12);
    uint32_t boundary_count = boundary_chk - (s->sdmasysad % boundary_chk);

    if (!(s->trnmod & SDHC_TRNS_BLK_CNT_EN) || !s->blkcnt) {
        qemu_log_mask(LOG_UNIMP, "infinite transfer is not supported\n");
        return;
    }

    /* XXX: Some sd/mmc drivers (for example, u-boot-slp) do not account for
     * possible stop at page boundary if initial address is not page aligned,
     * allow them to work properly */
    if ((s->sdmasysad % boundary_chk) == 0) {
        page_aligned = true;
    }

    if (s->trnmod & SDHC_TRNS_READ) {
        s->prnsts |= SDHC_DOING_READ | SDHC_DATA_INHIBIT |
                SDHC_DAT_LINE_ACTIVE;
        while (s->blkcnt) {
            if (s->data_count == 0) {
                for (n = 0; n < block_size; n++) {
                    s->fifo_buffer[n] = sdbus_read_data(&s->sdbus);
                }
            }
            begin = s->data_count;
            if (((boundary_count + begin) < block_size) && page_aligned) {
                s->data_count = boundary_count + begin;
                boundary_count = 0;
             } else {
                s->data_count = block_size;
                boundary_count -= block_size - begin;
                if (s->trnmod & SDHC_TRNS_BLK_CNT_EN) {
                    s->blkcnt--;
                }
            }
            dma_memory_write(&s->dma_as, s->sdmasysad,
                             &s->fifo_buffer[begin], s->data_count - begin);
            s->sdmasysad += s->data_count - begin;
            if (s->data_count == block_size) {
                s->data_count = 0;
            }
            if (page_aligned && boundary_count == 0) {
                break;
            }
        }
    } else {
        s->prnsts |= SDHC_DOING_WRITE | SDHC_DATA_INHIBIT |
                SDHC_DAT_LINE_ACTIVE;
        while (s->blkcnt) {
            begin = s->data_count;
            if (((boundary_count + begin) < block_size) && page_aligned) {
                s->data_count = boundary_count + begin;
                boundary_count = 0;
             } else {
                s->data_count = block_size;
                boundary_count -= block_size - begin;
            }
            dma_memory_read(&s->dma_as, s->sdmasysad,
                            &s->fifo_buffer[begin], s->data_count - begin);
            s->sdmasysad += s->data_count - begin;
            if (s->data_count == block_size) {
                for (n = 0; n < block_size; n++) {
                    sdbus_write_data(&s->sdbus, s->fifo_buffer[n]);
                }
                s->data_count = 0;
                if (s->trnmod & SDHC_TRNS_BLK_CNT_EN) {
                    s->blkcnt--;
                }
            }
            if (page_aligned && boundary_count == 0) {
                break;
            }
        }
    }

    if (s->blkcnt == 0) {
        sdhci_end_transfer(s);
    } else {
        if (s->norintstsen & SDHC_NISEN_DMA) {
            s->norintsts |= SDHC_NIS_DMA;
        }
        sdhci_update_irq(s);
    }
}

/* single block SDMA transfer */
static void sdhci_sdma_transfer_single_block(SDHCIState *s)
{
    int n;
    uint32_t datacnt = s->blksize & 0x0fff;

    if (s->trnmod & SDHC_TRNS_READ) {
        for (n = 0; n < datacnt; n++) {
            s->fifo_buffer[n] = sdbus_read_data(&s->sdbus);
        }
        dma_memory_write(&s->dma_as, s->sdmasysad, s->fifo_buffer, datacnt);
    } else {
        dma_memory_read(&s->dma_as, s->sdmasysad, s->fifo_buffer, datacnt);
        for (n = 0; n < datacnt; n++) {
            sdbus_write_data(&s->sdbus, s->fifo_buffer[n]);
        }
    }
    s->blkcnt--;

    sdhci_end_transfer(s);
}

typedef struct ADMADescr {
    hwaddr addr;
    uint16_t length;
    uint8_t attr;
    uint8_t incr;
} ADMADescr;

static void get_adma_description(SDHCIState *s, ADMADescr *dscr)
{
    uint32_t adma1 = 0;
    uint64_t adma2 = 0;
    hwaddr entry_addr = (hwaddr)s->admasysaddr;
    switch (SDHC_DMA_TYPE(s->hostctl)) {
    case SDHC_CTRL_ADMA2_32:
        dma_memory_read(&s->dma_as, entry_addr, (uint8_t *)&adma2,
                        sizeof(adma2));
        adma2 = le64_to_cpu(adma2);
        /* The spec does not specify endianness of descriptor table.
         * We currently assume that it is LE.
         */
        dscr->addr = (hwaddr)extract64(adma2, 32, 32) & ~0x3ull;
        dscr->length = (uint16_t)extract64(adma2, 16, 16);
        dscr->attr = (uint8_t)extract64(adma2, 0, 7);
        dscr->incr = 8;
        break;
    case SDHC_CTRL_ADMA1_32:
        dma_memory_read(&s->dma_as, entry_addr, (uint8_t *)&adma1,
                        sizeof(adma1));
        adma1 = le32_to_cpu(adma1);
        dscr->addr = (hwaddr)(adma1 & 0xFFFFF000);
        dscr->attr = (uint8_t)extract32(adma1, 0, 7);
        dscr->incr = 4;
        if ((dscr->attr & SDHC_ADMA_ATTR_ACT_MASK) == SDHC_ADMA_ATTR_SET_LEN) {
            dscr->length = (uint16_t)extract32(adma1, 12, 16);
        } else {
            dscr->length = 4096;
        }
        break;
    case SDHC_CTRL_ADMA2_64:
        dma_memory_read(&s->dma_as, entry_addr,
                        (uint8_t *)(&dscr->attr), 1);
        dma_memory_read(&s->dma_as, entry_addr + 2,
                        (uint8_t *)(&dscr->length), 2);
        dscr->length = le16_to_cpu(dscr->length);
        dma_memory_read(&s->dma_as, entry_addr + 4,
                        (uint8_t *)(&dscr->addr), 8);
        dscr->attr = le64_to_cpu(dscr->attr);
        dscr->attr &= 0xfffffff8;
        dscr->incr = 12;
        break;
    }
}

/* Advanced DMA data transfer */

static void sdhci_do_adma(SDHCIState *s)
{
    unsigned int n, begin, length;
    const uint16_t block_size = s->blksize & 0x0fff;
    ADMADescr dscr = {};
    int i;

    for (i = 0; i < SDHC_ADMA_DESCS_PER_DELAY; ++i) {
        s->admaerr &= ~SDHC_ADMAERR_LENGTH_MISMATCH;

        get_adma_description(s, &dscr);
        trace_sdhci_adma_loop(dscr.addr, dscr.length, dscr.attr);

        if ((dscr.attr & SDHC_ADMA_ATTR_VALID) == 0) {
            /* Indicate that error occurred in ST_FDS state */
            s->admaerr &= ~SDHC_ADMAERR_STATE_MASK;
            s->admaerr |= SDHC_ADMAERR_STATE_ST_FDS;

            /* Generate ADMA error interrupt */
            if (s->errintstsen & SDHC_EISEN_ADMAERR) {
                s->errintsts |= SDHC_EIS_ADMAERR;
                s->norintsts |= SDHC_NIS_ERR;
            }

            sdhci_update_irq(s);
            return;
        }

        length = dscr.length ? dscr.length : 65536;

        switch (dscr.attr & SDHC_ADMA_ATTR_ACT_MASK) {
        case SDHC_ADMA_ATTR_ACT_TRAN:  /* data transfer */

            if (s->trnmod & SDHC_TRNS_READ) {
                while (length) {
                    if (s->data_count == 0) {
                        for (n = 0; n < block_size; n++) {
                            s->fifo_buffer[n] = sdbus_read_data(&s->sdbus);
                        }
                    }
                    begin = s->data_count;
                    if ((length + begin) < block_size) {
                        s->data_count = length + begin;
                        length = 0;
                     } else {
                        s->data_count = block_size;
                        length -= block_size - begin;
                    }
                    dma_memory_write(&s->dma_as, dscr.addr,
                                     &s->fifo_buffer[begin],
                                     s->data_count - begin);
                    dscr.addr += s->data_count - begin;
                    if (s->data_count == block_size) {
                        s->data_count = 0;
                        if (s->trnmod & SDHC_TRNS_BLK_CNT_EN) {
                            s->blkcnt--;
                            if (s->blkcnt == 0) {
                                break;
                            }
                        }
                    }
                }
            } else {
                while (length) {
                    begin = s->data_count;
                    if ((length + begin) < block_size) {
                        s->data_count = length + begin;
                        length = 0;
                     } else {
                        s->data_count = block_size;
                        length -= block_size - begin;
                    }
                    dma_memory_read(&s->dma_as, dscr.addr,
                                    &s->fifo_buffer[begin],
                                    s->data_count - begin);
                    dscr.addr += s->data_count - begin;
                    if (s->data_count == block_size) {
                        for (n = 0; n < block_size; n++) {
                            sdbus_write_data(&s->sdbus, s->fifo_buffer[n]);
                        }
                        s->data_count = 0;
                        if (s->trnmod & SDHC_TRNS_BLK_CNT_EN) {
                            s->blkcnt--;
                            if (s->blkcnt == 0) {
                                break;
                            }
                        }
                    }
                }
            }
            s->admasysaddr += dscr.incr;
            break;
        case SDHC_ADMA_ATTR_ACT_LINK:   /* link to next descriptor table */
            s->admasysaddr = dscr.addr;
            trace_sdhci_adma("link", s->admasysaddr);
            break;
        default:
            s->admasysaddr += dscr.incr;
            break;
        }

        if (dscr.attr & SDHC_ADMA_ATTR_INT) {
            trace_sdhci_adma("interrupt", s->admasysaddr);
            if (s->norintstsen & SDHC_NISEN_DMA) {
                s->norintsts |= SDHC_NIS_DMA;
            }

            sdhci_update_irq(s);
        }

        /* ADMA transfer terminates if blkcnt == 0 or by END attribute */
        if (((s->trnmod & SDHC_TRNS_BLK_CNT_EN) &&
                    (s->blkcnt == 0)) || (dscr.attr & SDHC_ADMA_ATTR_END)) {
            trace_sdhci_adma_transfer_completed();
            if (length || ((dscr.attr & SDHC_ADMA_ATTR_END) &&
                (s->trnmod & SDHC_TRNS_BLK_CNT_EN) &&
                s->blkcnt != 0)) {
                trace_sdhci_error("SD/MMC host ADMA length mismatch");
                s->admaerr |= SDHC_ADMAERR_LENGTH_MISMATCH |
                        SDHC_ADMAERR_STATE_ST_TFR;
                if (s->errintstsen & SDHC_EISEN_ADMAERR) {
                    trace_sdhci_error("Set ADMA error flag");
                    s->errintsts |= SDHC_EIS_ADMAERR;
                    s->norintsts |= SDHC_NIS_ERR;
                }

                sdhci_update_irq(s);
            }
            sdhci_end_transfer(s);
            return;
        }

    }

    /* we have unfinished business - reschedule to continue ADMA */
    timer_mod(s->transfer_timer,
                   qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + SDHC_TRANSFER_DELAY);
}

/* Perform data transfer according to controller configuration */

static void sdhci_data_transfer(void *opaque)
{
    SDHCIState *s = (SDHCIState *)opaque;

    if (s->trnmod & SDHC_TRNS_DMA) {
        switch (SDHC_DMA_TYPE(s->hostctl)) {
        case SDHC_CTRL_SDMA:
            if ((s->blkcnt == 1) || !(s->trnmod & SDHC_TRNS_MULTI)) {
                sdhci_sdma_transfer_single_block(s);
            } else {
                sdhci_sdma_transfer_multi_blocks(s);
            }

            break;
        case SDHC_CTRL_ADMA1_32:
            if (!(s->capareg & R_SDHC_CAPAB_ADMA1_MASK)) {
                trace_sdhci_error("ADMA1 not supported");
                break;
            }

            sdhci_do_adma(s);
            break;
        case SDHC_CTRL_ADMA2_32:
            if (!(s->capareg & R_SDHC_CAPAB_ADMA2_MASK)) {
                trace_sdhci_error("ADMA2 not supported");
                break;
            }

            sdhci_do_adma(s);
            break;
        case SDHC_CTRL_ADMA2_64:
            if (!(s->capareg & R_SDHC_CAPAB_ADMA2_MASK) ||
                    !(s->capareg & R_SDHC_CAPAB_BUS64BIT_MASK)) {
                trace_sdhci_error("64 bit ADMA not supported");
                break;
            }

            sdhci_do_adma(s);
            break;
        default:
            trace_sdhci_error("Unsupported DMA type");
            break;
        }
    } else {
        if ((s->trnmod & SDHC_TRNS_READ) && sdbus_data_ready(&s->sdbus)) {
            s->prnsts |= SDHC_DOING_READ | SDHC_DATA_INHIBIT |
                    SDHC_DAT_LINE_ACTIVE;
            sdhci_read_block_from_card(s);
        } else {
            s->prnsts |= SDHC_DOING_WRITE | SDHC_DAT_LINE_ACTIVE |
                    SDHC_SPACE_AVAILABLE | SDHC_DATA_INHIBIT;
            sdhci_write_block_to_card(s);
        }
    }
}

static bool sdhci_can_issue_command(SDHCIState *s)
{
    if (!SDHC_CLOCK_IS_ON(s->clkcon) ||
        (((s->prnsts & SDHC_DATA_INHIBIT) || s->stopped_state) &&
        ((s->cmdreg & SDHC_CMD_DATA_PRESENT) ||
        ((s->cmdreg & SDHC_CMD_RESPONSE) == SDHC_CMD_RSP_WITH_BUSY &&
        !(SDHC_COMMAND_TYPE(s->cmdreg) == SDHC_CMD_ABORT))))) {
        return false;
    }

    return true;
}

/* The Buffer Data Port register must be accessed in sequential and
 * continuous manner */
static inline bool
sdhci_buff_access_is_sequential(SDHCIState *s, unsigned byte_num)
{
    if ((s->data_count & 0x3) != byte_num) {
        trace_sdhci_error("Non-sequential access to Buffer Data Port register"
                          "is prohibited\n");
        return false;
    }
    return true;
}

static uint64_t sdhci_read(void *opaque, hwaddr offset, unsigned size)
{
    SDHCIState *s = (SDHCIState *)opaque;
    uint32_t ret = 0;

    switch (offset & ~0x3) {
    case SDHC_SYSAD:
        ret = s->sdmasysad;
        break;
    case SDHC_BLKSIZE:
        ret = s->blksize | (s->blkcnt << 16);
        break;
    case SDHC_ARGUMENT:
        ret = s->argument;
        break;
    case SDHC_TRNMOD:
        ret = s->trnmod | (s->cmdreg << 16);
        break;
    case SDHC_RSPREG0 ... SDHC_RSPREG3:
        ret = s->rspreg[((offset & ~0x3) - SDHC_RSPREG0) >> 2];
        break;
    case  SDHC_BDATA:
        if (sdhci_buff_access_is_sequential(s, offset - SDHC_BDATA)) {
            ret = sdhci_read_dataport(s, size);
            trace_sdhci_access("read", size, offset, "->", ret, ret);
            return ret;
        }
        break;
    case SDHC_PRNSTS:
        ret = s->prnsts;
        break;
    case SDHC_HOSTCTL:
        ret = s->hostctl | (s->pwrcon << 8) | (s->blkgap << 16) |
              (s->wakcon << 24);
        break;
    case SDHC_CLKCON:
        ret = s->clkcon | (s->timeoutcon << 16);
        break;
    case SDHC_NORINTSTS:
        ret = s->norintsts | (s->errintsts << 16);
        break;
    case SDHC_NORINTSTSEN:
        ret = s->norintstsen | (s->errintstsen << 16);
        break;
    case SDHC_NORINTSIGEN:
        ret = s->norintsigen | (s->errintsigen << 16);
        break;
    case SDHC_ACMD12ERRSTS:
        ret = s->acmd12errsts;
        break;
    case SDHC_CAPAB:
        ret = (uint32_t)s->capareg;
        break;
    case SDHC_CAPAB + 4:
        ret = (uint32_t)(s->capareg >> 32);
        break;
    case SDHC_MAXCURR:
        ret = (uint32_t)s->maxcurr;
        break;
    case SDHC_MAXCURR + 4:
        ret = (uint32_t)(s->maxcurr >> 32);
        break;
    case SDHC_ADMAERR:
        ret =  s->admaerr;
        break;
    case SDHC_ADMASYSADDR:
        ret = (uint32_t)s->admasysaddr;
        break;
    case SDHC_ADMASYSADDR + 4:
        ret = (uint32_t)(s->admasysaddr >> 32);
        break;
    case SDHC_SLOT_INT_STATUS:
        ret = (s->version << 16) | sdhci_slotint(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "SDHC rd_%ub @0x%02" HWADDR_PRIx " "
                      "not implemented\n", size, offset);
        break;
    }

    ret >>= (offset & 0x3) * 8;
    ret &= (1ULL << (size * 8)) - 1;
    trace_sdhci_access("read", size, offset, "->", ret, ret);
    return ret;
}

static inline void sdhci_blkgap_write(SDHCIState *s, uint8_t value)
{
    if ((value & SDHC_STOP_AT_GAP_REQ) && (s->blkgap & SDHC_STOP_AT_GAP_REQ)) {
        return;
    }
    s->blkgap = value & SDHC_STOP_AT_GAP_REQ;

    if ((value & SDHC_CONTINUE_REQ) && s->stopped_state &&
            (s->blkgap & SDHC_STOP_AT_GAP_REQ) == 0) {
        if (s->stopped_state == sdhc_gap_read) {
            s->prnsts |= SDHC_DAT_LINE_ACTIVE | SDHC_DOING_READ;
            sdhci_read_block_from_card(s);
        } else {
            s->prnsts |= SDHC_DAT_LINE_ACTIVE | SDHC_DOING_WRITE;
            sdhci_write_block_to_card(s);
        }
        s->stopped_state = sdhc_not_stopped;
    } else if (!s->stopped_state && (value & SDHC_STOP_AT_GAP_REQ)) {
        if (s->prnsts & SDHC_DOING_READ) {
            s->stopped_state = sdhc_gap_read;
        } else if (s->prnsts & SDHC_DOING_WRITE) {
            s->stopped_state = sdhc_gap_write;
        }
    }
}

static inline void sdhci_reset_write(SDHCIState *s, uint8_t value)
{
    switch (value) {
    case SDHC_RESET_ALL:
        sdhci_reset(s);
        break;
    case SDHC_RESET_CMD:
        s->prnsts &= ~SDHC_CMD_INHIBIT;
        s->norintsts &= ~SDHC_NIS_CMDCMP;
        break;
    case SDHC_RESET_DATA:
        s->data_count = 0;
        s->prnsts &= ~(SDHC_SPACE_AVAILABLE | SDHC_DATA_AVAILABLE |
                SDHC_DOING_READ | SDHC_DOING_WRITE |
                SDHC_DATA_INHIBIT | SDHC_DAT_LINE_ACTIVE);
        s->blkgap &= ~(SDHC_STOP_AT_GAP_REQ | SDHC_CONTINUE_REQ);
        s->stopped_state = sdhc_not_stopped;
        s->norintsts &= ~(SDHC_NIS_WBUFRDY | SDHC_NIS_RBUFRDY |
                SDHC_NIS_DMA | SDHC_NIS_TRSCMP | SDHC_NIS_BLKGAP);
        break;
    }
}

static void
sdhci_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    SDHCIState *s = (SDHCIState *)opaque;
    unsigned shift =  8 * (offset & 0x3);
    uint32_t mask = ~(((1ULL << (size * 8)) - 1) << shift);
    uint32_t value = val;
    value <<= shift;

    switch (offset & ~0x3) {
    case SDHC_SYSAD:
        s->sdmasysad = (s->sdmasysad & mask) | value;
        MASKED_WRITE(s->sdmasysad, mask, value);
        /* Writing to last byte of sdmasysad might trigger transfer */
        if (!(mask & 0xFF000000) && TRANSFERRING_DATA(s->prnsts) && s->blkcnt &&
                s->blksize && SDHC_DMA_TYPE(s->hostctl) == SDHC_CTRL_SDMA) {
            if (s->trnmod & SDHC_TRNS_MULTI) {
                sdhci_sdma_transfer_multi_blocks(s);
            } else {
                sdhci_sdma_transfer_single_block(s);
            }
        }
        break;
    case SDHC_BLKSIZE:
        if (!TRANSFERRING_DATA(s->prnsts)) {
            MASKED_WRITE(s->blksize, mask, value);
            MASKED_WRITE(s->blkcnt, mask >> 16, value >> 16);
        }

        /* Limit block size to the maximum buffer size */
        if (extract32(s->blksize, 0, 12) > s->buf_maxsz) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Size 0x%x is larger than " \
                          "the maximum buffer 0x%x", __func__, s->blksize,
                          s->buf_maxsz);

            s->blksize = deposit32(s->blksize, 0, 12, s->buf_maxsz);
        }

        break;
    case SDHC_ARGUMENT:
        MASKED_WRITE(s->argument, mask, value);
        break;
    case SDHC_TRNMOD:
        /* DMA can be enabled only if it is supported as indicated by
         * capabilities register */
        if (!(s->capareg & R_SDHC_CAPAB_SDMA_MASK)) {
            value &= ~SDHC_TRNS_DMA;
        }
        MASKED_WRITE(s->trnmod, mask, value & SDHC_TRNMOD_MASK);
        MASKED_WRITE(s->cmdreg, mask >> 16, value >> 16);

        /* Writing to the upper byte of CMDREG triggers SD command generation */
        if ((mask & 0xFF000000) || !sdhci_can_issue_command(s)) {
            break;
        }

        sdhci_send_command(s);
        break;
    case  SDHC_BDATA:
        if (sdhci_buff_access_is_sequential(s, offset - SDHC_BDATA)) {
            sdhci_write_dataport(s, value >> shift, size);
        }
        break;
    case SDHC_HOSTCTL:
        if (!(mask & 0xFF0000)) {
            sdhci_blkgap_write(s, value >> 16);
        }
        MASKED_WRITE(s->hostctl, mask, value);
        MASKED_WRITE(s->pwrcon, mask >> 8, value >> 8);
        MASKED_WRITE(s->wakcon, mask >> 24, value >> 24);
        if (!(s->prnsts & SDHC_CARD_PRESENT) || ((s->pwrcon >> 1) & 0x7) < 5 ||
                !(s->capareg & (1 << (31 - ((s->pwrcon >> 1) & 0x7))))) {
            s->pwrcon &= ~SDHC_POWER_ON;
        }
        qemu_set_irq(s->access_led, s->hostctl & 1);
        break;
    case SDHC_CLKCON:
        if (!(mask & 0xFF000000)) {
            sdhci_reset_write(s, value >> 24);
        }
        MASKED_WRITE(s->clkcon, mask, value);
        MASKED_WRITE(s->timeoutcon, mask >> 16, value >> 16);
        if (s->clkcon & SDHC_CLOCK_INT_EN) {
            s->clkcon |= SDHC_CLOCK_INT_STABLE;
        } else {
            s->clkcon &= ~SDHC_CLOCK_INT_STABLE;
        }
        break;
    case SDHC_NORINTSTS:
        if (s->norintstsen & SDHC_NISEN_CARDINT) {
            value &= ~SDHC_NIS_CARDINT;
        }
        s->norintsts &= mask | ~value;
        s->errintsts &= (mask >> 16) | ~(value >> 16);
        if (s->errintsts) {
            s->norintsts |= SDHC_NIS_ERR;
        } else {
            s->norintsts &= ~SDHC_NIS_ERR;
        }
        sdhci_update_irq(s);
        break;
    case SDHC_NORINTSTSEN:
        MASKED_WRITE(s->norintstsen, mask, value);
        MASKED_WRITE(s->errintstsen, mask >> 16, value >> 16);
        s->norintsts &= s->norintstsen;
        s->errintsts &= s->errintstsen;
        if (s->errintsts) {
            s->norintsts |= SDHC_NIS_ERR;
        } else {
            s->norintsts &= ~SDHC_NIS_ERR;
        }
        /* Quirk for Raspberry Pi: pending card insert interrupt
         * appears when first enabled after power on */
        if ((s->norintstsen & SDHC_NISEN_INSERT) && s->pending_insert_state) {
            assert(s->pending_insert_quirk);
            s->norintsts |= SDHC_NIS_INSERT;
            s->pending_insert_state = false;
        }
        sdhci_update_irq(s);
        break;
    case SDHC_NORINTSIGEN:
        MASKED_WRITE(s->norintsigen, mask, value);
        MASKED_WRITE(s->errintsigen, mask >> 16, value >> 16);
        sdhci_update_irq(s);
        break;
    case SDHC_ADMAERR:
        MASKED_WRITE(s->admaerr, mask, value);
        break;
    case SDHC_ADMASYSADDR:
        s->admasysaddr = deposit64(s->admasysaddr, 0, 32, value);
        break;
    case SDHC_ADMASYSADDR + 4:
        s->admasysaddr = deposit64(s->admasysaddr, 32, 32, value);
        break;
    case SDHC_FEAER:
        s->acmd12errsts |= value;
        s->errintsts |= (value >> 16) & s->errintstsen;
        if (s->acmd12errsts) {
            s->errintsts |= SDHC_EIS_CMD12ERR;
        }
        if (s->errintsts) {
            s->norintsts |= SDHC_NIS_ERR;
        }
        sdhci_update_irq(s);
        break;
    case SDHC_ACMD12ERRSTS:
        MASKED_WRITE(s->acmd12errsts, mask, value);
        break;

    case SDHC_CAPAB:
    case SDHC_CAPAB + 4:
    case SDHC_MAXCURR:
    case SDHC_MAXCURR + 4:
        qemu_log_mask(LOG_GUEST_ERROR, "SDHC wr_%ub @0x%02" HWADDR_PRIx
                      " <- 0x%08x read-only\n", size, offset, value >> shift);
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "SDHC wr_%ub @0x%02" HWADDR_PRIx " <- 0x%08x "
                      "not implemented\n", size, offset, value >> shift);
        break;
    }
    trace_sdhci_access("write", size, offset, "<-",
                       value >> shift, value >> shift);
}

static const MemoryRegionOps sdhci_mmio_ops = {
    .read = sdhci_read,
    .write = sdhci_write,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static inline unsigned int sdhci_get_fifolen(SDHCIState *s)
{
    return 1 << (9 + FIELD_EX32(s->capareg, SDHC_CAPAB, MAXBLOCKLENGTH));
}

static void sdhci_init_readonly_registers(SDHCIState *s, Error **errp)
{
    if (s->spec_version < 1) {
        error_setg(errp, "spec version invalid");
        return;
    }
    s->version = (SDHC_HCVER_VENDOR << 8) | (s->spec_version - 1);

    if (!s->dma) {
        s->cap.sdma = s->cap.adma1 = s->cap.adma2 = false;
    }

    if (s->capareg == UINT64_MAX) {
        sdhci_init_capareg(s, errp);
    }
}

static void sdhci_initfn(SDHCIState *s)
{
    qbus_create_inplace(&s->sdbus, sizeof(s->sdbus),
                        TYPE_SDHCI_BUS, DEVICE(s), "sd-bus");

    s->access_led = qemu_allocate_irq(sdhci_led_handler, s, 0);
    s->insert_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sdhci_raise_insertion_irq, s);
    s->transfer_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sdhci_data_transfer, s);
}

static void sdhci_common_realize(SDHCIState *s, Error **errp)
{
    sdhci_init_readonly_registers(s, errp);
    if (errp && *errp) {
        return;
    }
    s->buf_maxsz = sdhci_get_fifolen(s);
    s->fifo_buffer = g_malloc0(s->buf_maxsz);

    memory_region_init_io(&s->iomem, OBJECT(s), &sdhci_mmio_ops, s, "sdhci",
                          SDHC_REGISTERS_MAP_SIZE);

    /* use system_memory() if property "dma-memory" not set */
    address_space_init(&s->dma_as,
                       s->dma_mr ? s->dma_mr : get_system_memory(),
                       "sdhci-dma");
}

static void sdhci_common_unrealize(SDHCIState *s, Error **errp)
{
    if (s->dma_mr) {
        address_space_destroy(&s->dma_as);
        object_unparent(OBJECT(&s->dma_mr));
    }

    /* This function is expected to be called only once for each class:
     * - SysBus:    via DeviceClass->unrealize(),
     * - PCI:       via PCIDeviceClass->exit().
     * However to avoid double-free and/or use-after-free we still nullify
     * this variable (better safe than sorry!). */
    g_free(s->fifo_buffer);
    s->fifo_buffer = NULL;
}

static void sdhci_uninitfn(SDHCIState *s)
{
    timer_del(s->insert_timer);
    timer_free(s->insert_timer);
    timer_del(s->transfer_timer);
    timer_free(s->transfer_timer);

    qemu_free_irq(s->access_led);
}

static bool sdhci_pending_insert_vmstate_needed(void *opaque)
{
    SDHCIState *s = opaque;

    return s->pending_insert_state;
}

static const VMStateDescription sdhci_pending_insert_vmstate = {
    .name = "sdhci/pending-insert",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = sdhci_pending_insert_vmstate_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(pending_insert_state, SDHCIState),
        VMSTATE_END_OF_LIST()
    },
};

const VMStateDescription sdhci_vmstate = {
    .name = "sdhci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(sdmasysad, SDHCIState),
        VMSTATE_UINT16(blksize, SDHCIState),
        VMSTATE_UINT16(blkcnt, SDHCIState),
        VMSTATE_UINT32(argument, SDHCIState),
        VMSTATE_UINT16(trnmod, SDHCIState),
        VMSTATE_UINT16(cmdreg, SDHCIState),
        VMSTATE_UINT32_ARRAY(rspreg, SDHCIState, 4),
        VMSTATE_UINT32(prnsts, SDHCIState),
        VMSTATE_UINT8(hostctl, SDHCIState),
        VMSTATE_UINT8(pwrcon, SDHCIState),
        VMSTATE_UINT8(blkgap, SDHCIState),
        VMSTATE_UINT8(wakcon, SDHCIState),
        VMSTATE_UINT16(clkcon, SDHCIState),
        VMSTATE_UINT8(timeoutcon, SDHCIState),
        VMSTATE_UINT8(admaerr, SDHCIState),
        VMSTATE_UINT16(norintsts, SDHCIState),
        VMSTATE_UINT16(errintsts, SDHCIState),
        VMSTATE_UINT16(norintstsen, SDHCIState),
        VMSTATE_UINT16(errintstsen, SDHCIState),
        VMSTATE_UINT16(norintsigen, SDHCIState),
        VMSTATE_UINT16(errintsigen, SDHCIState),
        VMSTATE_UINT16(acmd12errsts, SDHCIState),
        VMSTATE_UINT16(data_count, SDHCIState),
        VMSTATE_UINT64(admasysaddr, SDHCIState),
        VMSTATE_UINT8(stopped_state, SDHCIState),
        VMSTATE_VBUFFER_UINT32(fifo_buffer, SDHCIState, 1, NULL, buf_maxsz),
        VMSTATE_TIMER_PTR(insert_timer, SDHCIState),
        VMSTATE_TIMER_PTR(transfer_timer, SDHCIState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &sdhci_pending_insert_vmstate,
        NULL
    },
};

/* Capabilities registers provide information on supported features of this
 * specific host controller implementation */
static Property sdhci_properties[] = {
    DEFINE_PROP_UINT8("sd-spec-version", SDHCIState, spec_version, 2),

    /* Timeout clock frequency 1-63, 0 - not defined */
    DEFINE_PROP_UINT8("timeout-freq", SDHCIState, cap.timeout_clk_freq, 0),
    /* Timeout clock unit 0 - kHz, 1 - MHz */
    DEFINE_PROP_BOOL("freq-in-mhz", SDHCIState, cap.timeout_clk_in_mhz, true),
    /* Maximum base clock frequency for SD clock in MHz (range 10-63 MHz, 0) */
    DEFINE_PROP_UINT8("clock-freq", SDHCIState, cap.base_clk_freq_mhz, 0),

    /* Maximum host controller R/W buffers size
     * Possible values: 512, 1024, 2048 bytes */
    DEFINE_PROP_UINT16("max-block-length", SDHCIState, cap.max_blk_len, 512),
    /* DMA */
    DEFINE_PROP_BOOL("dma", SDHCIState, dma, true), /* shortcut */
    DEFINE_PROP_BOOL("sdma", SDHCIState, cap.sdma, true),
    DEFINE_PROP_BOOL("adma1", SDHCIState, cap.adma1, false),
    DEFINE_PROP_BOOL("adma2", SDHCIState, cap.adma2, true),
    /* Suspend/resume support */
    DEFINE_PROP_BOOL("suspend", SDHCIState, cap.suspend, false),
    /* High speed support */
    DEFINE_PROP_BOOL("high-speed", SDHCIState, cap.high_speed, true),
    /* Voltage support 3.3/3.0/1.8V */
    DEFINE_PROP_BOOL("3v3", SDHCIState, cap.v33, true),
    DEFINE_PROP_BOOL("3v0", SDHCIState, cap.v30, false),
    DEFINE_PROP_BOOL("1v8", SDHCIState, cap.v18, false),

    DEFINE_PROP_BOOL("64bit", SDHCIState, cap.bus64, false),

    /* capareg: deprecated */
    DEFINE_PROP_UINT64("capareg", SDHCIState, capareg, UINT64_MAX),

    DEFINE_PROP_UINT64("maxcurr", SDHCIState, maxcurr, 0),
    DEFINE_PROP_BOOL("pending-insert-quirk", SDHCIState, pending_insert_quirk,
                     false),
    DEFINE_PROP_LINK("dma-memory", SDHCIState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void sdhci_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->vmsd = &sdhci_vmstate;
    dc->props = sdhci_properties;
    dc->reset = sdhci_poweron_reset;
}

/* --- qdev PCI --- */

static void sdhci_pci_realize(PCIDevice *dev, Error **errp)
{
    SDHCIState *s = PCI_SDHCI(dev);

    sdhci_initfn(s);
    sdhci_common_realize(s, errp);
    if (errp && *errp) {
        return;
    }

    dev->config[PCI_CLASS_PROG] = 0x01; /* Standard Host supported DMA */
    dev->config[PCI_INTERRUPT_PIN] = 0x01; /* interrupt pin A */
    s->irq = pci_allocate_irq(dev);
    pci_register_bar(dev, 0, 0, &s->iomem);
}

static void sdhci_pci_exit(PCIDevice *dev)
{
    SDHCIState *s = PCI_SDHCI(dev);

    sdhci_common_unrealize(s, &error_abort);
    sdhci_uninitfn(s);
}

static void sdhci_pci_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = sdhci_pci_realize;
    k->exit = sdhci_pci_exit;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_SDHCI;
    k->class_id = PCI_CLASS_SYSTEM_SDHCI;

    sdhci_common_class_init(klass, data);
}

static const TypeInfo sdhci_pci_info = {
    .name = TYPE_PCI_SDHCI,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(SDHCIState),
    .class_init = sdhci_pci_class_init,
};

/* --- qdev SysBus --- */

static void sdhci_sysbus_init(Object *obj)
{
    SDHCIState *s = SYSBUS_SDHCI(obj);

    sdhci_initfn(s);
}

static void sdhci_sysbus_finalize(Object *obj)
{
    SDHCIState *s = SYSBUS_SDHCI(obj);
    sdhci_uninitfn(s);
}

static void sdhci_sysbus_realize(DeviceState *dev, Error ** errp)
{
    SDHCIState *s = SYSBUS_SDHCI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sdhci_common_realize(s, errp);
    if (errp && *errp) {
        return;
    }

    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void sdhci_sysbus_unrealize(DeviceState *dev, Error **errp)
{
    SDHCIState *s = SYSBUS_SDHCI(dev);

    sdhci_common_unrealize(s, &error_abort);
}

static void sdhci_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sdhci_sysbus_realize;
    dc->unrealize = sdhci_sysbus_unrealize;

    sdhci_common_class_init(klass, data);
}

static const TypeInfo sdhci_sysbus_info = {
    .name = TYPE_SYSBUS_SDHCI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SDHCIState),
    .instance_init = sdhci_sysbus_init,
    .instance_finalize = sdhci_sysbus_finalize,
    .class_init = sdhci_sysbus_class_init,
};

/* --- qdev bus master --- */

static void sdhci_bus_class_init(ObjectClass *klass, void *data)
{
    SDBusClass *sbc = SD_BUS_CLASS(klass);

    sbc->set_inserted = sdhci_set_inserted;
    sbc->set_readonly = sdhci_set_readonly;
}

static const TypeInfo sdhci_bus_info = {
    .name = TYPE_SDHCI_BUS,
    .parent = TYPE_SD_BUS,
    .instance_size = sizeof(SDBus),
    .class_init = sdhci_bus_class_init,
};

static void sdhci_register_types(void)
{
    type_register_static(&sdhci_pci_info);
    type_register_static(&sdhci_sysbus_info);
    type_register_static(&sdhci_bus_info);
}

type_init(sdhci_register_types)
