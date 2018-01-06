/*
 * SD Memory Card emulation as defined in the "SD Memory Card Physical
 * layer specification, Version 1.10."
 *
 * Copyright (c) 2006 Andrzej Zaborowski  <balrog@zabor.org>
 * Copyright (c) 2007 CodeSourcery
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "hw/qdev.h"
#include "hw/hw.h"
#include "hw/registerfields.h"
#include "sysemu/block-backend.h"
#include "hw/sd/sd.h"
#include "hw/sd/sdcard_legacy.h"
#include "qapi/error.h"
#include "qemu/bitmap.h"
#include "qemu/cutils.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "trace.h"

//#define DEBUG_SD 1

#ifdef DEBUG_SD
#define DPRINTF(fmt, ...) \
do { fprintf(stderr, "SD: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif

#define ACMD41_ENQUIRY_MASK     0x00ffffff

#define SDCARD_CMD_MAX 64

typedef enum {
    SD_PHY_SPEC_VER_2_00 = 200,
    SD_PHY_SPEC_VER_3_01 = 301, /* not yet supported */
} sd_phy_spec_ver_t;

typedef enum {
    MMC_SPEC_VER_2_2    = 202,  /* not well supported */
    /* e.MMC */
    MMC_SPEC_VER_4_51   = 451,
    MMC_SPEC_VER_5_1    = 501,  /* not supported */
} mmc_phy_spec_ver_t;

typedef enum {
    sd_capacity_unknown,
    sd_capacity_sdsc,           /* not well supported */
    sd_capacity_sdhc,
    sd_capacity_sdxc,           /* not yet supported */
} sd_card_capacity_t;

typedef enum {
    sd_r0 = 0,    /* no response */
    sd_r1,        /* normal response command */
    sd_r2_i,      /* CID register */
    sd_r2_s,      /* CSD register */
    sd_r3,        /* OCR register */
    sd_r6 = 6,    /* Published RCA response */
    sd_r7,        /* Operating voltage */
    sd_r1b = -1,
    sd_illegal = -2,
} sd_rsp_type_t;

enum SDCardModes {
    sd_inactive,
    sd_card_identification_mode,
    sd_data_transfer_mode,
};

enum SDCardStates {
    sd_inactive_state = -1,
    sd_idle_state = 0,
    sd_ready_state,
    sd_identification_state,
    sd_standby_state,
    sd_transfer_state,
    sd_sendingdata_state,
    sd_receivingdata_state,
    sd_programming_state,
    sd_disconnect_state,
};

struct SDState {
    DeviceState parent_obj;

    /* SD Memory Card Registers */
    uint32_t ocr;
    uint8_t scr[8];
    uint8_t cid[16];
    uint8_t csd[16];
    uint16_t rca;
    uint32_t card_status;
    uint8_t sd_status[64];

    uint32_t vhs;
    bool wp_switch;
    unsigned long *wp_groups;
    int32_t wpgrps_size;
    uint64_t size;
    uint32_t blk_len;
    uint32_t multi_blk_cnt;
    uint32_t erase_start;
    uint32_t erase_end;
    uint8_t pwd[16];
    uint32_t pwd_len;
    uint8_t function_group[6];

    int spec_version;
    uint32_t bus_protocol;
    sd_card_capacity_t capacity;

    uint32_t mode;    /* current card mode, one of SDCardModes */
    int32_t state;    /* current card state, one of SDCardStates */
    uint8_t current_cmd;
    /* True if we will handle the next command as an ACMD. Note that this does
     * *not* track the APP_CMD status bit!
     */
    bool expecting_acmd;
    uint32_t blk_written;
    uint64_t data_start;
    uint32_t data_offset;
    uint8_t data[512];
    qemu_irq readonly_cb;
    qemu_irq inserted_cb;
    BlockBackend *blk;
    QEMUTimer *ocr_power_timer;

    bool enable;
    const char *proto_name;
};

static const char *sd_protocol_name(sd_bus_protocol_t protocol)
{
    switch (protocol) {
    case PROTO_SD:
        return "SD";
    case PROTO_SPI:
        return "SPI";
    case PROTO_MMC:
        return "MMC";
    default:
        g_assert_not_reached();
    }
}

static const char *sd_state_name(enum SDCardStates state)
{
    static const char *state_name[] = {
        [sd_idle_state]             = "idle",
        [sd_ready_state]            = "ready",
        [sd_identification_state]   = "identification",
        [sd_standby_state]          = "standby",
        [sd_transfer_state]         = "transfer",
        [sd_sendingdata_state]      = "sendingdata",
        [sd_receivingdata_state]    = "receivingdata",
        [sd_programming_state]      = "programming",
        [sd_disconnect_state]       = "disconnect",
    };
    if (state == sd_inactive_state) {
        return "inactive";
    }
    return state_name[state];
}

static const char *sd_capacity(sd_card_capacity_t capacity)
{
    static const char *capacity_name[] = {
        [sd_capacity_unknown]   = "UNKN",
        [sd_capacity_sdsc]      = "SDSC",
        [sd_capacity_sdhc]      = "SDHC",
        [sd_capacity_sdxc]      = "SDXC",
    };
    return capacity_name[capacity];
}

static const char *sd_cmd_abbreviation(uint8_t cmd)
{
    static const char *cmd_abbrev[SDCARD_CMD_MAX] = {
         [0]    = "GO_IDLE_STATE",
         [2]    = "ALL_SEND_CID",            [3]    = "SEND_RELATIVE_ADDR",
         [4]    = "SET_DSR",                 [5]    = "IO_SEND_OP_COND",
         [6]    = "SWITCH_FUNC",             [7]    = "SELECT/DESELECT_CARD",
         [8]    = "SEND_IF_COND",            [9]    = "SEND_CSD",
        [10]    = "SEND_CID",               [11]    = "VOLTAGE_SWITCH",
        [12]    = "STOP_TRANSMISSION",      [13]    = "SEND_STATUS",
                                            [15]    = "GO_INACTIVE_STATE",
        [16]    = "SET_BLOCKLEN",           [17]    = "READ_SINGLE_BLOCK",
        [18]    = "READ_MULTIPLE_BLOCK",    [19]    = "SEND_TUNING_BLOCK",
        [20]    = "SPEED_CLASS_CONTROL",    [21]    = "DPS_spec",
                                            [23]    = "SET_BLOCK_COUNT",
        [24]    = "WRITE_BLOCK",            [25]    = "WRITE_MULTIPLE_BLOCK",
        [26]    = "MANUF_RSVD",             [27]    = "PROGRAM_CSD",
        [28]    = "SET_WRITE_PROT",         [29]    = "CLR_WRITE_PROT",
        [30]    = "SEND_WRITE_PROT",
        [32]    = "ERASE_WR_BLK_START",     [33]    = "ERASE_WR_BLK_END",
        [34]    = "SW_FUNC_RSVD",           [35]    = "SW_FUNC_RSVD",
        [36]    = "SW_FUNC_RSVD",           [37]    = "SW_FUNC_RSVD",
        [38]    = "ERASE",
        [40]    = "DPS_spec",
        [42]    = "LOCK_UNLOCK",            [43]    = "Q_MANAGEMENT",
        [44]    = "Q_TASK_INFO_A",          [45]    = "Q_TASK_INFO_B",
        [46]    = "Q_RD_TASK",              [47]    = "Q_WR_TASK",
        [48]    = "READ_EXTR_SINGLE",       [49]    = "WRITE_EXTR_SINGLE",
        [50]    = "SW_FUNC_RSVD", /* FIXME */
        [52]    = "IO_RW_DIRECT",           [53]    = "IO_RW_EXTENDED",
        [54]    = "SDIO_RSVD",              [55]    = "APP_CMD",
        [56]    = "GEN_CMD",                [57]    = "SW_FUNC_RSVD",
        [58]    = "READ_EXTR_MULTI",        [59]    = "WRITE_EXTR_MULTI",
        [60]    = "MANUF_RSVD",             [61]    = "MANUF_RSVD",
        [62]    = "MANUF_RSVD",             [63]    = "MANUF_RSVD",
    };
    return cmd_abbrev[cmd] ? cmd_abbrev[cmd] : "UNKNOWN_CMD";
}

static const char *sd_acmd_abbreviation(uint8_t cmd)
{
    static const char *acmd_abbrev[SDCARD_CMD_MAX] = {
         [6] = "SET_BUS_WIDTH",
        [13] = "SD_STATUS",
        [14] = "DPS_spec",                  [15] = "DPS_spec",
        [16] = "DPS_spec",
        [18] = "SECU_spec",
        [22] = "SEND_NUM_WR_BLOCKS",        [23] = "SET_WR_BLK_ERASE_COUNT",
        [41] = "SD_SEND_OP_COND",
        [42] = "SET_CLR_CARD_DETECT",
        [51] = "SEND_SCR",
        [52] = "SECU_spec",                 [53] = "SECU_spec",
        [54] = "SECU_spec",
        [56] = "SECU_spec",                 [57] = "SECU_spec",
        [58] = "SECU_spec",                 [59] = "SECU_spec",
    };

    return acmd_abbrev[cmd] ? acmd_abbrev[cmd] : "UNKNOWN_ACMD";
}

static void sd_set_mode(SDState *sd)
{
    switch (sd->state) {
    case sd_inactive_state:
        sd->mode = sd_inactive;
        break;

    case sd_idle_state:
    case sd_ready_state:
    case sd_identification_state:
        sd->mode = sd_card_identification_mode;
        break;

    case sd_standby_state:
    case sd_transfer_state:
    case sd_sendingdata_state:
    case sd_receivingdata_state:
    case sd_programming_state:
    case sd_disconnect_state:
        sd->mode = sd_data_transfer_mode;
        break;
    }
}

