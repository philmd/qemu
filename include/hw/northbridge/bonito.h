#ifndef HW_NORTHBRIDGE_BONITO_H
#define HW_NORTHBRIDGE_BONITO_H

#include "hw/irq.h"

/* bonito.c */
PCIBus *bonito_init(qemu_irq *pic);

#endif
