/*
 * MAX32650 GPIO
 *
 * platform-sdk local addition
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MAX32650_GPIO_H
#define HW_MAX32650_GPIO_H

#include "hw/sysbus.h"
#include "qom/object.h"

/* Register offsets, relative to the per-instance GPIO bank base address */
#define GPIO_EN0            0x00
#define GPIO_EN0_SET        0x04
#define GPIO_EN0_CLR        0x08
#define GPIO_OUTEN          0x0C
#define GPIO_OUTEN_SET      0x10
#define GPIO_OUTEN_CLR      0x14
#define GPIO_OUT            0x18
#define GPIO_OUT_SET        0x1C
#define GPIO_OUT_CLR        0x20
#define GPIO_IN             0x24
#define GPIO_INTMODE        0x28
#define GPIO_INTPOL         0x2C
#define GPIO_INEN           0x30
#define GPIO_INTEN          0x34
#define GPIO_INTEN_SET      0x38
#define GPIO_INTEN_CLR      0x3C
#define GPIO_INTFL          0x40
#define GPIO_INTFL_CLR      0x48
#define GPIO_WKEN           0x4C
#define GPIO_WKEN_SET       0x50
#define GPIO_WKEN_CLR       0x54
#define GPIO_DUALEDGE       0x5C
#define GPIO_PADCTRL0       0x60
#define GPIO_PADCTRL1       0x64
#define GPIO_EN1            0x68
#define GPIO_EN1_SET        0x6C
#define GPIO_EN1_CLR        0x70
#define GPIO_EN2            0x74
#define GPIO_EN2_SET        0x78
#define GPIO_EN2_CLR        0x7C
#define GPIO_EN3            0x80
#define GPIO_EN3_SET        0x84
#define GPIO_EN3_CLR        0x88
#define GPIO_HYSEN          0xA8
#define GPIO_SRSEL          0xAC
#define GPIO_DS0            0xB0
#define GPIO_DS1            0xB4
#define GPIO_PS             0xB8
#define GPIO_VSSEL          0xC0

/* One 32-pin bank per instance; used for gpio0/gpio1/gpio2 alike */
#define MAX32650_GPIO_NUM_PINS  32

#define TYPE_MAX32650_GPIO "max32650-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(Max32650GpioState, MAX32650_GPIO)

struct Max32650GpioState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    /* Function/direction/output state */
    uint32_t en0;
    uint32_t outen;
    uint32_t out;

    /*
     * IN is derived (not just stored): for a pin with OUTEN set it mirrors
     * OUT (loopback); otherwise it mirrors the externally driven level for
     * that pin, gated by INEN. Cached here so INTFL can be computed from
     * the previous vs. new value.
     */
    uint32_t in;

    /* Raw external pin levels last driven in via qdev_init_gpio_in() */
    uint32_t in_ext;

    /* Interrupt configuration/status */
    uint32_t intmode;
    uint32_t intpol;
    uint32_t inen;
    uint32_t inten;
    uint32_t intfl;
    uint32_t dualedge;

    /* Wake enable */
    uint32_t wken;

    /* Alternate-function enable banks -- stored only, no functional effect */
    uint32_t en1;
    uint32_t en2;
    uint32_t en3;

    /* Pad electrical characteristics -- stored only, no functional effect */
    uint32_t padctrl0;
    uint32_t padctrl1;
    uint32_t hysen;
    uint32_t srsel;
    uint32_t ds0;
    uint32_t ds1;
    uint32_t ps;
    uint32_t vssel;

    qemu_irq irq;
    qemu_irq out_lines[MAX32650_GPIO_NUM_PINS];
};

#endif /* HW_MAX32650_GPIO_H */