static const sd_cmd_type_t sd_cmd_type[SDCARD_CMD_MAX] = {
    sd_bc,   sd_none, sd_bcr,  sd_bcr,  sd_none, sd_none, sd_none, sd_ac,
    sd_bcr,  sd_ac,   sd_ac,   sd_adtc, sd_ac,   sd_ac,   sd_none, sd_ac,
    /* 16 */
    sd_ac,   sd_adtc, sd_adtc, sd_none, sd_none, sd_none, sd_none, sd_none,
    sd_adtc, sd_adtc, sd_adtc, sd_adtc, sd_ac,   sd_ac,   sd_adtc, sd_none,
    /* 32 */
    sd_ac,   sd_ac,   sd_none, sd_none, sd_none, sd_none, sd_ac,   sd_none,
    sd_none, sd_none, sd_bc,   sd_none, sd_none, sd_none, sd_none, sd_none,
    /* 48 */
    sd_none, sd_none, sd_none, sd_none, sd_none, sd_none, sd_none, sd_ac,
    sd_adtc, sd_none, sd_none, sd_none, sd_none, sd_none, sd_none, sd_none,
};

#define BIT_2_4     (BIT(2) | BIT(4))
#define BIT_2_4_7   (BIT(2) | BIT(4) | BIT(7))
#define BIT_SECU    BIT(30) /* SD Specifications Part3 Security Specification */
#define BIT_MANUF   BIT(31) /* reserved for manufacturer */

typedef struct {
    struct {
        uint16_t version;
        uint32_t ccc_mask;
    } sd, spi, mmc;
} sd_cmd_supported_t;

static const sd_cmd_supported_t cmd_supported[SDCARD_CMD_MAX] = {
     /*           SD                  SPI                 eMMC         */
     [0] = {{200, BIT(0)},      {200, BIT(0)},      {451, BIT(0)}       },
     [1] = {{301, BIT(0)},      {200, BIT(0)},      {451, BIT(0)}       },
     [2] = {{200, BIT(0)},      {},                 {451, BIT(0)}       },
     [3] = {{200, BIT(0)},      {},                 {451, BIT(0)}       },
     [4] = {{200, BIT(0)},      {},                 {451, BIT(0)}       },
     [5] = {{200, BIT(9)},      {200, BIT(9)},      {451, BIT(0)}       },
     [6] = {{200, BIT(10)},     {200, BIT(10)},     {451, BIT(0)}       },
     [7] = {{200, BIT(0)},      {},                 {451, BIT(0)}       },
     [8] = {{200, BIT(0)},      {200, BIT(0)},      {451, BIT(0)}       },
     [9] = {{200, BIT(0)},      {200, BIT(0)},      {451, BIT(0)}       },
    [10] = {{200, BIT(0)},      {200, BIT(0)},      {451, BIT(0)}       },
    [12] = {{200, BIT(0)},      {200, BIT(0)},      {451, BIT(0)}       },
    [13] = {{200, BIT(0)},      {200, BIT(0)},      {451, BIT(0)}       },
    [14] = {{200, BIT(0)},      {},                 {451, BIT(0)}       },
    [15] = {{200, BIT(0)},      {},                 {451, BIT(0)}       },
    [16] = {{200, BIT_2_4_7},   {200, BIT_2_4_7},   {451, BIT_2_4_7}    },
    [17] = {{200, BIT(2)},      {200, BIT(2)},      {451, BIT(2)}       },
    [18] = {{200, BIT(2)},      {200, BIT(2)},      {451, BIT(2)}       },
    [19] = {{},                 {},                 {451, BIT(0)}       },
    [21] = {{},                 {},                 {/* HS200 */}       },
    [23] = {{301, BIT_2_4},     {},                 {/*BIT_2_4 ?*/}     },
    [24] = {{200, BIT(4)},      {200, BIT(4)},      {451, BIT(4)}       },
    [25] = {{200, BIT(4)},      {200, BIT(4)},      {451, BIT(4)}       },
    [26] = {{200, BIT_MANUF},   {/*?*/},            {/*?*/}             },
    [27] = {{200, BIT(4)},      {200, BIT(4)},      {451, BIT(4)}       },
    [28] = {{200, BIT(6)},      {200, BIT(6)},      {451, BIT(6)}       },
    [29] = {{200, BIT(6)},      {200, BIT(6)},      {451, BIT(6)}       },
    [30] = {{200, BIT(6)},      {200, BIT(6)},      {451, BIT(6)}       },
    [31] = {{},                 {},                 {451, BIT(6)}       },
    [32] = {{200, BIT(5)},      {200, BIT(5)},      {}                  },
    [33] = {{200, BIT(5)},      {200, BIT(5)},      {}                  },
    [34] = {{200, BIT(10)},     {200, BIT(10)},     {}                  },
    [35] = {{200, BIT(10)},     {200, BIT(10)},     {451, BIT(5)}       },
    [36] = {{200, BIT(10)},     {200, BIT(10)},     {451, BIT(5)}       },
    [37] = {{200, BIT(10)},     {200, BIT(10)},     {},                 },
    [38] = {{200, BIT(5)},      {200, BIT(5)},      {451, BIT(5)}       },
    [39] = {{},                 {},                 {451, BIT(9)}       },
    [40] = {{},                 {},                 {451, BIT(9)}       },
    [41] = {{},                 {},                 {/*451, BIT(7)*/}   },
    [42] = {{200, BIT(7)},      {200, BIT(7)},      {451, BIT(4)}       },
    [44] = {{},                 {},                 {501, BIT(10)}      },
    [45] = {{},                 {},                 {501, BIT(10)}      },
    [46] = {{},                 {},                 {501, BIT(10)}      },
    [47] = {{},                 {},                 {501, BIT(10)}      },
    [48] = {{},                 {},                 {501, BIT(10)}      },
    [49] = {{},                 {},                 {451, BIT(4)}       },
    [50] = {{200, BIT(10)},     {200, BIT(10)},     {}                  },
    [52] = {{200, BIT(9)},      {200, BIT(9)},      {}                  },
    [53] = {{200, BIT(9)},      {200, BIT(9)},      {451, BIT(10)}      },
    [54] = {{/* 2.00 SDIO */},  {/* 2.00 SDIO */},  {451, BIT(10)}      },
    [55] = {{200, BIT(8)},      {200, BIT(8)},      {451, BIT(8)}       },
    [56] = {{200, BIT(8)},      {200, BIT(8)},      {451, BIT(8)}       },
    [57] = {{200, BIT(10)},     {200, BIT(10)},     {}                  },
    [58] = {{301, BIT(0)},      {200, BIT(0)},      {}                  },
    [59] = {{301, BIT(0)},      {200, BIT(0)},      {}                  },
    [60] = {{200, BIT_MANUF},   {/*?*/},            {/*?*/}             },
    [61] = {{200, BIT_MANUF},   {/*?*/},            {/*?*/}             },
    [62] = {{200, BIT_MANUF},   {/*?*/},            {/*?*/}             },
    [63] = {{200, BIT_MANUF},   {/*?*/},            {/*?*/}             },
}, acmd_supported[SDCARD_CMD_MAX] = {
     /*           SD                  SPI                 eMMC         */
     [6] = {{200, BIT(8)},      {},                 {451, BIT(0)}       },
    [13] = {{200, BIT(8)},      {200, BIT(8)},      {451, BIT(0)}       },
    [18] = {{200, BIT_SECU},    {200, BIT_SECU},    {451, BIT(0)}       },
    [22] = {{200, BIT(8)},      {200, BIT(8)},      {451, BIT(4)}       },
    [23] = {{200, BIT(8)},      {200, BIT(8)},      {451, BIT(4)}       },
    [25] = {{200, BIT_SECU},    {200, BIT_SECU},    {451, BIT(0)}       },
    [26] = {{200, BIT_SECU},    {200, BIT_SECU},    {451, BIT(0)}       },
    [38] = {{200, BIT_SECU},    {200, BIT_SECU},    {451, BIT(0)}       },
    [41] = {{200, BIT(8)},      {200, BIT(8)},      {451, BIT(8)}       },
    [42] = {{200, BIT(8)},      {200, BIT(8)},      {451, BIT(0)}       },
    [43] = {{200, BIT_SECU},    {200, BIT_SECU},    {451, BIT(0)}       },
    [44] = {{200, BIT_SECU},    {200, BIT_SECU},    {451, BIT(0)}       },
    [45] = {{200, BIT_SECU},    {200, BIT_SECU},    {451, BIT(0)}       },
    [46] = {{200, BIT_SECU},    {200, BIT_SECU},    {451, BIT(0)}       },
    [47] = {{200, BIT_SECU},    {200, BIT_SECU},    {451, BIT(0)}       },
    [48] = {{200, BIT_SECU},    {200, BIT_SECU},    {451, BIT(0)}       },
    [49] = {{200, BIT_SECU},    {200, BIT_SECU},    {451, BIT(0)}       },
    [51] = {{200, BIT(8)},      {200, BIT(8)},      {451, BIT(8)}       },
};

static const char *spec_version_name(uint16_t spec_version)
{
    switch (spec_version) {
    case SD_PHY_SPEC_VER_2_00:
        return "v2.00";
    case MMC_SPEC_VER_2_2:
        return "v2.2";
    case SD_PHY_SPEC_VER_3_01:
        return "v3.01";
    case MMC_SPEC_VER_4_51:
        return "v4.51";
    case MMC_SPEC_VER_5_1:
        return "v5.1";
    default:
        g_assert_not_reached();
    }
}

