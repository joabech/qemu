/*
 * MAX32650 GPIO
 *
 * platform-sdk local addition
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/gpio/max32650_gpio.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "trace.h"

/*
 * Recompute IN from OUT/OUTEN/INEN/in_ext, update INTFL for any pin whose
 * level transitioned in a way its INTMODE/INTPOL/DUALEDGE configuration
 * cares about, drive the per-pin output lines for pins in output mode, and
 * refresh the combined IRQ line towards the NVIC.
 */
static void max32650_gpio_update(Max32650GpioState *s)
{
    uint32_t old_in = s->in;
    uint32_t new_in = 0;

    for (int i = 0; i < MAX32650_GPIO_NUM_PINS; i++) {
        uint32_t mask = 1u << i;
        bool level;

        if (s->outen & mask) {
            /* Output pin: IN loops back the driven OUT value. */
            level = s->out & mask;
        } else if (s->inen & mask) {
            /* Input pin with the input path enabled. */
            level = s->in_ext & mask;
        } else {
            /* Input path disabled: no valid level to report. */
            level = false;
        }

        if (level) {
            new_in |= mask;
        }
    }

    for (int i = 0; i < MAX32650_GPIO_NUM_PINS; i++) {
        uint32_t mask = 1u << i;
        bool old_level, new_level, triggered;

        if (!(s->inten & mask) || !(s->inen & mask)) {
            continue;
        }

        old_level = old_in & mask;
        new_level = new_in & mask;

        if (s->intmode & mask) {
            /* Edge-triggered. */
            if (s->dualedge & mask) {
                triggered = old_level != new_level;
            } else if (s->intpol & mask) {
                triggered = !old_level && new_level;   /* rising */
            } else {
                triggered = old_level && !new_level;   /* falling */
            }
        } else {
            /* Level-triggered: keep asserting while the level matches. */
            triggered = (s->intpol & mask) ? new_level : !new_level;
        }

        if (triggered) {
            s->intfl |= mask;
        }
    }

    s->in = new_in;

    for (int i = 0; i < MAX32650_GPIO_NUM_PINS; i++) {
        uint32_t mask = 1u << i;
        bool level = (s->outen & mask) && (s->out & mask);

        qemu_set_irq(s->out_lines[i], level);
    }

    qemu_set_irq(s->irq, (s->intfl & s->inten) != 0);
}

static void max32650_gpio_set(void *opaque, int line, int level)
{
    Max32650GpioState *s = opaque;
    uint32_t mask = 1u << line;

    if (level) {
        s->in_ext |= mask;
    } else {
        s->in_ext &= ~mask;
    }

    max32650_gpio_update(s);
}

static void max32650_gpio_reset_hold(Object *obj, ResetType type)
{
    Max32650GpioState *s = MAX32650_GPIO(obj);

    s->en0 = 0;
    s->outen = 0;
    s->out = 0;
    s->in = 0;
    s->in_ext = 0;
    s->intmode = 0;
    s->intpol = 0;
    s->inen = 0;
    s->inten = 0;
    s->intfl = 0;
    s->dualedge = 0;
    s->wken = 0;
    s->en1 = 0;
    s->en2 = 0;
    s->en3 = 0;
    s->padctrl0 = 0;
    s->padctrl1 = 0;
    s->hysen = 0;
    s->srsel = 0;
    s->ds0 = 0;
    s->ds1 = 0;
    s->ps = 0;
    s->vssel = 0;

    max32650_gpio_update(s);
}

