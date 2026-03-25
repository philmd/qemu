/*
 * QEMU Object Model (compat properties stubs for user emulation)
 *
 * Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qom/compat-properties.h"

void object_apply_compat_props(Object *obj)
{
}

bool object_apply_global_props(Object *obj, const GPtrArray *props,
                               Error **errp)
{
    return true;
}