static bool cmd_version_supported(SDState *sd, uint8_t cmd, bool is_acmd)
{
    const sd_cmd_supported_t *cmdset = is_acmd ? acmd_supported : cmd_supported;
    uint16_t cmd_version;

    switch (sd->bus_protocol) {
    case PROTO_SD:
        cmd_version = cmdset[cmd].sd.version;
        break;
    case PROTO_SPI:
        cmd_version = cmdset[cmd].spi.version;
        break;
    case PROTO_MMC:
        cmd_version = cmdset[cmd].mmc.version;
        break;
    default:
        g_assert_not_reached();
    }
    if (cmd_version >= sd->spec_version) {
        return true;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Unsupported %s%02u (%s)\n",
                  sd->proto_name, is_acmd ? "ACMD" : "CMD", cmd,
                  spec_version_name(cmd_version));

    return false;
}

static bool cmd_class_supported(SDState *sd, uint8_t cmd, uint8_t class,
                                bool is_acmd)
{
    const sd_cmd_supported_t *cmdset = is_acmd ? acmd_supported : cmd_supported;
    uint32_t cmd_ccc_mask;

    switch (sd->bus_protocol) {
    case PROTO_SD:
        cmd_ccc_mask = cmdset[cmd].sd.ccc_mask;
        break;
    case PROTO_SPI:
        /* class 1, 3 and 9 are not supported in SPI mode */
        cmd_ccc_mask = cmdset[cmd].spi.ccc_mask;
        break;
    case PROTO_MMC:
        cmd_ccc_mask = cmdset[cmd].mmc.ccc_mask;
        break;
    default:
        g_assert_not_reached();
    }
    if (cmd_ccc_mask & BIT(class)) {
        return true;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Unsupported %s%02u (class %d, %s)\n",
                  sd->proto_name, is_acmd ? "ACMD" : "CMD", cmd, class,
                  spec_version_name(sd->spec_version));

    return false;
}

static uint8_t sd_crc7(void *message, size_t width)
{
    int i, bit;
    uint8_t shift_reg = 0x00;
    uint8_t *msg = (uint8_t *) message;

    for (i = 0; i < width; i ++, msg ++)
        for (bit = 7; bit >= 0; bit --) {
            shift_reg <<= 1;
            if ((shift_reg >> 7) ^ ((*msg >> bit) & 1))
                shift_reg ^= 0x89;
        }

    return shift_reg;
}

static uint16_t sd_crc16(void *message, size_t width)
{
    int i, bit;
    uint16_t shift_reg = 0x0000;
    uint16_t *msg = (uint16_t *) message;
    width <<= 1;

    for (i = 0; i < width; i ++, msg ++)
        for (bit = 15; bit >= 0; bit --) {
            shift_reg <<= 1;
            if ((shift_reg >> 15) ^ ((*msg >> bit) & 1))
                shift_reg ^= 0x1011;
        }

    return shift_reg;
}

FIELD(OCR, CARD_CAPACITY,              30, 1); /* 0:SDSC, 1:SDHC/SDXC */
FIELD(OCR, CARD_POWER_UP,              31, 1);

#define OCR_POWER_DELAY_NS      500000 /* 0.5ms */

static void sd_reset_ocr(SDState *sd)
{
    /* All voltages OK, Standard Capacity SD Memory Card, not yet powered up */
    sd->ocr = 0x00ffff00;
}

static void sd_ocr_powerup(void *opaque)
{
    SDState *sd = opaque;

    assert(!FIELD_EX32(sd->ocr, OCR, CARD_POWER_UP));

    /* card power-up OK */
    sd->ocr = FIELD_DP32(sd->ocr, OCR, CARD_POWER_UP, 1);

    if (sd->capacity >= sd_capacity_sdhc) {
        sd->ocr = FIELD_DP32(sd->ocr, OCR, CARD_CAPACITY, 1);
    }
}

static void sd_reset_scr(SDState *sd)
{
    sd->scr[0] = 0x00;		/* SCR Structure */
    sd->scr[1] = 0x2f;		/* SD Security Support */
    sd->scr[2] = 0x00;
    sd->scr[3] = 0x00;
    sd->scr[4] = 0x00;
    sd->scr[5] = 0x00;
    sd->scr[6] = 0x00;
    sd->scr[7] = 0x00;
}

#define MID	0xaa
#define OID	"XY"
#define PNM	"QEMU!"
#define PRV	0x01
#define MDT_YR	2006
#define MDT_MON	2

static void sd_reset_cid(SDState *sd)
{
    sd->cid[0] = MID;		/* Fake card manufacturer ID (MID) */
    sd->cid[1] = OID[0];	/* OEM/Application ID (OID) */
    sd->cid[2] = OID[1];
    sd->cid[3] = PNM[0];	/* Fake product name (PNM) */
    sd->cid[4] = PNM[1];
    sd->cid[5] = PNM[2];
    sd->cid[6] = PNM[3];
    sd->cid[7] = PNM[4];
    sd->cid[8] = PRV;		/* Fake product revision (PRV) */
    sd->cid[9] = 0xde;		/* Fake serial number (PSN) */
    sd->cid[10] = 0xad;
    sd->cid[11] = 0xbe;
    sd->cid[12] = 0xef;
    sd->cid[13] = 0x00 |	/* Manufacture date (MDT) */
        ((MDT_YR - 2000) / 10);
    sd->cid[14] = ((MDT_YR % 10) << 4) | MDT_MON;
    sd->cid[15] = (sd_crc7(sd->cid, 15) << 1) | 1;
}

#define HWBLOCK_SHIFT	9			/* 512 bytes */
#define SECTOR_SHIFT	5			/* 16 kilobytes */
#define WPGROUP_SHIFT	7			/* 2 megs */
#define CMULT_SHIFT	9			/* 512 times HWBLOCK_SIZE */
#define WPGROUP_SIZE	(1 << (HWBLOCK_SHIFT + SECTOR_SHIFT + WPGROUP_SHIFT))

static const uint8_t sd_csd_rw_mask[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0xfe,
};

static void sd_reset_csd(SDState *sd, uint64_t size)
{
    uint32_t csize = (size >> (CMULT_SHIFT + HWBLOCK_SHIFT)) - 1;
    uint32_t sectsize = (1 << (SECTOR_SHIFT + 1)) - 1;
    uint32_t wpsize = (1 << (WPGROUP_SHIFT + 1)) - 1;

    if (size <= 1 * G_BYTE /* FIXME 2GB? */) { /* Standard Capacity SD */
        sd->csd[0] = 0x00;	/* CSD structure */
        sd->csd[1] = 0x26;	/* Data read access-time-1 */
        sd->csd[2] = 0x00;	/* Data read access-time-2 */
        sd->csd[3] = 0x5a;	/* Max. data transfer rate */
        sd->csd[4] = 0x5f;	/* Card Command Classes */
        sd->csd[5] = 0x50 |	/* Max. read data block length */
            HWBLOCK_SHIFT;
        sd->csd[6] = 0xe0 |	/* Partial block for read allowed */
            ((csize >> 10) & 0x03);
        sd->csd[7] = 0x00 |	/* Device size */
            ((csize >> 2) & 0xff);
        sd->csd[8] = 0x3f |	/* Max. read current */
            ((csize << 6) & 0xc0);
        sd->csd[9] = 0xfc |	/* Max. write current */
            ((CMULT_SHIFT - 2) >> 1);
        sd->csd[10] = 0x40 |	/* Erase sector size */
            (((CMULT_SHIFT - 2) << 7) & 0x80) | (sectsize >> 1);
        sd->csd[11] = 0x00 |	/* Write protect group size */
            ((sectsize << 7) & 0x80) | wpsize;
        sd->csd[12] = 0x90 |	/* Write speed factor */
            (HWBLOCK_SHIFT >> 2);
        sd->csd[13] = 0x20 |	/* Max. write data block length */
            ((HWBLOCK_SHIFT << 6) & 0xc0);
        sd->csd[14] = 0x00;	/* File format group */
        sd->csd[15] = (sd_crc7(sd->csd, 15) << 1) | 1;
    } else {			/* SDHC */
        size /= 512 * 1024;
        size -= 1;
        sd->csd[0] = 0x40;
        sd->csd[1] = 0x0e;
        sd->csd[2] = 0x00;
        sd->csd[3] = 0x32;
        sd->csd[4] = 0x5b;
        sd->csd[5] = 0x59;
        sd->csd[6] = 0x00;
        sd->csd[7] = (size >> 16) & 0xff;
        sd->csd[8] = (size >> 8) & 0xff;
        sd->csd[9] = (size & 0xff);
        sd->csd[10] = 0x7f;
        sd->csd[11] = 0x80;
        sd->csd[12] = 0x0a;
        sd->csd[13] = 0x40;
        sd->csd[14] = 0x00;
        sd->csd[15] = 0x00;
    }
}

static void sd_reset_rca(SDState *sd)
{
    sd->rca = sd->bus_protocol == PROTO_MMC;
}

static void sd_set_rca(SDState *sd)
{
    sd->rca += 0x4567;
}

/* Card status bits, split by clear condition:
 * A : According to the card current state
 * B : Always related to the previous command
 * C : Cleared by read
 */
#define CARD_STATUS_A	0x02004100
#define CARD_STATUS_B	0x00c01e00
#define CARD_STATUS_C	0xfd39a028

static void sd_reset_cardstatus(SDState *sd)
{
    sd->card_status = 0x00000100;
}

static void sd_reset_sdstatus(SDState *sd)
{
    memset(sd->sd_status, 0, 64);
}

static int sd_req_crc_validate(SDRequest *req)
{
    uint8_t buffer[5];
    buffer[0] = 0x40 | req->cmd;
    buffer[1] = (req->arg >> 24) & 0xff;
    buffer[2] = (req->arg >> 16) & 0xff;
    buffer[3] = (req->arg >> 8) & 0xff;
    buffer[4] = (req->arg >> 0) & 0xff;
    return 0;
    return sd_crc7(buffer, 5) != req->crc;	/* TODO */
}