static uint64_t max32650_gpio_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    Max32650GpioState *s = opaque;
    uint64_t retvalue = 0;

    switch (addr) {
    case GPIO_EN0:
        retvalue = s->en0;
        break;
    case GPIO_OUTEN:
        retvalue = s->outen;
        break;
    case GPIO_OUT:
        retvalue = s->out;
        break;
    case GPIO_IN:
        retvalue = s->in;
        break;
    case GPIO_INTMODE:
        retvalue = s->intmode;
        break;
    case GPIO_INTPOL:
        retvalue = s->intpol;
        break;
    case GPIO_INEN:
        retvalue = s->inen;
        break;
    case GPIO_INTEN:
        retvalue = s->inten;
        break;
    case GPIO_INTFL:
        retvalue = s->intfl;
        break;
    case GPIO_WKEN:
        retvalue = s->wken;
        break;
    case GPIO_DUALEDGE:
        retvalue = s->dualedge;
        break;
    case GPIO_PADCTRL0:
        retvalue = s->padctrl0;
        break;
    case GPIO_PADCTRL1:
        retvalue = s->padctrl1;
        break;
    case GPIO_EN1:
        retvalue = s->en1;
        break;
    case GPIO_EN2:
        retvalue = s->en2;
        break;
    case GPIO_EN3:
        retvalue = s->en3;
        break;
    case GPIO_HYSEN:
        retvalue = s->hysen;
        break;
    case GPIO_SRSEL:
        retvalue = s->srsel;
        break;
    case GPIO_DS0:
        retvalue = s->ds0;
        break;
    case GPIO_DS1:
        retvalue = s->ds1;
        break;
    case GPIO_PS:
        retvalue = s->ps;
        break;
    case GPIO_VSSEL:
        retvalue = s->vssel;
        break;
    /* Write-only SET/CLR side-effect registers read back as 0. */
    case GPIO_EN0_SET:
    case GPIO_EN0_CLR:
    case GPIO_OUTEN_SET:
    case GPIO_OUTEN_CLR:
    case GPIO_OUT_SET:
    case GPIO_OUT_CLR:
    case GPIO_INTEN_SET:
    case GPIO_INTEN_CLR:
    case GPIO_INTFL_CLR:
    case GPIO_WKEN_SET:
    case GPIO_WKEN_CLR:
    case GPIO_EN1_SET:
    case GPIO_EN1_CLR:
    case GPIO_EN2_SET:
    case GPIO_EN2_CLR:
    case GPIO_EN3_SET:
    case GPIO_EN3_CLR:
        retvalue = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }

    return retvalue;
}

static void max32650_gpio_write(void *opaque, hwaddr addr,
                                 uint64_t val64, unsigned int size)
{
    Max32650GpioState *s = opaque;
    uint32_t value = val64;

    switch (addr) {
    case GPIO_EN0:
        s->en0 = value;
        return;
    case GPIO_EN0_SET:
        s->en0 |= value;
        return;
    case GPIO_EN0_CLR:
        s->en0 &= ~value;
        return;
    case GPIO_OUTEN:
        s->outen = value;
        max32650_gpio_update(s);
        return;
    case GPIO_OUTEN_SET:
        s->outen |= value;
        max32650_gpio_update(s);
        return;
    case GPIO_OUTEN_CLR:
        s->outen &= ~value;
        max32650_gpio_update(s);
        return;
    case GPIO_OUT:
        s->out = value;
        max32650_gpio_update(s);
        return;
    case GPIO_OUT_SET:
        s->out |= value;
        max32650_gpio_update(s);
        return;
    case GPIO_OUT_CLR:
        s->out &= ~value;
        max32650_gpio_update(s);
        return;
    case GPIO_IN:
        /* IN is read-only. */
        return;
    case GPIO_INTMODE:
        s->intmode = value;
        max32650_gpio_update(s);
        return;
    case GPIO_INTPOL:
        s->intpol = value;
        max32650_gpio_update(s);
        return;
    case GPIO_INEN:
        s->inen = value;
        max32650_gpio_update(s);
        return;
    case GPIO_INTEN:
        s->inten = value;
        max32650_gpio_update(s);
        return;
    case GPIO_INTEN_SET:
        s->inten |= value;
        max32650_gpio_update(s);
        return;
    case GPIO_INTEN_CLR:
        s->inten &= ~value;
        max32650_gpio_update(s);
        return;
    case GPIO_INTFL:
        /* INTFL is read-only; cleared via INTFL_CLR. */
        return;
    case GPIO_INTFL_CLR:
        s->intfl &= ~value;
        max32650_gpio_update(s);
        return;
    case GPIO_WKEN:
        s->wken = value;
        return;
    case GPIO_WKEN_SET:
        s->wken |= value;
        return;
    case GPIO_WKEN_CLR:
        s->wken &= ~value;
        return;
    case GPIO_DUALEDGE:
        s->dualedge = value;
        max32650_gpio_update(s);
        return;
    case GPIO_PADCTRL0:
        s->padctrl0 = value;
        return;
    case GPIO_PADCTRL1:
        s->padctrl1 = value;
        return;
    case GPIO_EN1:
        s->en1 = value;
        return;
    case GPIO_EN1_SET:
        s->en1 |= value;
        return;
    case GPIO_EN1_CLR:
        s->en1 &= ~value;
        return;
    case GPIO_EN2:
        s->en2 = value;
        return;
    case GPIO_EN2_SET:
        s->en2 |= value;
        return;
    case GPIO_EN2_CLR:
        s->en2 &= ~value;
        return;
    case GPIO_EN3:
        s->en3 = value;
        return;
    case GPIO_EN3_SET:
        s->en3 |= value;
        return;
    case GPIO_EN3_CLR:
        s->en3 &= ~value;
        return;
    case GPIO_HYSEN:
        s->hysen = value;
        return;
    case GPIO_SRSEL:
        s->srsel = value;
        return;
    case GPIO_DS0:
        s->ds0 = value;
        return;
    case GPIO_DS1:
        s->ds1 = value;
        return;
    case GPIO_PS:
        s->ps = value;
        return;
    case GPIO_VSSEL:
        s->vssel = value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"
            HWADDR_PRIx "\n", __func__, addr);
    }
}

