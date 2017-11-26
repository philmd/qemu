/*
 * Helper to hexdump a buffer
 *
 * Copyright (c) 2013 Red Hat, Inc.
 * Copyright (c) 2013 Gerd Hoffmann <kraxel@redhat.com>
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 * Copyright (c) 2013 Xilinx, Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/cutils.h"

void qemu_hexdump(const char *buf, FILE *fp, const char *prefix, size_t size)
{
    unsigned int b, len, i, c;

    for (b = 0; b < size; b += 16) {
        len = size - b;
        if (len > 16) {
            len = 16;
        }
        fprintf(fp, "%s: %04x:", prefix, b);
        for (i = 0; i < 16; i++) {
            if ((i % 4) == 0) {
                fprintf(fp, " ");
            }
            if (i < len) {
                fprintf(fp, " %02x", (unsigned char)buf[b + i]);
            } else {
                fprintf(fp, "   ");
            }
        }
        fprintf(fp, " ");
        for (i = 0; i < len; i++) {
            c = buf[b + i];
            if (c < ' ' || c > '~') {
                c = '.';
            }
            fprintf(fp, "%c", c);
        }
        fprintf(fp, "\n");
    }
}

char *qemu_hexbuf_strdup(const void *buf, size_t size,
                         const char *str_hdr, const char *desc_if_empty)
{
    const uint8_t *u8 = (uint8_t *)buf;
    GString *s;
    int i;

    if (!size) {
        return g_strdup(desc_if_empty ? desc_if_empty : "");
    }
    s = g_string_new(str_hdr ? : "");
    for (i = 0; i < size; i++) {
        g_string_append_printf(s, "%02x ", u8[i]);
    }

    return g_string_free(s, FALSE);
}