static size_t sd_response_r1_make(SDState *sd, uint8_t *response)
{
    uint32_t status = sd->card_status;
    /* Clear the "clear on read" status bits */
    sd->card_status &= ~CARD_STATUS_C;

    if (sd->bus_protocol == PROTO_SPI) {
        response[0] = 0xff; /* XXX */
        return 1;
    } else {
        response[0] = (status >> 24) & 0xff;
        response[1] = (status >> 16) & 0xff;
        response[2] = (status >> 8) & 0xff;
        response[3] = (status >> 0) & 0xff;
        return 4;
    }
}

static size_t sd_response_r1b_make(SDState *sd, uint8_t *response)
{
    /* This response token is identical to the R1 format with the
     * optional addition of the busy signal. */
    if (sd->bus_protocol == PROTO_SPI) {
        /* The busy signal token can be any number of bytes. A zero value
         * indicates card is busy. A non-zero value indicates the card is
         * ready for the next command. */
        size_t sz = sd_response_r1_make(sd, response);

        response[sz++] = 0x42;

        return sz;
    }
    return sd_response_r1_make(sd, response);
}

static size_t sd_response_r2s_make(SDState *sd, uint8_t *response)
{
    if (sd->bus_protocol == PROTO_SPI) {
        /* TODO */
        return 2;
    } else {
        memcpy(response, sd->csd, sizeof(sd->csd));
        return 16;
    }
}

static size_t sd_response_r3_make(SDState *sd, uint8_t *response)
{
    int ofs = 0;

    if (sd->bus_protocol == PROTO_SPI) {
        ofs += sd_response_r1_make(sd, response);
    }
    response[ofs++] = (sd->ocr >> 24) & 0xff;
    response[ofs++] = (sd->ocr >> 16) & 0xff;
    response[ofs++] = (sd->ocr >> 8) & 0xff;
    response[ofs++] = (sd->ocr >> 0) & 0xff;

    return ofs;
}

static void sd_response_r6_make(SDState *sd, uint8_t *response)
{
    uint16_t arg;
    uint16_t status;

    assert(sd->bus_protocol != PROTO_SPI);
    arg = sd->rca;
    status = ((sd->card_status >> 8) & 0xc000) |
             ((sd->card_status >> 6) & 0x2000) |
              (sd->card_status & 0x1fff);
    sd->card_status &= ~(CARD_STATUS_C & 0xc81fff);

    response[0] = (arg >> 8) & 0xff;
    response[1] = arg & 0xff;
    response[2] = (status >> 8) & 0xff;
    response[3] = status & 0xff;
}

static void sd_response_r7_make(SDState *sd, uint8_t *response)
{
    assert(sd->bus_protocol != PROTO_SPI);
    response[0] = (sd->vhs >> 24) & 0xff;
    response[1] = (sd->vhs >> 16) & 0xff;
    response[2] = (sd->vhs >>  8) & 0xff;
    response[3] = (sd->vhs >>  0) & 0xff;
}

static inline uint64_t sd_addr_to_wpnum(uint64_t addr)
{
    return addr >> (HWBLOCK_SHIFT + SECTOR_SHIFT + WPGROUP_SHIFT);
}

static void sd_reset(DeviceState *dev)
{
    SDState *sd = SD_CARD(dev);
    uint64_t size;
    uint64_t sect;

    trace_sdcard_reset();
    if (sd->blk) {
        blk_get_geometry(sd->blk, &sect);
    } else {
        sect = 0;
    }
    size = sect << 9;

    sect = sd_addr_to_wpnum(size) + 1;

    sd->state = sd_idle_state;
    sd_reset_rca(sd);
    sd_reset_ocr(sd);
    sd_reset_scr(sd);
    sd_reset_cid(sd);
    sd_reset_csd(sd, size);
    sd_reset_cardstatus(sd);
    sd_reset_sdstatus(sd);

    g_free(sd->wp_groups);
    sd->wp_switch = sd->blk ? blk_is_read_only(sd->blk) : false;
    sd->wpgrps_size = sect;
    sd->wp_groups = bitmap_new(sd->wpgrps_size);
    memset(sd->function_group, 0, sizeof(sd->function_group));
    sd->erase_start = 0;
    sd->erase_end = 0;
    sd->size = size;
    sd->blk_len = 0x200;
    sd->pwd_len = 0;
    sd->expecting_acmd = false;
    sd->multi_blk_cnt = 0;
}

static bool sd_get_inserted(SDState *sd)
{
    return sd->blk && blk_is_inserted(sd->blk);
}

static bool sd_get_readonly(SDState *sd)
{
    return sd->wp_switch;
}

static void sd_cardchange(void *opaque, bool load, Error **errp)
{
    SDState *sd = opaque;
    DeviceState *dev = DEVICE(sd);
    SDBus *sdbus = SD_BUS(qdev_get_parent_bus(dev));
    bool inserted = sd_get_inserted(sd);
    bool readonly = sd_get_readonly(sd);

    if (inserted) {
        trace_sdcard_inserted(readonly);
        sd_reset(dev);
    } else {
        trace_sdcard_ejected();
    }

    /* The IRQ notification is for legacy non-QOM SD controller devices;
     * QOMified controllers use the SDBus APIs.
     */
    if (sdbus) {
        sdbus_set_inserted(sdbus, inserted);
        if (inserted) {
            sdbus_set_readonly(sdbus, readonly);
        }
    } else {
        qemu_set_irq(sd->inserted_cb, inserted);
        if (inserted) {
            qemu_set_irq(sd->readonly_cb, readonly);
        }
    }
}

static const BlockDevOps sd_block_ops = {
    .change_media_cb = sd_cardchange,
};

static bool sd_ocr_vmstate_needed(void *opaque)
{
    SDState *sd = opaque;

    /* Include the OCR state (and timer) if it is not yet powered up */
    return !FIELD_EX32(sd->ocr, OCR, CARD_POWER_UP);
}

static const VMStateDescription sd_ocr_vmstate = {
    .name = "sd-card/ocr-state",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = sd_ocr_vmstate_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ocr, SDState),
        VMSTATE_TIMER_PTR(ocr_power_timer, SDState),
        VMSTATE_END_OF_LIST()
    },
};

static int sd_vmstate_pre_load(void *opaque)
{
    SDState *sd = opaque;

    /* If the OCR state is not included (prior versions, or not
     * needed), then the OCR must be set as powered up. If the OCR state
     * is included, this will be replaced by the state restore.
     */
    sd_ocr_powerup(sd);

    return 0;
}

static const VMStateDescription sd_vmstate = {
    .name = "sd-card",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_load = sd_vmstate_pre_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(mode, SDState),
        VMSTATE_INT32(state, SDState),
        VMSTATE_UINT8_ARRAY(cid, SDState, 16),
        VMSTATE_UINT8_ARRAY(csd, SDState, 16),
        VMSTATE_UINT16(rca, SDState),
        VMSTATE_UINT32(card_status, SDState),
        VMSTATE_PARTIAL_BUFFER(sd_status, SDState, 1),
        VMSTATE_UINT32(vhs, SDState),
        VMSTATE_BITMAP(wp_groups, SDState, 0, wpgrps_size),
        VMSTATE_UINT32(blk_len, SDState),
        VMSTATE_UINT32(multi_blk_cnt, SDState),
        VMSTATE_UINT32(erase_start, SDState),
        VMSTATE_UINT32(erase_end, SDState),
        VMSTATE_UINT8_ARRAY(pwd, SDState, 16),
        VMSTATE_UINT32(pwd_len, SDState),
        VMSTATE_UINT8_ARRAY(function_group, SDState, 6),
        VMSTATE_UINT8(current_cmd, SDState),
        VMSTATE_BOOL(expecting_acmd, SDState),
        VMSTATE_UINT32(blk_written, SDState),
        VMSTATE_UINT64(data_start, SDState),
        VMSTATE_UINT32(data_offset, SDState),
        VMSTATE_UINT8_ARRAY(data, SDState, 512),
        VMSTATE_UNUSED_V(1, 512),
        VMSTATE_BOOL(enable, SDState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &sd_ocr_vmstate,
        NULL
    },
};

/* Legacy initialization function for use by non-qdevified callers */
static SDState *sdcard_init(BlockBackend *blk, sd_bus_protocol_t bus_protocol)
{
    Object *obj;
    DeviceState *dev;
    Error *err = NULL;

    obj = object_new(TYPE_SD_CARD);
    dev = DEVICE(obj);
    qdev_prop_set_drive(dev, "drive", blk, &err);
    if (err) {
        error_report("sd_init failed: %s", error_get_pretty(err));
        return NULL;
    }
    switch (bus_protocol) {
    case PROTO_SPI:
        qdev_prop_set_bit(dev, "spi", true);
        break;
    case PROTO_MMC:
        qdev_prop_set_bit(dev, "mmc", true);
        break;
    default:
        break;
    }
    object_property_set_bool(obj, true, "realized", &err);
    if (err) {
        error_report("sd_init failed: %s", error_get_pretty(err));
        return NULL;
    }

    return SD_CARD(dev);
}

SDState *sd_init(BlockBackend *blk, bool is_spi)
{
    return sdcard_init(blk, is_spi ? PROTO_SPI : PROTO_SD);
}

SDState *mmc_init(BlockBackend *blk)
{
    return sdcard_init(blk, PROTO_MMC);
}

void sd_set_cb(SDState *sd, qemu_irq readonly, qemu_irq insert)
{
    sd->readonly_cb = readonly;
    sd->inserted_cb = insert;
    qemu_set_irq(readonly, sd->blk ? blk_is_read_only(sd->blk) : 0);
    qemu_set_irq(insert, sd->blk ? blk_is_inserted(sd->blk) : 0);
}

