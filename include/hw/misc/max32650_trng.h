/*
 * MAX32650 True Random Number Generator
 *
 * platform-sdk local addition
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Register layout is TRNG_REVA (used by MAX32650): just CTRL at 0x00 and a
 * read-only DATA at 0x04, no separate
 * STATUS register -- data-ready is bit RNG_IS within CTRL itself, modeled
 * here as always set since a random value is always available. This
 * project's firmware never touches these registers (it only gates the
 * TRNG's peripheral clock during SystemInit), so none of this is exercised
 * yet -- treat it as unverified against real firmware until something
 * calls MXC_TRNG_*.
 */
#ifndef HW_MAX32650_TRNG_H
#define HW_MAX32650_TRNG_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MAX32650_TRNG "max32650-trng"
OBJECT_DECLARE_SIMPLE_TYPE(Max32650TrngState, MAX32650_TRNG)

#define TRNG_CTRL 0x00
#define TRNG_DATA 0x04

/* CTRL (MXC_F_TRNG_REVA_CTRL_*) */
#define TRNG_CTRL_RNG_IE  (1 << 2)
#define TRNG_CTRL_RNG_ISC (1 << 3)
#define TRNG_CTRL_RNG_IS  (1 << 5)

struct Max32650TrngState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t ctrl;

    qemu_irq irq;
};

#endif
