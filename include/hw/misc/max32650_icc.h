/*
 * MAX32650 Instruction Cache
 *
 * platform-sdk local addition
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MAX32650_ICC_H
#define HW_MAX32650_ICC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MAX32650_ICC "max32650-icc"
OBJECT_DECLARE_SIMPLE_TYPE(Max32650IccState, MAX32650_ICC)

#define ICC_INFO       0x0
#define ICC_SZ         0x4
#define ICC_CTRL       0x100
#define ICC_INVALIDATE 0x700

struct Max32650IccState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t info;
    uint32_t sz;
    uint32_t ctrl;
};

#endif