static void sd_erase(SDState *sd)
{
    int i;
    uint64_t erase_start = sd->erase_start;
    uint64_t erase_end = sd->erase_end;

    trace_sdcard_erase();
    if (!sd->erase_start || !sd->erase_end) {
        sd->card_status |= ERASE_SEQ_ERROR;
        return;
    }

    if (extract32(sd->ocr, OCR_CCS_BITN, 1)) {
        /* High capacity memory card: erase units are 512 byte blocks */
        erase_start *= 512;
        erase_end *= 512;
    }

    erase_start = sd_addr_to_wpnum(erase_start);
    erase_end = sd_addr_to_wpnum(erase_end);
    sd->erase_start = 0;
    sd->erase_end = 0;
    sd->csd[14] |= 0x40;

    for (i = erase_start; i <= erase_end; i++) {
        if (test_bit(i, sd->wp_groups)) {
            sd->card_status |= WP_ERASE_SKIP;
        }
    }
}

static uint32_t sd_wpbits(SDState *sd, uint64_t addr)
{
    uint32_t i, wpnum;
    uint32_t ret = 0;

    wpnum = sd_addr_to_wpnum(addr);

    for (i = 0; i < 32; i++, wpnum++, addr += WPGROUP_SIZE) {
        if (addr < sd->size && test_bit(wpnum, sd->wp_groups)) {
            ret |= (1 << i);
        }
    }

    return ret;
}

static void sd_function_switch(SDState *sd, uint32_t arg)
{
    int i, mode, new_func, crc;
    mode = !!(arg & 0x80000000);

    sd->data[0] = 0x00;		/* Maximum current consumption */
    sd->data[1] = 0x01;
    sd->data[2] = 0x80;		/* Supported group 6 functions */
    sd->data[3] = 0x01;
    sd->data[4] = 0x80;		/* Supported group 5 functions */
    sd->data[5] = 0x01;
    sd->data[6] = 0x80;		/* Supported group 4 functions */
    sd->data[7] = 0x01;
    sd->data[8] = 0x80;		/* Supported group 3 functions */
    sd->data[9] = 0x01;
    sd->data[10] = 0x80;	/* Supported group 2 functions */
    sd->data[11] = 0x43;
    sd->data[12] = 0x80;	/* Supported group 1 functions */
    sd->data[13] = 0x03;
    for (i = 0; i < 6; i ++) {
        new_func = (arg >> (i * 4)) & 0x0f;
        if (mode && new_func != 0x0f)
            sd->function_group[i] = new_func;
        sd->data[14 + (i >> 1)] = new_func << ((i * 4) & 4);
    }
    memset(&sd->data[17], 0, 47);
    crc = sd_crc16(sd->data, 64);
    sd->data[65] = crc >> 8;
    sd->data[66] = crc & 0xff;
}

static inline bool sd_wp_addr(SDState *sd, uint64_t addr)
{
    return test_bit(sd_addr_to_wpnum(addr), sd->wp_groups);
}

static void sd_lock_command(SDState *sd)
{
    int erase, lock, clr_pwd, set_pwd, pwd_len;
    erase = !!(sd->data[0] & 0x08);
    lock = sd->data[0] & 0x04;
    clr_pwd = sd->data[0] & 0x02;
    set_pwd = sd->data[0] & 0x01;

    if (sd->blk_len > 1)
        pwd_len = sd->data[1];
    else
        pwd_len = 0;

    if (lock) {
        trace_sdcard_lock();
    } else {
        trace_sdcard_unlock();
    }
    if (erase) {
        if (!(sd->card_status & CARD_IS_LOCKED) || sd->blk_len > 1 ||
                        set_pwd || clr_pwd || lock || sd->wp_switch ||
                        (sd->csd[14] & 0x20)) {
            sd->card_status |= LOCK_UNLOCK_FAILED;
            return;
        }
        bitmap_zero(sd->wp_groups, sd->wpgrps_size);
        sd->csd[14] &= ~0x10;
        sd->card_status &= ~CARD_IS_LOCKED;
        sd->pwd_len = 0;
        /* Erasing the entire card here! */
        fprintf(stderr, "SD: Card force-erased by CMD42\n");
        return;
    }

    if (sd->blk_len < 2 + pwd_len ||
                    pwd_len <= sd->pwd_len ||
                    pwd_len > sd->pwd_len + 16) {
        sd->card_status |= LOCK_UNLOCK_FAILED;
        return;
    }

    if (sd->pwd_len && memcmp(sd->pwd, sd->data + 2, sd->pwd_len)) {
        sd->card_status |= LOCK_UNLOCK_FAILED;
        return;
    }

    pwd_len -= sd->pwd_len;
    if ((pwd_len && !set_pwd) ||
                    (clr_pwd && (set_pwd || lock)) ||
                    (lock && !sd->pwd_len && !set_pwd) ||
                    (!set_pwd && !clr_pwd &&
                     (((sd->card_status & CARD_IS_LOCKED) && lock) ||
                      (!(sd->card_status & CARD_IS_LOCKED) && !lock)))) {
        sd->card_status |= LOCK_UNLOCK_FAILED;
        return;
    }

    if (set_pwd) {
        memcpy(sd->pwd, sd->data + 2 + sd->pwd_len, pwd_len);
        sd->pwd_len = pwd_len;
    }

    if (clr_pwd) {
        sd->pwd_len = 0;
    }

    if (lock)
        sd->card_status |= CARD_IS_LOCKED;
    else
        sd->card_status &= ~CARD_IS_LOCKED;
}

