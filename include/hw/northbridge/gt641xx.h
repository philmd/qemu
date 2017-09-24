#ifndef HW_NORTHBRIDGE_GT641XX_H
#define HW_NORTHBRIDGE_GT641XX_H

#include "hw/irq.h"

/* gt64xxx.c */
PCIBus *gt64120_register(qemu_irq *pic);

#endif
