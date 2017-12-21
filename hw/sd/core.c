/*
 * SD card bus interface code.
 *
 * Copyright (c) 2015 Linaro Limited
 *
 * Author:
 *  Peter Maydell <peter.maydell@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sd/sd.h"
#include "qemu/cutils.h"
#include "trace.h"

static inline const char *sdbus_name(SDBus *sdbus)
{
    return sdbus->qbus.name;
}

static SDState *get_slave(SDBus *sdbus)
{
    /* We only ever have one child on the bus so just return it */
    BusChild *kid = QTAILQ_FIRST(&sdbus->qbus.children);

    if (!kid) {
        return NULL;
    }
    return SDBUS_SLAVE(kid->child);
}

int sdbus_do_command(SDBus *sdbus, SDRequest *req, uint8_t *response)
{
    SDState *card = get_slave(sdbus);
    int sz = 0;
    char *hexbuf;

    trace_sdbus_command(sdbus_name(sdbus), req->cmd, req->arg, req->crc);
    if (card) {
        SDSlaveClass *sc = SDBUS_SLAVE_GET_CLASS(card);

        sz = sc->do_command(card, req, response);
        hexbuf = qemu_hexbuf_strdup(response, sz, NULL, "resp: ");
        trace_sdbus_command_response(sdbus_name(sdbus),
                                     req->cmd, req->arg, hexbuf);
        g_free(hexbuf);
    }

    return sz;
}

void sdbus_write_data(SDBus *sdbus, uint8_t value)
{
    SDState *card = get_slave(sdbus);

    trace_sdbus_write(sdbus_name(sdbus), value);
    if (card) {
        SDSlaveClass *sc = SDBUS_SLAVE_GET_CLASS(card);

        sc->write_data(card, value);
    }
}

uint8_t sdbus_read_data(SDBus *sdbus)
{
    SDState *card = get_slave(sdbus);
    uint8_t value = 0;

    if (card) {
        SDSlaveClass *sc = SDBUS_SLAVE_GET_CLASS(card);

        value = sc->read_data(card);
    }
    trace_sdbus_read(sdbus_name(sdbus), value);

    return value;
}

bool sdbus_data_ready(SDBus *sdbus)
{
    SDState *card = get_slave(sdbus);

    if (card) {
        SDSlaveClass *sc = SDBUS_SLAVE_GET_CLASS(card);

        return sc->data_ready(card);
    }

    return false;
}

bool sdbus_get_inserted(SDBus *sdbus)
{
    SDState *card = get_slave(sdbus);

    if (card) {
        SDSlaveClass *sc = SDBUS_SLAVE_GET_CLASS(card);

        return sc->get_inserted(card);
    }

    return false;
}

bool sdbus_get_readonly(SDBus *sdbus)
{
    SDState *card = get_slave(sdbus);

    if (card) {
        SDSlaveClass *sc = SDBUS_SLAVE_GET_CLASS(card);

        return sc->get_readonly(card);
    }

    return false;
}

void sdbus_set_inserted(SDBus *sdbus, bool inserted)
{
    SDMasterClass *sbc = SDBUS_MASTER_GET_CLASS(sdbus);
    BusState *qbus = BUS(sdbus);

    if (sbc->set_inserted) {
        sbc->set_inserted(qbus->parent, inserted);
    }
}

void sdbus_set_readonly(SDBus *sdbus, bool readonly)
{
    SDMasterClass *sbc = SDBUS_MASTER_GET_CLASS(sdbus);
    BusState *qbus = BUS(sdbus);

    if (sbc->set_readonly) {
        sbc->set_readonly(qbus->parent, readonly);
    }
}

void sdbus_set_voltage(SDBus *sdbus, uint16_t millivolts)
{
    SDState *slave = get_slave(sdbus);

    sdbus->millivolts = millivolts;
    if (slave) {
        SDSlaveClass *sc = SDBUS_SLAVE_GET_CLASS(slave);

        if (sc->change_voltage) {
            sc->change_voltage(slave, millivolts);
        }
    }
}

void sdbus_reparent_card(SDBus *from, SDBus *to)
{
    SDState *card = get_slave(from);
    SDSlaveClass *sc;
    bool readonly;

    /* We directly reparent the card object rather than implementing this
     * as a hotpluggable connection because we don't want to expose SD cards
     * to users as being hotpluggable, and we can get away with it in this
     * limited use case. This could perhaps be implemented more cleanly in
     * future by adding support to the hotplug infrastructure for "device
     * can be hotplugged only via code, not by user".
     */

    if (!card) {
        return;
    }

    sc = SDBUS_SLAVE_GET_CLASS(card);
    readonly = sc->get_readonly(card);

    sdbus_set_inserted(from, false);
    qdev_set_parent_bus(DEVICE(card), &to->qbus);
    sdbus_set_inserted(to, true);
    sdbus_set_readonly(to, readonly);
}

static void sd_bus_instance_init(Object *obj)
{
    SDBus *s = SD_BUS(obj);

    /* Default 3v3 */
    s->millivolts = 3300;
    object_property_add_uint16_ptr(obj, "millivolts", &s->millivolts, NULL);
}

static const TypeInfo sd_bus_info = {
    .name = TYPE_SD_BUS,
    .parent = TYPE_BUS,
    .instance_init = sd_bus_instance_init,
    .instance_size = sizeof(SDBus),
};

static const TypeInfo sd_master_info = {
    .name = TYPE_SDBUS_MASTER_INTERFACE,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(SDMasterClass),
};

static const TypeInfo sd_slave_info = {
    .name = TYPE_SDBUS_SLAVE_INTERFACE,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(SDSlaveClass),
};

static void sd_bus_register_types(void)
{
    type_register_static(&sd_bus_info);
    type_register_static(&sd_master_info);
    type_register_static(&sd_slave_info);
}

type_init(sd_bus_register_types)

DeviceState *sdbus_create_slave_no_init(SDBus *bus, const char *name)
{
    assert(bus);
    return qdev_create(BUS(bus), name);
}

DeviceState *sdbus_create_slave(SDBus *bus, const char *name)
{
    DeviceState *dev = sdbus_create_slave_no_init(bus, name);

    qdev_init_nofail(dev);
    return dev;
}

SDBus *sdbus_create_bus(DeviceState *parent, const char *name)
{
    return SD_BUS(qbus_create(TYPE_SD_BUS, parent, name));
}