static sd_rsp_type_t sd_normal_command(SDState *sd, SDRequest req)
{
    uint16_t rca = 0x0000;
    uint64_t addr = (sd->ocr & (1 << 30)) ? (uint64_t) req.arg << 9 : req.arg;

    if (req.cmd != 55 || sd->expecting_acmd) {
        trace_sdcard_normal_command(sd->proto_name,
                                    sd_cmd_abbreviation(req.cmd), req.cmd,
                                    req.arg, sd_state_name(sd->state));
    }

    /* Not interpreting this as an app command */
    sd->card_status &= ~APP_CMD;

    if (sd_cmd_type[req.cmd] == sd_ac
        || sd_cmd_type[req.cmd] == sd_adtc) {
        rca = req.arg >> 16;
    }

    /* CMD23 (set block count) must be immediately followed by CMD18 or CMD25
     * if not, its effects are cancelled */
    if (sd->multi_blk_cnt != 0 && !(req.cmd == 18 || req.cmd == 25)) {
        sd->multi_blk_cnt = 0;
    }

    switch (req.cmd) {
    /* Basic commands (Class 0 and Class 1) */
    case 0:	/* CMD0:   GO_IDLE_STATE */
        if (sd->state != sd_inactive_state) {
            sd->state = sd_idle_state;
            sd_reset(DEVICE(sd));
        }
        return sd->bus_protocol == PROTO_SPI ? sd_r1 : sd_r0;

    case 1:	/* CMD1:   SEND_OP_CMD */
        if (sd->bus_protocol == PROTO_SPI) {
            sd->state = sd_transfer_state;
            return sd_r1;
        }
        goto bad_cmd;

    case 2:	/* CMD2:   ALL_SEND_CID */
        if (sd->state == sd_ready_state) {
            sd->state = sd_identification_state;
            return sd_r2_i;
        }
        break;

    case 3:	/* CMD3:   SEND_RELATIVE_ADDR */
        switch (sd->state) {
        case sd_identification_state:
        case sd_standby_state:
            sd->state = sd_standby_state;
            sd_set_rca(sd);
            return sd_r6;

        default:
            break;
        }
        break;

    case 4:	/* CMD4:   SEND_DSR */
        switch (sd->state) {
        case sd_standby_state:
            break;

        default:
            break;
        }
        break;

    case 5: /* CMD5: reserved for SDIO cards */
        return sd_illegal;

    case 6:	/* CMD6:   SWITCH_FUNCTION */
        if (sd->mode == sd_data_transfer_mode) {
            sd_function_switch(sd, req.arg);
            sd->state = sd_sendingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd_r1;
        }
        break;

    case 7:	/* CMD7:   SELECT/DESELECT_CARD */
        switch (sd->state) {
        case sd_standby_state:
            if (sd->rca != rca)
                return sd_r0;

            sd->state = sd_transfer_state;
            return sd_r1b;

        case sd_transfer_state:
        case sd_sendingdata_state:
            if (sd->rca == rca)
                break;

            sd->state = sd_standby_state;
            return sd_r1b;

        case sd_disconnect_state:
            if (sd->rca != rca)
                return sd_r0;

            sd->state = sd_programming_state;
            return sd_r1b;

        case sd_programming_state:
            if (sd->rca == rca)
                break;

            sd->state = sd_disconnect_state;
            return sd_r1b;

        default:
            break;
        }
        break;

    case 8:	/* CMD8:   SEND_IF_COND */
        /* Physical Layer Specification Version 2.00 command */
        switch (sd->state) {
        case sd_idle_state:
            sd->vhs = 0;

            /* No response if not exactly one VHS bit is set.  */
            if (!(req.arg >> 8) || (req.arg >> (ctz32(req.arg & ~0xff) + 1))) {
                return sd->bus_protocol == PROTO_SPI ? sd_r7 : sd_r0; /* XXX */
            }

            /* Accept.  */
            sd->vhs = req.arg;
            return sd_r7;

        default:
            break;
        }
        break;

    case 9:	/* CMD9:   SEND_CSD */
        switch (sd->state) {
        case sd_standby_state:
            if (sd->rca != rca)
                return sd_r0;

            return sd_r2_s;

        case sd_transfer_state:
            if (sd->bus_protocol == PROTO_SPI) {
                sd->state = sd_sendingdata_state;
                memcpy(sd->data, sd->csd, 16);
                sd->data_start = addr;
                sd->data_offset = 0;
                return sd_r1;
            }

        default:
            break;
        }
        break;

    case 10:	/* CMD10:  SEND_CID */
        switch (sd->state) {
        case sd_standby_state:
            if (sd->rca != rca)
                return sd_r0;

            return sd_r2_i;

        case sd_transfer_state:
            if (sd->bus_protocol == PROTO_SPI) {
                sd->state = sd_sendingdata_state;
                memcpy(sd->data, sd->cid, 16);
                sd->data_start = addr;
                sd->data_offset = 0;
                return sd_r1;
            }

        default:
            break;
        }
        break;

    case 11:	/* CMD11:  READ_DAT_UNTIL_STOP */
        if (sd->state == sd_transfer_state) {
            sd->state = sd_sendingdata_state;
            sd->data_start = req.arg;
            sd->data_offset = 0;

            if (sd->data_start + sd->blk_len > sd->size)
                sd->card_status |= ADDRESS_ERROR;
            return sd_r0;
        }
        break;

    case 12:	/* CMD12:  STOP_TRANSMISSION */
        switch (sd->state) {
        case sd_sendingdata_state:
            sd->state = sd_transfer_state;
            return sd_r1b;

        case sd_receivingdata_state:
            sd->state = sd_programming_state;
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
            return sd_r1b;

        default:
            break;
        }
        break;

    case 13:	/* CMD13:  SEND_STATUS */
        if (sd->mode == sd_data_transfer_mode) {
            if (sd->rca != rca)
                return sd_r0;

            return sd_r1;
        }
        break;

    case 15:	/* CMD15:  GO_INACTIVE_STATE */
        if (sd->mode == sd_data_transfer_mode) {
            if (sd->rca != rca)
                return sd_r0;

            sd->state = sd_inactive_state;
            return sd_r0;
        }
        break;

    /* Block read commands (Classs 2) */
    case 16:	/* CMD16:  SET_BLOCKLEN */
        if (sd->state == sd_transfer_state) {
            if (req.arg > (1 << HWBLOCK_SHIFT))
                sd->card_status |= BLOCK_LEN_ERROR;
            else
                sd->blk_len = req.arg;

            return sd_r1;
        }
        break;

    case 17:	/* CMD17:  READ_SINGLE_BLOCK */
        if (sd->state == sd_transfer_state) {
            sd->state = sd_sendingdata_state;
            sd->data_start = addr;
            sd->data_offset = 0;

            if (sd->data_start + sd->blk_len > sd->size)
                sd->card_status |= ADDRESS_ERROR;
            return sd_r1;
        }
        break;

    case 18:	/* CMD18:  READ_MULTIPLE_BLOCK */
        if (sd->state == sd_transfer_state) {
            sd->state = sd_sendingdata_state;
            sd->data_start = addr;
            sd->data_offset = 0;

            if (sd->data_start + sd->blk_len > sd->size)
                sd->card_status |= ADDRESS_ERROR;
            return sd_r1;
        }
        break;

    case 23:    /* CMD23: SET_BLOCK_COUNT */
        if (sd->state == sd_transfer_state) {
            sd->multi_blk_cnt = req.arg;
            return sd_r1;
        }
        break;

    /* Block write commands (Class 4) */
    case 24:	/* CMD24:  WRITE_SINGLE_BLOCK */
        if (sd->bus_protocol == PROTO_SPI) {
            goto unimplemented_cmd;
        }
        if (sd->state == sd_transfer_state) {
            sd->state = sd_receivingdata_state;
            sd->data_start = addr;
            sd->data_offset = 0;
            sd->blk_written = 0;

            if (sd->data_start + sd->blk_len > sd->size)
                sd->card_status |= ADDRESS_ERROR;
            if (sd_wp_addr(sd, sd->data_start))
                sd->card_status |= WP_VIOLATION;
            if (sd->csd[14] & 0x30)
                sd->card_status |= WP_VIOLATION;
            return sd_r1;
        }
        break;

    case 25:	/* CMD25:  WRITE_MULTIPLE_BLOCK */
        if (sd->bus_protocol == PROTO_SPI) {
            goto unimplemented_cmd;
        }
        if (sd->state == sd_transfer_state) {
            sd->state = sd_receivingdata_state;
            sd->data_start = addr;
            sd->data_offset = 0;
            sd->blk_written = 0;

            if (sd->data_start + sd->blk_len > sd->size)
                sd->card_status |= ADDRESS_ERROR;
            if (sd_wp_addr(sd, sd->data_start))
                sd->card_status |= WP_VIOLATION;
            if (sd->csd[14] & 0x30)
                sd->card_status |= WP_VIOLATION;
            return sd_r1;
        }
        break;

    case 26:	/* CMD26:  PROGRAM_CID */
        if (sd->state == sd_transfer_state) {
            sd->state = sd_receivingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd_r1;
        }
        break;

    case 27:	/* CMD27:  PROGRAM_CSD */
        if (sd->bus_protocol == PROTO_SPI) {
            goto unimplemented_cmd;
        }
        if (sd->state == sd_transfer_state) {
            sd->state = sd_receivingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd_r1;
        }
        break;

    /* Write protection (Class 6) */
    case 28:	/* CMD28:  SET_WRITE_PROT */
        if (sd->state == sd_transfer_state) {
            if (addr >= sd->size) {
                sd->card_status |= ADDRESS_ERROR;
                return sd_r1b;
            }

            sd->state = sd_programming_state;
            set_bit(sd_addr_to_wpnum(addr), sd->wp_groups);
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
            return sd_r1b;
        }
        break;

    case 29:	/* CMD29:  CLR_WRITE_PROT */
        if (sd->state == sd_transfer_state) {
            if (addr >= sd->size) {
                sd->card_status |= ADDRESS_ERROR;
                return sd_r1b;
            }

            sd->state = sd_programming_state;
            clear_bit(sd_addr_to_wpnum(addr), sd->wp_groups);
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
            return sd_r1b;
        }
        break;

    case 30:	/* CMD30:  SEND_WRITE_PROT */
        if (sd->state == sd_transfer_state) {
            sd->state = sd_sendingdata_state;
            *(uint32_t *) sd->data = sd_wpbits(sd, req.arg);
            sd->data_start = addr;
            sd->data_offset = 0;
            return sd->bus_protocol == PROTO_SPI ? sd_r1 : sd_r1b;
        }
        break;

    /* Erase commands (Class 5) */
    case 32:	/* CMD32:  ERASE_WR_BLK_START */
        if (sd->state == sd_transfer_state) {
            sd->erase_start = req.arg;
            return sd_r1;
        }
        break;

    case 33:	/* CMD33:  ERASE_WR_BLK_END */
        if (sd->state == sd_transfer_state) {
            sd->erase_end = req.arg;
            return sd_r1;
        }
        break;

    case 38:	/* CMD38:  ERASE */
        if (sd->state == sd_transfer_state) {
            if (sd->csd[14] & 0x30) {
                sd->card_status |= WP_VIOLATION;
                return sd_r1b;
            }

            sd->state = sd_programming_state;
            sd_erase(sd);
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
            return sd_r1b;
        }
        break;

    /* Lock card commands (Class 7) */
    case 42:	/* CMD42:  LOCK_UNLOCK */
        if (sd->bus_protocol == PROTO_SPI) {
            goto unimplemented_cmd;
        }
        if (sd->state == sd_transfer_state) {
            sd->state = sd_receivingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd_r1;
        }
        break;

    case 52 ... 54:
        /* CMD52, CMD53, CMD54: reserved for SDIO cards
         * (see the SDIO Simplified Specification V2.0)
         * Handle as illegal command but do not complain
         * on stderr, as some OSes may use these in their
         * probing for presence of an SDIO card.
         */
        return sd_illegal;

    /* Application specific commands (Class 8) */
    case 55:	/* CMD55:  APP_CMD */
        if (sd->bus_protocol != PROTO_SPI) {
            if (sd->rca != rca) {
                return sd_r0;
            }
        }
        sd->expecting_acmd = true;
        sd->card_status |= APP_CMD;
        return sd_r1;

    case 56:	/* CMD56:  GEN_CMD */
        if (sd->state == sd_transfer_state) {
            sd->data_offset = 0;
            if (req.arg & 1)
                sd->state = sd_sendingdata_state;
            else
                sd->state = sd_receivingdata_state;
            return sd_r1;
        }
        break;

    case 58:    /* CMD58:   READ_OCR (SPI) */
        if (sd->bus_protocol != PROTO_SPI) {
            goto bad_cmd;
        }
        return sd_r3;

    case 59:    /* CMD59:   CRC_ON_OFF (SPI) */
        if (sd->bus_protocol != PROTO_SPI) {
            goto bad_cmd;
        }
        goto unimplemented_cmd;

    default:
    bad_cmd:
        qemu_log_mask(LOG_GUEST_ERROR, "SD: Unknown CMD%i\n", req.cmd);
        return sd_illegal;

    unimplemented_cmd:
        /* Commands that are recognised but not yet implemented in SPI mode.  */
        qemu_log_mask(LOG_UNIMP, "SD: CMD%i not implemented in SPI mode\n",
                      req.cmd);
        return sd_illegal;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "SD: CMD%i in a wrong state\n", req.cmd);
    return sd_illegal;
}