static const MemoryRegionOps max32650_gpio_ops = {
    .read = max32650_gpio_read,
    .write = max32650_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription max32650_gpio_vmstate = {
    .name = TYPE_MAX32650_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(en0, Max32650GpioState),
        VMSTATE_UINT32(outen, Max32650GpioState),
        VMSTATE_UINT32(out, Max32650GpioState),
        VMSTATE_UINT32(in, Max32650GpioState),
        VMSTATE_UINT32(in_ext, Max32650GpioState),
        VMSTATE_UINT32(intmode, Max32650GpioState),
        VMSTATE_UINT32(intpol, Max32650GpioState),
        VMSTATE_UINT32(inen, Max32650GpioState),
        VMSTATE_UINT32(inten, Max32650GpioState),
        VMSTATE_UINT32(intfl, Max32650GpioState),
        VMSTATE_UINT32(dualedge, Max32650GpioState),
        VMSTATE_UINT32(wken, Max32650GpioState),
        VMSTATE_UINT32(en1, Max32650GpioState),
        VMSTATE_UINT32(en2, Max32650GpioState),
        VMSTATE_UINT32(en3, Max32650GpioState),
        VMSTATE_UINT32(padctrl0, Max32650GpioState),
        VMSTATE_UINT32(padctrl1, Max32650GpioState),
        VMSTATE_UINT32(hysen, Max32650GpioState),
        VMSTATE_UINT32(srsel, Max32650GpioState),
        VMSTATE_UINT32(ds0, Max32650GpioState),
        VMSTATE_UINT32(ds1, Max32650GpioState),
        VMSTATE_UINT32(ps, Max32650GpioState),
        VMSTATE_UINT32(vssel, Max32650GpioState),
        VMSTATE_END_OF_LIST()
    }
};

static void max32650_gpio_init(Object *obj)
{
    Max32650GpioState *s = MAX32650_GPIO(obj);
    DeviceState *dev = DEVICE(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &max32650_gpio_ops, s,
                          TYPE_MAX32650_GPIO, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    qdev_init_gpio_out(dev, s->out_lines, MAX32650_GPIO_NUM_PINS);
    qdev_init_gpio_in(dev, max32650_gpio_set, MAX32650_GPIO_NUM_PINS);
}

static void max32650_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = max32650_gpio_reset_hold;

    dc->vmsd = &max32650_gpio_vmstate;
}

static const TypeInfo max32650_gpio_info = {
    .name          = TYPE_MAX32650_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Max32650GpioState),
    .instance_init = max32650_gpio_init,
    .class_init    = max32650_gpio_class_init,
};

static void max32650_gpio_register_types(void)
{
    type_register_static(&max32650_gpio_info);
}

type_init(max32650_gpio_register_types)