static sd_rsp_type_t sd_app_command(SDState *sd, SDRequest req)
{
    trace_sdcard_app_command(sd->proto_name, sd_acmd_abbreviation(req.cmd),
                             req.cmd, req.arg, sd_state_name(sd->state));
    sd->card_status |= APP_CMD;
    switch (req.cmd) {
    case 6:	/* ACMD6:  SET_BUS_WIDTH */
        if (sd->state == sd_transfer_state) {
            sd->sd_status[0] &= 0x3f;
            sd->sd_status[0] |= (req.arg & 0x03) << 6;
            return sd_r1;
        }
        break;

    case 13:	/* ACMD13: SD_STATUS */
        if (sd->state == sd_transfer_state) {
            sd->state = sd_sendingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd->bus_protocol == PROTO_SPI ? sd_r2_s : sd_r1;
        }
        break;

    case 18:
        if (sd->bus_protocol == PROTO_SPI) {
            goto unimplemented_cmd;
        }
        break;

    case 22:	/* ACMD22: SEND_NUM_WR_BLOCKS */
        if (sd->state == sd_transfer_state) {
            *(uint32_t *) sd->data = sd->blk_written;

            sd->state = sd_sendingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd_r1;
        }
        break;

    case 23:	/* ACMD23: SET_WR_BLK_ERASE_COUNT */
        if (sd->state == sd_transfer_state) {
            return sd_r1;
        }
        break;

    case 25:
    case 26:
        if (sd->bus_protocol == PROTO_SPI) {
            goto unimplemented_cmd;
        }
        break;

    case 38:
        if (sd->bus_protocol == PROTO_SPI) {
            goto unimplemented_cmd;
        }
        break;

    case 41:	/* ACMD41: SD_APP_OP_COND */
        if (sd->bus_protocol == PROTO_SPI) {
            /* SEND_OP_CMD */
            sd->state = sd_transfer_state;
            return sd_r1;
        }
        if (sd->state == sd_idle_state) {
            /* If it's the first ACMD41 since reset, we need to decide
             * whether to power up. If this is not an enquiry ACMD41,
             * we immediately report power on and proceed below to the
             * ready state, but if it is, we set a timer to model a
             * delay for power up. This works around a bug in EDK2
             * UEFI, which sends an initial enquiry ACMD41, but
             * assumes that the card is in ready state as soon as it
             * sees the power up bit set. */
            if (!FIELD_EX32(sd->ocr, OCR, CARD_POWER_UP)) {
                if ((req.arg & ACMD41_ENQUIRY_MASK) != 0) {
                    timer_del(sd->ocr_power_timer);
                    sd_ocr_powerup(sd);
                } else if (!timer_pending(sd->ocr_power_timer)) {
                    timer_mod_ns(sd->ocr_power_timer,
                                 (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                                  + OCR_POWER_DELAY_NS));
                }
            }

            /* We accept any voltage.  10000 V is nothing.
             *
             * Once we're powered up, we advance straight to ready state
             * unless it's an enquiry ACMD41 (bits 23:0 == 0).
             */
            if (req.arg & ACMD41_ENQUIRY_MASK) {
                sd->state = sd_ready_state;
            }

            return sd_r3;
        }
        break;

    case 42:	/* ACMD42: SET_CLR_CARD_DETECT */
        if (sd->state == sd_transfer_state) {
            /* Bringing in the 50KOhm pull-up resistor... Done.  */
            return sd_r1;
        }
        break;

    case 43 ... 49:
        if (sd->bus_protocol == PROTO_SPI) {
            goto unimplemented_cmd;
        }
        break;

    case 51:	/* ACMD51: SEND_SCR */
        if (sd->state == sd_transfer_state) {
            sd->state = sd_sendingdata_state;
            sd->data_start = 0;
            sd->data_offset = 0;
            return sd_r1;
        }
        break;

    case 55:    /* Not exist */
        break;

    default:
        /* Fall back to standard commands.  */
        return sd_normal_command(sd, req);

    unimplemented_cmd:
        /* Commands that are recognised but not yet implemented in SPI mode.  */
        qemu_log_mask(LOG_UNIMP, "SD: CMD%i not implemented in SPI mode\n",
                      req.cmd);
        return sd_illegal;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "SD: ACMD%i in a wrong state\n", req.cmd);
    return sd_illegal;
}

static bool cmd_valid_while_locked(SDState *sd, SDRequest *req)
{
    /* Valid commands in locked state:
     * basic class (0)
     * lock card class (7)
     * CMD16
     * implicitly, the ACMD prefix CMD55
     * ACMD41 and ACMD42
     * Anything else provokes an "illegal command" response.
     */
    if (sd->expecting_acmd) {
        return req->cmd == 41 || req->cmd == 42;
    }
    if (req->cmd == 16 || req->cmd == 55) {
        return true;
    }
    return cmd_class_supported(sd, req->cmd, 0, false) ||
            cmd_class_supported(sd, req->cmd, 7, false);
}

int sd_do_command(SDState *sd, SDRequest *req,
                  uint8_t *response) {
    int last_state;
    sd_rsp_type_t rtype;
    int rsplen;

    if (!sd->blk || !blk_is_inserted(sd->blk) || !sd->enable) {
        return 0;
    }

    if (sd_req_crc_validate(req)) {
        sd->card_status |= COM_CRC_ERROR;
        rtype = sd_illegal;
        goto send_response;
    }

    if (req->cmd >= SDCARD_CMD_MAX) {
        qemu_log_mask(LOG_GUEST_ERROR, "SD: incorrect command 0x%02x\n",
                      req->cmd);
        req->cmd &= 0x3f;
    }

    if (!cmd_version_supported(sd, req->cmd, sd->expecting_acmd)) {
        sd->card_status |= ILLEGAL_COMMAND;
        rtype = sd_illegal;
        goto send_response;
    }

    if (sd->card_status & CARD_IS_LOCKED) {
        if (!cmd_valid_while_locked(sd, req)) {
            sd->card_status |= ILLEGAL_COMMAND;
            sd->expecting_acmd = false;
            qemu_log_mask(LOG_GUEST_ERROR, "SD: Card is locked\n");
            rtype = sd_illegal;
            goto send_response;
        }
    }

    last_state = sd->state;
    sd_set_mode(sd);

    if (sd->expecting_acmd) {
        sd->expecting_acmd = false;
        rtype = sd_app_command(sd, *req);
    } else {
        rtype = sd_normal_command(sd, *req);
    }

    if (rtype == sd_illegal) {
        sd->card_status |= ILLEGAL_COMMAND;
    } else {
        /* Valid command, we can update the 'state before command' bits.
         * (Do this now so they appear in r1 responses.)
         */
        sd->current_cmd = req->cmd;
        sd->card_status &= ~CURRENT_STATE;
        sd->card_status |= (last_state << 9);
    }

send_response:
    switch (rtype) {
    case sd_r1:
        rsplen = sd_response_r1_make(sd, response);
        break;

    case sd_r1b:
        rsplen = sd_response_r1b_make(sd, response);
        break;

    case sd_r2_i:
        memcpy(response, sd->cid, sizeof(sd->cid));
        rsplen = 16;
        break;

    case sd_r2_s:
        rsplen = sd_response_r2s_make(sd, response);
        break;

    case sd_r3:
        rsplen = sd_response_r3_make(sd, response);
        break;

    case sd_r6:
        sd_response_r6_make(sd, response);
        rsplen = 4;
        break;

    case sd_r7:
        sd_response_r7_make(sd, response);
        rsplen = 4;
        break;

    case sd_r0:
    case sd_illegal:
    default:
        rsplen = 0;
        break;
    }

    if (rtype != sd_illegal) {
        /* Clear the "clear on valid command" status bits now we've
         * sent any response
         */
        sd->card_status &= ~CARD_STATUS_B;
    }

#ifdef DEBUG_SD
    if (rsplen) {
        int i;
        DPRINTF("Response:");
        for (i = 0; i < rsplen; i++)
            fprintf(stderr, " %02x", response[i]);
        fprintf(stderr, " state %d\n", sd->state);
    } else {
        DPRINTF("No response %d\n", sd->state);
    }
#endif

    return rsplen;
}

static void sd_blk_read(SDState *sd, uint64_t addr, uint32_t len)
{
    trace_sdcard_read_block(addr, len);
    if (!sd->blk || blk_pread(sd->blk, addr, sd->data, len) < 0) {
        fprintf(stderr, "sd_blk_read: read error on host side\n");
    }
}

static void sd_blk_write(SDState *sd, uint64_t addr, uint32_t len)
{
    trace_sdcard_write_block(addr, len);
    if (!sd->blk || blk_pwrite(sd->blk, addr, sd->data, len, 0) < 0) {
        fprintf(stderr, "sd_blk_write: write error on host side\n");
    }
}

#define BLK_READ_BLOCK(a, len)	sd_blk_read(sd, a, len)
#define BLK_WRITE_BLOCK(a, len)	sd_blk_write(sd, a, len)
#define APP_READ_BLOCK(a, len)	memset(sd->data, 0xec, len)
#define APP_WRITE_BLOCK(a, len)

void sd_write_data(SDState *sd, uint8_t value)
{
    int i;

    if (!sd->blk || !blk_is_inserted(sd->blk) || !sd->enable)
        return;

    if (sd->state != sd_receivingdata_state) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "sd_write_data: not in Receiving-Data state\n");
        return;
    }

    if (sd->card_status & (ADDRESS_ERROR | WP_VIOLATION))
        return;

    trace_sdcard_write_data(value);
    switch (sd->current_cmd) {
    case 24:	/* CMD24:  WRITE_SINGLE_BLOCK */
        sd->data[sd->data_offset ++] = value;
        if (sd->data_offset >= sd->blk_len) {
            /* TODO: Check CRC before committing */
            sd->state = sd_programming_state;
            BLK_WRITE_BLOCK(sd->data_start, sd->data_offset);
            sd->blk_written ++;
            sd->csd[14] |= 0x40;
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
        }
        break;

    case 25:	/* CMD25:  WRITE_MULTIPLE_BLOCK */
        if (sd->data_offset == 0) {
            /* Start of the block - let's check the address is valid */
            if (sd->data_start + sd->blk_len > sd->size) {
                sd->card_status |= ADDRESS_ERROR;
                break;
            }
            if (sd_wp_addr(sd, sd->data_start)) {
                sd->card_status |= WP_VIOLATION;
                break;
            }
        }
        sd->data[sd->data_offset++] = value;
        if (sd->data_offset >= sd->blk_len) {
            /* TODO: Check CRC before committing */
            sd->state = sd_programming_state;
            BLK_WRITE_BLOCK(sd->data_start, sd->data_offset);
            sd->blk_written++;
            sd->data_start += sd->blk_len;
            sd->data_offset = 0;
            sd->csd[14] |= 0x40;

            /* Bzzzzzzztt .... Operation complete.  */
            if (sd->multi_blk_cnt != 0) {
                if (--sd->multi_blk_cnt == 0) {
                    /* Stop! */
                    sd->state = sd_transfer_state;
                    break;
                }
            }

            sd->state = sd_receivingdata_state;
        }
        break;

    case 26:	/* CMD26:  PROGRAM_CID */
        sd->data[sd->data_offset ++] = value;
        if (sd->data_offset >= sizeof(sd->cid)) {
            /* TODO: Check CRC before committing */
            sd->state = sd_programming_state;
            for (i = 0; i < sizeof(sd->cid); i ++)
                if ((sd->cid[i] | 0x00) != sd->data[i])
                    sd->card_status |= CID_CSD_OVERWRITE;

            if (!(sd->card_status & CID_CSD_OVERWRITE))
                for (i = 0; i < sizeof(sd->cid); i ++) {
                    sd->cid[i] |= 0x00;
                    sd->cid[i] &= sd->data[i];
                }
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
        }
        break;

    case 27:	/* CMD27:  PROGRAM_CSD */
        sd->data[sd->data_offset ++] = value;
        if (sd->data_offset >= sizeof(sd->csd)) {
            /* TODO: Check CRC before committing */
            sd->state = sd_programming_state;
            for (i = 0; i < sizeof(sd->csd); i ++)
                if ((sd->csd[i] | sd_csd_rw_mask[i]) !=
                    (sd->data[i] | sd_csd_rw_mask[i]))
                    sd->card_status |= CID_CSD_OVERWRITE;

            /* Copy flag (OTP) & Permanent write protect */
            if (sd->csd[14] & ~sd->data[14] & 0x60)
                sd->card_status |= CID_CSD_OVERWRITE;

            if (!(sd->card_status & CID_CSD_OVERWRITE))
                for (i = 0; i < sizeof(sd->csd); i ++) {
                    sd->csd[i] |= sd_csd_rw_mask[i];
                    sd->csd[i] &= sd->data[i];
                }
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
        }
        break;

    case 42:	/* CMD42:  LOCK_UNLOCK */
        sd->data[sd->data_offset ++] = value;
        if (sd->data_offset >= sd->blk_len) {
            /* TODO: Check CRC before committing */
            sd->state = sd_programming_state;
            sd_lock_command(sd);
            /* Bzzzzzzztt .... Operation complete.  */
            sd->state = sd_transfer_state;
        }
        break;

    case 56:	/* CMD56:  GEN_CMD */
        sd->data[sd->data_offset ++] = value;
        if (sd->data_offset >= sd->blk_len) {
            APP_WRITE_BLOCK(sd->data_start, sd->data_offset);
            sd->state = sd_transfer_state;
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "sd_write_data: unknown command\n");
        break;
    }
}

uint8_t sd_read_data(SDState *sd)
{
    /* TODO: Append CRCs */
    uint8_t ret;
    int io_len;

    if (!sd->blk || !blk_is_inserted(sd->blk) || !sd->enable)
        return 0x00;

    if (sd->state != sd_sendingdata_state) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "sd_read_data: not in Sending-Data state\n");
        return 0x00;
    }

    if (sd->card_status & (ADDRESS_ERROR | WP_VIOLATION))
        return 0x00;

    io_len = (sd->ocr & (1 << 30)) ? 512 : sd->blk_len;

    trace_sdcard_read_data(io_len);
    switch (sd->current_cmd) {
    case 6:	/* CMD6:   SWITCH_FUNCTION */
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= 64)
            sd->state = sd_transfer_state;
        break;

    case 9:	/* CMD9:   SEND_CSD */
    case 10:	/* CMD10:  SEND_CID */
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= 16)
            sd->state = sd_transfer_state;
        break;

    case 11:	/* CMD11:  READ_DAT_UNTIL_STOP */
        if (sd->data_offset == 0)
            BLK_READ_BLOCK(sd->data_start, io_len);
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= io_len) {
            sd->data_start += io_len;
            sd->data_offset = 0;
            if (sd->data_start + io_len > sd->size) {
                sd->card_status |= ADDRESS_ERROR;
                break;
            }
        }
        break;

    case 13:	/* ACMD13: SD_STATUS */
        ret = sd->sd_status[sd->data_offset ++];

        if (sd->data_offset >= sizeof(sd->sd_status))
            sd->state = sd_transfer_state;
        break;

    case 17:	/* CMD17:  READ_SINGLE_BLOCK */
        if (sd->data_offset == 0)
            BLK_READ_BLOCK(sd->data_start, io_len);
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= io_len)
            sd->state = sd_transfer_state;
        break;

    case 18:	/* CMD18:  READ_MULTIPLE_BLOCK */
        if (sd->data_offset == 0)
            BLK_READ_BLOCK(sd->data_start, io_len);
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= io_len) {
            sd->data_start += io_len;
            sd->data_offset = 0;

            if (sd->multi_blk_cnt != 0) {
                if (--sd->multi_blk_cnt == 0) {
                    /* Stop! */
                    sd->state = sd_transfer_state;
                    break;
                }
            }

            if (sd->data_start + io_len > sd->size) {
                sd->card_status |= ADDRESS_ERROR;
                break;
            }
        }
        break;

    case 22:	/* ACMD22: SEND_NUM_WR_BLOCKS */
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= 4)
            sd->state = sd_transfer_state;
        break;

    case 30:	/* CMD30:  SEND_WRITE_PROT */
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= 4)
            sd->state = sd_transfer_state;
        break;

    case 51:	/* ACMD51: SEND_SCR */
        ret = sd->scr[sd->data_offset ++];

        if (sd->data_offset >= sizeof(sd->scr))
            sd->state = sd_transfer_state;
        break;

    case 56:	/* CMD56:  GEN_CMD */
        if (sd->data_offset == 0)
            APP_READ_BLOCK(sd->data_start, sd->blk_len);
        ret = sd->data[sd->data_offset ++];

        if (sd->data_offset >= sd->blk_len)
            sd->state = sd_transfer_state;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "sd_read_data: unknown command\n");
        return 0x00;
    }

    return ret;
}

bool sd_data_ready(SDState *sd)
{
    return sd->state == sd_sendingdata_state;
}

void sd_enable(SDState *sd, bool enable)
{
    sd->enable = enable;
}

static void sd_instance_init(Object *obj)
{
    SDState *sd = SD_CARD(obj);

    sd->enable = true;
    sd->ocr_power_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sd_ocr_powerup, sd);
}

static void sd_instance_finalize(Object *obj)
{
    SDState *sd = SD_CARD(obj);

    timer_del(sd->ocr_power_timer);
    timer_free(sd->ocr_power_timer);
}

static void sd_realize(DeviceState *dev, Error **errp)
{
    SDState *sd = SD_CARD(dev);
    int ret;

    sd->proto_name = sd_protocol_name(sd->bus_protocol);
    if (sd->bus_protocol == PROTO_MMC) {
        sd->spec_version = MMC_SPEC_VER_4_51;
    } else {
        sd->spec_version = SD_PHY_SPEC_VER_2_00;
    }

    if (sd->blk && blk_is_read_only(sd->blk)) {
        error_setg(errp, "Cannot use read-only drive as SD card");
        return;
    }

    if (sd->blk) {
        int64_t size;

        ret = blk_set_perm(sd->blk, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                           BLK_PERM_ALL, errp);
        if (ret < 0) {
            return;
        }

        size = blk_getlength(sd->blk);
        if (size < 0 || size <= 2 * G_BYTE) {
            sd->capacity = sd_capacity_sdsc;
        } else if (size <= 32 * G_BYTE) {
            sd->capacity = sd_capacity_sdhc;
        } else if (size <= 2 * T_BYTE) {
            sd->capacity = sd_capacity_sdxc;
        } else {
            error_setg(errp, "block size unsupported: %lld TB", size / T_BYTE);
            return;
        }
        trace_sdcard_capacity(sd_capacity(sd->capacity), size);

        if (sd->capacity == sd_capacity_sdxc
                && sd->spec_version < SD_PHY_SPEC_VER_3_01) {
            error_setg(errp, "capacity SDHC requires at least Spec v3.01");
            return;
        }

        blk_set_dev_ops(sd->blk, &sd_block_ops, sd);
    }
}

static Property sd_properties[] = {
    DEFINE_PROP_DRIVE("drive", SDState, blk),
    /* We do not model the chip select pin, so allow the board to select
     * whether card should be in SSI or MMC/SD mode.  It is also up to the
     * board to ensure that ssi transfers only occur when the chip select
     * is asserted.  */
    DEFINE_PROP_BIT("spi", SDState, bus_protocol, 1, false),
    DEFINE_PROP_BIT("mmc", SDState, bus_protocol, 2, false),
    DEFINE_PROP_END_OF_LIST()
};

static void sd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SDCardClass *sc = SD_CARD_CLASS(klass);

    dc->realize = sd_realize;
    dc->props = sd_properties;
    dc->vmsd = &sd_vmstate;
    dc->reset = sd_reset;
    dc->bus_type = TYPE_SD_BUS;

    sc->do_command = sd_do_command;
    sc->write_data = sd_write_data;
    sc->read_data = sd_read_data;
    sc->data_ready = sd_data_ready;
    sc->enable = sd_enable;
    sc->get_inserted = sd_get_inserted;
    sc->get_readonly = sd_get_readonly;
}

static const TypeInfo sd_info = {
    .name = TYPE_SD_CARD,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SDState),
    .class_size = sizeof(SDCardClass),
    .class_init = sd_class_init,
    .instance_init = sd_instance_init,
    .instance_finalize = sd_instance_finalize,
};

static void sd_register_types(void)
{
    type_register_static(&sd_info);
}

type_init(sd_register_types)
